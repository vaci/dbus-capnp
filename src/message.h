// Copyright (c) 2023 Vaci Koblizek.
// Licensed under the Apache 2.0 license found in the LICENSE file or at:
//     https://opensource.org/licenses/Apache-2.0

#include "dbus.capnp.h"
#include <capnp/orphan.h>

#include <systemd/sd-bus.h>

namespace dbus {
  capnp::Orphan<capnp::List<Field>> extract(sd_bus_message* msg, capnp::Orphanage orphanage);
  capnp::Orphan<capnp::List<KeyValue>> extractDictionary(sd_bus_message* msg, capnp::Orphanage orphanage);
  capnp::Orphan<capnp::List<Field>> extractArray(sd_bus_message* msg, capnp::Orphanage orphanage);
}
