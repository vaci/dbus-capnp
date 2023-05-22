// Copyright (c) 2023 Vaci Koblizek.
// Licensed under the Apache 2.0 license found in the LICENSE file or at:
//     https://opensource.org/licenses/Apache-2.0

#include "dbus.h"
#include "message.h"

#include <systemd/sd-bus.h>
#include <capnp/dynamic.h>
#include <kj/array.h>
#include <kj/async.h>
#include <kj/debug.h>
#include <kj/refcount.h>
#include <kj/timer.h>

#include <cstdio>

#include <fcntl.h>
#include <poll.h>


#define CHECK_ERROR(CALL, ERRMSG) { KJ_REQUIRE(CALL) >= 0, ERRMSG)
int message_handler(sd_bus_message* m, void* userdata, sd_bus_error* ret_error) {
  return 0;
}


#define BUS_REQUIRE(cond, ...) KJ_REQUIRE(cond, __VA_ARGS__) >= 0


namespace dbus {

  namespace {

  kj::Promise<void> append(sd_bus_message* msg, Field::Reader field) {
    auto which = field.which();
    using Which = decltype(which);
      switch (which) {
      case Which::BYTE:
	KJ_REQUIRE(::sd_bus_message_append(msg, "y", field.getByte()) >= 0);
	return kj::READY_NOW;
      case Which::BOOL:
	KJ_REQUIRE(::sd_bus_message_append(msg, "b", field.getBool()) >= 0);
	return kj::READY_NOW;
      case Which::INT16:
	KJ_REQUIRE(::sd_bus_message_append(msg, "n", field.getInt16()) >= 0);
	return kj::READY_NOW;
      case Which::UINT16:
	KJ_REQUIRE(::sd_bus_message_append(msg, "q", field.getUint16()) >= 0);
	return kj::READY_NOW;
      case Which::INT32:
	KJ_REQUIRE(::sd_bus_message_append(msg, "i", field.getInt32()) >= 0);
	return kj::READY_NOW;
      case Which::UINT32:
	KJ_REQUIRE(::sd_bus_message_append(msg, "u", field.getUint32()) >= 0);
	return kj::READY_NOW;
      case Which::INT64:
	KJ_REQUIRE(::sd_bus_message_append(msg, "x", field.getInt64()) >= 0);
	return kj::READY_NOW;
      case Which::UINT64:
	KJ_REQUIRE(::sd_bus_message_append(msg, "t", field.getUint64()) >= 0);
	return kj::READY_NOW;
      case Which::DOUBLE:
	KJ_REQUIRE(::sd_bus_message_append(msg, "d", field.getDouble()) >= 0);
	return kj::READY_NOW;
      case Which::STRING:
	KJ_REQUIRE(::sd_bus_message_append(msg, "s", field.getString()) >= 0);
	return kj::READY_NOW;
      case Which::OBJECT_PATH:
	KJ_REQUIRE(::sd_bus_message_append(msg, "o", field.getObjectPath()) >= 0);
	return kj::READY_NOW;
      case Which::SIGNATURE:
	KJ_REQUIRE(::sd_bus_message_append(msg, "g", field.getSignature()) >= 0);
	return kj::READY_NOW;
      case Which::UNIX: {
	sd_bus_message_ref(msg);
	return
	  field.getUnix().getFd()
	  .then(
	    [msg](auto maybeFd) {
	      KJ_IF_MAYBE(fd, maybeFd) {
		KJ_REQUIRE(::sd_bus_message_append(msg, "h", fd) >= 0);
	      }
	    }
	  )
	  .attach(
	    kj::defer([msg]{ ::sd_bus_message_unref(msg); })
	  );
      }
      default:
	return kj::READY_NOW;
      }
    }

    struct BusServer;

    struct Slot {
      ~Slot();

      sd_bus_slot* slot_;
      kj::Own<kj::PromiseFulfiller<sd_bus_message*>> fulfiller_;
    };

    struct BusServer
      : Bus::Server
      , kj::TaskSet::ErrorHandler 
      , kj::Refcounted {


      kj::Own<BusServer> addRef() {
	return kj::addRef(*this);
      }

      void handleMessage(sd_bus_message* msg, sd_bus_error* err) {
      }

      static int handleMessage(sd_bus_message* msg, void* userdata, sd_bus_error* err) {
	auto slot = reinterpret_cast<Slot*>(userdata);
	if (::sd_bus_error_is_set(err)) {
	  auto name = err->name;
	  auto message = err->message;
	  slot->fulfiller_->reject(KJ_EXCEPTION(FAILED, err, message));
	}
	else {
	  sd_bus_message_ref(msg);
	  slot->fulfiller_->fulfill(kj::mv(msg));
	}
	return 0;
      }


      BusServer(kj::Timer& timer, sd_bus*);
      ~BusServer();

      kj::Promise<void> call(CallContext ctx) override;

      void taskFailed(kj::Exception&& exc) {
	KJ_LOG(ERROR, exc);
      }

      kj::Promise<void> poll() {
	return
	  timer_.afterDelay(kj::MILLISECONDS*200)
	  .then(
	    [this]{
	      int fd = ::sd_bus_get_fd(bus_);
	      int events = ::sd_bus_get_events(bus_);
	      //uint64_t timeout;
	      //::sd_bus_get_timeout(bus_, &timeout);

	      struct pollfd fds[1];
	      fds[0].fd = fd;
	      fds[0].events = events;
	      ::poll(fds, 1, 0);
	      sd_bus_message* msg;
	      ::sd_bus_process(bus_, &msg);
	      if (msg == nullptr) {
		//KJ_LOG(INFO, "Progress made!", events);
	      }
	      else {
		KJ_LOG(INFO, "Got message");
	      }
				   
	    }
	  )
	  .then(
	    [this]{
	      return poll();
	    }
	  );
      }

      kj::Timer& timer_;
      sd_bus* bus_;
      kj::TaskSet tasks_{*this};
    };
  }

  Slot::~Slot() {
    ::sd_bus_slot_unref(slot_);
  }

  BusServer::BusServer(kj::Timer& timer, sd_bus* bus)
    : timer_{timer}
    , bus_{bus} {
    tasks_.add(poll());
  }

  BusServer::~BusServer() {
    ::sd_bus_close(bus_);
  }

  kj::Promise<void> BusServer::call(CallContext ctx) {
    ::sd_bus_message* call;
    ::sd_bus_message* reply;

    auto params = ctx.getParams();
    auto fields = params.getFields();

    ::sd_bus_message* msg;
    KJ_REQUIRE(::sd_bus_message_new_method_call(
	  bus_, &msg,
	  params.hasDestination() ? params.getDestination().cStr() : nullptr,
	  params.hasPath() ? params.getPath().cStr() : nullptr,
	  params.hasIface() ? params.getIface().cStr() : nullptr,
	  params.getMember().cStr()
       ) >= 0);
		 

    kj::Promise<void> promise = kj::READY_NOW;
    for (auto field: fields) {
      promise = promise.then([msg, field]{ return append(msg, field); });
    }

    return
      promise.then(
        [this,  ctx = kj::mv(ctx), msg]() mutable {
	  auto slot = kj::heap<Slot>();
	  auto paf = kj::newPromiseAndFulfiller<sd_bus_message*>();
	  slot->fulfiller_ = kj::mv(paf.fulfiller);

	  KJ_REQUIRE(::sd_bus_call_async(
	   bus_,
	   &slot->slot_,
	   msg,
	   &BusServer::handleMessage, slot,
	   0) >= 0);

	  return
	    paf.promise.then(
	      [ctx = kj::mv(ctx)](auto msg) mutable {
		::sd_bus_message_dump(msg, stderr, 0);
		::sd_bus_message_rewind(msg, 0);
		if (::sd_bus_message_is_method_error(msg, nullptr)) {
		  auto* err = ::sd_bus_message_get_error(msg);
		  auto errname = err->name;
		  auto errmsg  = err->message;
		  throw KJ_EXCEPTION(FAILED, errname, errmsg);
		}
		
	       auto reply = ctx.getResults();
	       KJ_IF_MAYBE(value, ::sd_bus_message_get_destination(msg)) {
		 reply.setDestination(value);
	       }
	       KJ_IF_MAYBE(value, ::sd_bus_message_get_path(msg)) {
		 reply.setPath(value);
	       }
	       KJ_IF_MAYBE(value, ::sd_bus_message_get_interface(msg)) {
		 reply.setIface(value);
	       }
	       KJ_IF_MAYBE(value, ::sd_bus_message_get_member(msg)) {
		 reply.setMember(value);
	       }
	       {
		 auto orphanage = capnp::Orphanage::getForMessageContaining(reply);
		 auto fields = extract(msg, orphanage);
		 reply.adoptFields(kj::mv(fields));
	       }
	      }
	    )
	    .attach(kj::mv(slot));
	}
     );
  }

  struct ManagerServer
    : capnp::DynamicCapability::Server {

    kj::Promise<void> call(
      capnp::InterfaceSchema::Method method,
      capnp::CallContext<capnp::DynamicStruct, capnp::DynamicStruct> ctx) override {

      auto req = bus_.callRequest();
      
      auto name = method.getProto().getName();

      req.setMember(name);
      
      auto params = ctx.getParams();
      auto schema = params.getSchema();

      
      auto fields = req.initFields(schema.getFields().size());
      
      for (auto ii: kj::indices(fields)) {
	auto type = schema.getFields()[ii].getType();
	auto param = params.get(schema.getFields()[ii]);
	auto which = type.which();
	using Which = decltype(which);
	switch (which) {
	case Which::UINT8: {
	  fields[ii].setByte(param.as<capnp::byte>());
	  break;
	}
	case Which::BOOL: {
	  fields[ii].setBool(param.as<bool>());
	  break;
	}
	case Which::INT16: {
	  fields[ii].setInt32(param.as<int32_t>());
	  break;
	}
	case Which::UINT16: {
	  fields[ii].setUint64(param.as<uint32_t>());
	  break;
	}
	case Which::INT32: {
	  fields[ii].setInt32(param.as<int32_t>());
	  break;
	}
	case Which::UINT32: {
	  fields[ii].setUint64(param.as<uint32_t>());
	  break;
	}
	case Which::INT64: {
	  fields[ii].setInt64(param.as<int64_t>());
	  break;
	}
	case Which::UINT64: {
	  fields[ii].setUint64(param.as<uint64_t>());
	  break;
	}
	case Which::FLOAT32: {
	  fields[ii].setDouble(param.as<float>());
	  break;
	}
	case Which::FLOAT64: {
	  fields[ii].setDouble(param.as<double>());
	  break;
	}
	case Which::TEXT: {
	  fields[ii].setString(param.as<capnp::Text>());
	  break;
	}
	case Which::INTERFACE:
	  fields[ii].setUnix(param.as<capnp::Capability>());
	  break;
	}
      }
     
      
      return kj::READY_NOW;
    }

    Bus::Client bus_{nullptr};
  };

  struct DbusServer
    : Dbus::Server {

    DbusServer(kj::Timer& timer)
      : timer_{timer} {
    }

    kj::Promise<void> user(UserContext ctx) override {
      auto params = ctx.getParams();
      auto desc = params.getDescription();
      auto reply = ctx.getResults();
      ::sd_bus* bus;
      KJ_REQUIRE(::sd_bus_open_user_with_description(&bus, desc.cStr()) >= 0);
      reply.setBus(kj::refcounted<BusServer>(timer_, bus));
      return kj::READY_NOW;
    }

    kj::Promise<void> system(SystemContext ctx) override {
      auto params = ctx.getParams();
      auto desc = params.getDescription();
      auto reply = ctx.getResults();
      ::sd_bus* bus;
      KJ_REQUIRE(::sd_bus_open_system_with_description(&bus, desc.cStr()) >= 0);
      reply.setBus(kj::refcounted<BusServer>(timer_, bus));
      return kj::READY_NOW;
    }
 
    kj::Timer& timer_;
  };

  Dbus::Client newDbus(kj::Timer& timer) {
    return kj::heap<DbusServer>(timer);
  }
}
