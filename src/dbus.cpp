// Copyright (c) 2023 Vaci Koblizek.
// Licensed under the Apache 2.0 license found in the LICENSE file or at:
//     https://opensource.org/licenses/Apache-2.0

#include "dbus.h"
#include "message.h"

#include <systemd/sd-bus.h>
#include <capnp/dynamic.h>
#include <kj/array.h>
#include <kj/async.h>
#include <kj/async-unix.h>
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

    struct BusServer;

    struct BusServer
      : Bus::Server
      , kj::TaskSet::ErrorHandler 
      , kj::Refcounted {


      kj::Own<BusServer> addRef() {
	return kj::addRef(*this);
      }

      BusServer(kj::UnixEventPort& port_, kj::Timer& timer, sd_bus*);
      ~BusServer();

      kj::Promise<void> call(CallContext ctx) override;

      void taskFailed(kj::Exception&& exc) {
	KJ_LOG(ERROR, exc);
      }

      kj::Promise<void> poll() {
	int fd = sd_bus_get_fd(bus_);
	int events = sd_bus_get_events(bus_);
	uint64_t timeout;
	KJ_REQUIRE(sd_bus_get_timeout(bus_, &timeout) >= 0);

	auto flags = kj::UnixEventPort::FdObserver::OBSERVE_READ_WRITE;
	auto obs = kj::heap<kj::UnixEventPort::FdObserver>(port_, fd, flags);

	return
	  timer_.timeoutAfter(
	    timeout * kj::MICROSECONDS,
	    obs->whenBecomesReadable()
	    .then(
	      [this]{
		sd_bus_message* msg;
		int err;
		do {
		  err = sd_bus_process(bus_, &msg);
		} while (err > 0);
	      }
	    )
	    .attach(kj::mv(obs))
	  )
	  .then(
	    [this]{
	      return poll();
	    }
	  );
      }

      kj::UnixEventPort& port_;
      kj::Timer& timer_;
      sd_bus* bus_;
      kj::Canceler cancel_;
      kj::TaskSet tasks_{*this};
    };
  }

  BusServer::BusServer(kj::UnixEventPort& port, kj::Timer& timer, sd_bus* bus)
    : port_{port}
    , timer_{timer}
    , bus_{bus} {
    tasks_.add(cancel_.wrap(poll()));
  }

  BusServer::~BusServer() {
    cancel_.cancel(KJ_EXCEPTION(DISCONNECTED));
    sd_bus_close(bus_);
  }

  kj::Promise<void> BusServer::call(CallContext ctx) {
    sd_bus_message* call;
    sd_bus_message* reply;

    auto params = ctx.getParams();
    auto fields = params.getFields();

    sd_bus_message* msg;
    KJ_REQUIRE(sd_bus_message_new_method_call(
	  bus_, &msg,
	  params.hasDestination() ? params.getDestination().cStr() : nullptr,
	  params.hasPath() ? params.getPath().cStr() : nullptr,
	  params.hasIface() ? params.getIface().cStr() : nullptr,
	  params.getMember().cStr()
       ) >= 0);
		 

    kj::Promise<void> promise = kj::READY_NOW;
    for (auto field: fields) {
      promise = promise.then([msg, field]{ return _::append(msg, field); });
    }

    return
      promise.then(
        [this,  ctx = kj::mv(ctx), msg]() mutable {
	  return
	    _::call(bus_, msg)
	    .then(
	      [ctx = kj::mv(ctx)](auto msg) mutable -> kj::Promise<void> {
		if (sd_bus_message_is_method_error(msg, nullptr)) {
		  return _::err(msg);
		}
		
	       _::build(ctx.getResults(), msg);
	       return kj::READY_NOW;
	      }
	    );
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
      _::setFields(req, ctx.getParams());
     
      return
	req.send()
	.then(
	      [ctx = kj::mv(ctx)](auto msg) mutable {
		auto reply = ctx.getResults();
		auto schema = reply.getSchema();
		for (auto ii: kj::indices(msg.getFields())) {
		}
	      }
	      );
	      
    }

    Bus::Client bus_{nullptr};
  };

  struct DbusServer
    : Dbus::Server {

    DbusServer(kj::UnixEventPort& port, kj::Timer& timer)
      : port_{port}
      , timer_{timer} {
    }

    kj::Promise<void> user(UserContext ctx) override {
      auto params = ctx.getParams();
      auto desc = params.getDescription();
      auto reply = ctx.getResults();
      sd_bus* bus;
      KJ_REQUIRE(sd_bus_open_user_with_description(&bus, desc.cStr()) >= 0);
      reply.setBus(kj::refcounted<BusServer>(port_, timer_, bus));
      return kj::READY_NOW;
    }

    kj::Promise<void> system(SystemContext ctx) override {
      auto params = ctx.getParams();
      auto desc = params.getDescription();
      auto reply = ctx.getResults();
      sd_bus* bus;
      KJ_REQUIRE(sd_bus_open_system_with_description(&bus, desc.cStr()) >= 0);
      reply.setBus(kj::refcounted<BusServer>(port_, timer_, bus));
      return kj::READY_NOW;
    }

    kj::UnixEventPort& port_; 
    kj::Timer& timer_;
  };

  Dbus::Client newDbus(kj::UnixEventPort& port, kj::Timer& timer) {
    return kj::heap<DbusServer>(port, timer);
  }
}
