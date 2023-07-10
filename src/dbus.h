#pragma once
// Copyright (c) 2023 Vaci Koblizek.
// Licensed under the Apache 2.0 license found in the LICENSE file or at:
//     https://opensource.org/licenses/Apache-2.0

#include "dbus.capnp.h"

namespace kj {
  struct Timer;
  struct UnixEventPort;
};

namespace dbus {
  Dbus::Client newDbus(kj::UnixEventPort&, kj::Timer&);
}
