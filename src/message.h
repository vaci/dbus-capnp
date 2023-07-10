#pragma once
// Copyright (c) 2023 Vaci Koblizek.
// Licensed under the Apache 2.0 license found in the LICENSE file or at:
//     https://opensource.org/licenses/Apache-2.0

#include "dbus.capnp.h"
#include <capnp/dynamic.h>
#include <capnp/orphan.h>

struct sd_bus;
struct sd_bus_message;

namespace dbus {

namespace _ {

  capnp::Orphan<capnp::List<Field>> extract(sd_bus_message* msg, capnp::Orphanage orphanage);
  capnp::Orphan<capnp::List<KeyValue>> extractDictionary(sd_bus_message* msg, capnp::Orphanage orphanage);
  capnp::Orphan<capnp::List<Field>> extractArray(sd_bus_message* msg, capnp::Orphanage orphanage);

  kj::Promise<::sd_bus_message*> call(::sd_bus*, ::sd_bus_message*);
  kj::Promise<void> append(sd_bus_message*, Field::Reader);

  void build(Message::Builder, ::sd_bus_message*);

  kj::Exception err(::sd_bus_message* msg);

  void setFields(
    Message::Builder builder,
    capnp::DynamicStruct::Reader params);

  void buildField(
    Field::Builder builder,
    capnp::DynamicValue::Reader reader, capnp::Type type);
}
}
