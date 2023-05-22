// Copyright (c) 2023 Vaci Koblizek.
// Licensed under the Apache 2.0 license found in the LICENSE file or at:
//     https://opensource.org/licenses/Apache-2.0

#include "message.h"

#include <kj/debug.h>

#include <fcntl.h>

namespace dbus {

  namespace {
    struct UnixServer
      : Unix::Server {

      UnixServer(int fd)
	: fd_{fd} {
      }

      ~UnixServer() {
	::close(fd_);
      }

      kj::Maybe<int> getFd() override {
	return fd_;
      }

      int fd_;
    };
  }

  capnp::Orphan<capnp::List<Field>> extract(sd_bus_message* msg, capnp::Orphanage orphanage) {
    auto fields = orphanage.newOrphan<capnp::List<Field>>(0);
    int count = 0;
    while (true) {
      char type;
      const char* contents;
      int err = ::sd_bus_message_peek_type(msg, &type, &contents);
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
	KJ_SYSCALL(::fcntl(value, F_DUPFD_CLOEXEC, 3), "dup");
	field.setUnix(kj::heap<UnixServer>(fd));
	break;
      }
      case 'r':
      case '(': {
	::sd_bus_message_enter_container(msg, type, contents);
	auto content = extract(msg, orphanage);
	field.adoptStructure(kj::mv(content));
	::sd_bus_message_exit_container(msg);
	break;
      }
      case '{': {
	::sd_bus_message_enter_container(msg, type, contents);
	auto content = extractDictionary(msg, orphanage);
	field.adoptDictionary(kj::mv(content));
	::sd_bus_message_exit_container(msg);
	break;
      }
      default:
	KJ_LOG(INFO, "Skipping ", type, contents);
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
}

