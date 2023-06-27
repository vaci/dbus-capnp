// Copyright (c) 2023 Vaci Koblizek.
// Licensed under the Apache 2.0 license found in the LICENSE file or at:
//     https://opensource.org/licenses/Apache-2.0

#include "message.h"

#include <capnp/dynamic.h>
#include <capnp/schema.h>
#include <kj/debug.h>

#include <fcntl.h>

namespace dbus {

  namespace _ {

  kj::Exception err(::sd_bus_message* msg) {
    auto* err = ::sd_bus_message_get_error(msg);
    auto errname = err->name;
    auto errmsg  = err->message;
    return KJ_EXCEPTION(FAILED, errname, errmsg);
  }

  auto ref(::sd_bus_message* msg) {
    sd_bus_message_ref(msg);
    return kj::defer([msg] { ::sd_bus_message_unref(msg); });
  }

  namespace {
    struct UnixServer: Unix::Server {
      UnixServer(int fd): fd_{fd} {}
      ~UnixServer() { ::close(fd_); }
      kj::Maybe<int> getFd() override { return fd_; }
      int fd_;
    };
  }

  capnp::Orphan<capnp::List<Field>> extract(sd_bus_message* msg, capnp::Orphanage orphanage) {
    auto fields = orphanage.newOrphan<capnp::List<Field>>(0);
    int count = 0;
    while (true) {
      char type;
      const char* contents;
      int err;
      KJ_REQUIRE((err = ::sd_bus_message_peek_type(msg, &type, &contents)) >= 0);
      if (err == 0) {
	break;
      }
      count++;
      fields.truncate(count);
      auto field = fields.get()[count-1];
      switch (type) {
      case 'y': {
	capnp::byte value;
	::sd_bus_message_read_basic(msg, type, &value);
	field.setByte(value);
	break;
      }
      case 'b': {
	int value;
	::sd_bus_message_read_basic(msg, type, &value);
	field.setBool(value);
	break;
      }
      case 'n': {
	int16_t value;
	::sd_bus_message_read_basic(msg, type, &value);
	field.setInt16(value);
	break;
      }
      case 'q': {
	uint16_t value;
	::sd_bus_message_read_basic(msg, type, &value);
	field.setUint16(value);
	break;
      }
      case 'i': {
	int32_t value;
	::sd_bus_message_read_basic(msg, type, &value);
	field.setInt32(value);
	break;
      }
      case 'u': {
	uint32_t value;
	::sd_bus_message_read_basic(msg, type, &value);
	field.setUint32(value);
	break;
      }
      case 'x': {
	int64_t value;
	::sd_bus_message_read_basic(msg, type, &value);
	field.setInt64(value);
	break;
      }
      case 't': {
	uint64_t value;
	::sd_bus_message_read_basic(msg, type, &value);
	field.setUint64(value);
	break;
      }
      case 'd': {
	double value;
	::sd_bus_message_read_basic(msg, type, &value);
	field.setDouble(value);
	break;
      }
      case 'g': {
	const char* value;
	::sd_bus_message_read_basic(msg, type, &value);
	field.setSignature(value);
	break;
      }
      case 'o': {
	const char* value;
	::sd_bus_message_read_basic(msg, type, &value);
	field.setObjectPath(value);
	break;
      }
      case 's': {
	const char* value;
	::sd_bus_message_read_basic(msg, type, &value);
	field.setString(value);
	break;
      }
      case 'a': {
	::sd_bus_message_enter_container(msg, SD_BUS_TYPE_ARRAY, contents);
	auto content = extract(msg, orphanage);
	field.adoptArray(kj::mv(content));
	::sd_bus_message_exit_container(msg);
	break;
      }
      case 'h': {
	int value;
	::sd_bus_message_read_basic(msg, type, &value);
	int fd;
	KJ_SYSCALL(fd = ::fcntl(value, F_DUPFD_CLOEXEC, 3), "dup");
	field.setUnix(kj::heap<UnixServer>(fd));
	break;
      }
      case 'r':
      case '(': {
	::sd_bus_message_enter_container(msg, SD_BUS_TYPE_STRUCT, contents);
	auto content = extract(msg, orphanage);
	field.adoptStructure(kj::mv(content));
	::sd_bus_message_exit_container(msg);
	break;
      }
      case '{': {
	::sd_bus_message_enter_container(msg, SD_BUS_TYPE_DICT_ENTRY, contents);
	auto content = extractDictionary(msg, orphanage);
	field.adoptDictionary(kj::mv(content));
	::sd_bus_message_exit_container(msg);
	break;
      }
      default:
	KJ_LOG(INFO, "Skipping ", type);
	sd_bus_message_skip(msg, nullptr);
	break;
      }
    }
    return fields;
  }

  capnp::Orphan<capnp::List<KeyValue>> extractDictionary(sd_bus_message* msg, capnp::Orphanage orphanage) {
    auto fields = orphanage.newOrphan<capnp::List<KeyValue>>(0);
    return fields;
  }

  namespace {

    struct Slot {
      ~Slot() {
	if (slot_) {
	  ::sd_bus_slot_unref(slot_);
	}
      }

      sd_bus_slot* slot_{nullptr};
      kj::Own<kj::PromiseFulfiller<sd_bus_message*>> fulfiller_;
    };

    int callback(sd_bus_message* msg, void* userdata, sd_bus_error* err) {
      auto slot = reinterpret_cast<Slot*>(userdata);
      ::sd_bus_message_ref(msg);
      slot->fulfiller_->fulfill(kj::mv(msg));
      return 0;
    }
  }

  kj::Promise<::sd_bus_message*> call(::sd_bus* bus, ::sd_bus_message* msg) {
    auto slot = kj::heap<Slot>();
    auto paf = kj::newPromiseAndFulfiller<sd_bus_message*>();
    slot->fulfiller_ = kj::mv(paf.fulfiller);
    auto err = ::sd_bus_call_async(bus, &slot->slot_, msg, &callback, slot, 0);
    KJ_REQUIRE(err >= 0, "sd_bus_call_async");
    return paf.promise.attach(kj::mv(slot));
  }

  void setFields(
    Message::Builder builder,
    capnp::DynamicStruct::Reader params) {

    auto schema = params.getSchema();
    auto fieldSchemas = schema.getFields();
    auto fields = builder.initFields(fieldSchemas.size());

    for (auto ii: kj::indices(fields)) {
      auto fieldSchema = fieldSchemas[ii];
      _::buildField(fields[ii], params.get(fieldSchema), fieldSchema.getType());
    }
  }

  void buildArray(
    Field::Builder builder,
    capnp::DynamicList::Reader value) {
    auto schema = value.getSchema();
    auto fields = builder.initArray(value.size());
    for (auto ii: kj::indices(value)) {
      _::buildField(fields[ii], value[ii], schema.getElementType());
    }
  }

  void buildField(
    Field::Builder builder,
    capnp::DynamicValue::Reader value, capnp::Type type) {

    auto which = type.which();
    using Which = decltype(which);
    switch (which) {
    case Which::UINT8:
      builder.setByte(value.as<capnp::byte>());
      break;
    case Which::BOOL:
      builder.setBool(value.as<bool>());
      break;
    case Which::INT16:
      builder.setInt32(value.as<int32_t>());
      break;
    case Which::UINT16:
      builder.setUint64(value.as<uint32_t>());
      break;
    case Which::INT32:
      builder.setInt32(value.as<int32_t>());
      break;
    case Which::UINT32:
      builder.setUint64(value.as<uint32_t>());
      break;
    case Which::INT64:
      builder.setInt64(value.as<int64_t>());
      break;
    case Which::UINT64:
      builder.setUint64(value.as<uint64_t>());
      break;
    case Which::FLOAT32:
      builder.setDouble(value.as<float>());
      break;
    case Which::FLOAT64:
      builder.setDouble(value.as<double>());
      break;
    case Which::TEXT:
      builder.setString(value.as<capnp::Text>());
      break;
    case Which::INTERFACE:
      builder.setUnix(value.as<capnp::Capability>());
      break;
    case Which::LIST:
      buildArray(builder, value.as<capnp::DynamicList>());
      break;
    }
  }

  kj::Promise<void> append(sd_bus_message* msg, Field::Reader field) {
    auto which = field.which();
    using Which = decltype(which);
    switch (which) {
    case Which::BYTE:
      KJ_REQUIRE(::sd_bus_message_append(msg, "y", field.getByte()) >= 0);
      break;
    case Which::BOOL:
      KJ_REQUIRE(::sd_bus_message_append(msg, "b", field.getBool()) >= 0);
      break;
    case Which::INT16:
      KJ_REQUIRE(::sd_bus_message_append(msg, "n", field.getInt16()) >= 0);
      break;
    case Which::UINT16:
      KJ_REQUIRE(::sd_bus_message_append(msg, "q", field.getUint16()) >= 0);
      break;
    case Which::INT32:
      KJ_REQUIRE(::sd_bus_message_append(msg, "i", field.getInt32()) >= 0);
      break;
    case Which::UINT32:
      KJ_REQUIRE(::sd_bus_message_append(msg, "u", field.getUint32()) >= 0);
      break;
    case Which::INT64:
      KJ_REQUIRE(::sd_bus_message_append(msg, "x", field.getInt64()) >= 0);
      break;
    case Which::UINT64:
      KJ_REQUIRE(::sd_bus_message_append(msg, "t", field.getUint64()) >= 0);
      break;
    case Which::DOUBLE:
      KJ_REQUIRE(::sd_bus_message_append(msg, "d", field.getDouble()) >= 0);
      break;
    case Which::STRING:
      KJ_REQUIRE(::sd_bus_message_append(msg, "s", field.getString()) >= 0);
      break;
    case Which::OBJECT_PATH:
      KJ_REQUIRE(::sd_bus_message_append(msg, "o", field.getObjectPath()) >= 0);
      break;
    case Which::SIGNATURE:
      KJ_REQUIRE(::sd_bus_message_append(msg, "g", field.getSignature()) >= 0);
      break;
    case Which::UNIX: {
      return
	field.getUnix().getFd()
	.then(
	  [msg](auto maybeFd) {
	    KJ_IF_MAYBE(fd, maybeFd) {
	      KJ_REQUIRE(::sd_bus_message_append(msg, "h", fd) >= 0);
	    }
	  }
	)
	.attach(ref(msg));
    }
    default:
      break;
    }

    return kj::READY_NOW;
  }
 
  void build(Message::Builder builder, ::sd_bus_message* msg) {
    KJ_IF_MAYBE(value, ::sd_bus_message_get_destination(msg)) {
      builder.setDestination(value);
    }
    KJ_IF_MAYBE(value, ::sd_bus_message_get_path(msg)) {
      builder.setPath(value);
    }
    KJ_IF_MAYBE(value, ::sd_bus_message_get_interface(msg)) {
      builder.setIface(value);
    }
    KJ_IF_MAYBE(value, ::sd_bus_message_get_member(msg)) {
      builder.setMember(value);
    }
    {
      auto orphanage = capnp::Orphanage::getForMessageContaining(builder);
      auto fields = _::extract(msg, orphanage);
      builder.adoptFields(kj::mv(fields));
    }
  }

}
}
