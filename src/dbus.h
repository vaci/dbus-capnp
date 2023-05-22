#include "dbus.capnp.h"

namespace kj {
  struct Timer;
};

namespace dbus {
  Dbus::Client newDbus(kj::Timer&);
}
