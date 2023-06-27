// Copyright (c) 2023 Vaci Koblizek.
// Licensed under the Apache 2.0 license found in the LICENSE file or at:
//     https://opensource.org/licenses/Apache-2.0

#include "dbus.h"

#include <kj/async-io.h>
#include <kj/debug.h>
#include <kj/main.h>

#include <gtest/gtest.h>

using namespace dbus;

struct ConfigTest
  : testing::Test {

  ConfigTest() {
  }

  ~ConfigTest() noexcept {
  }

  kj::AsyncIoContext ioCtx_{kj::setupAsyncIo()};
  kj::WaitScope& waitScope_{ioCtx_.waitScope};
  kj::Network& network_{ioCtx_.provider->getNetwork()};
};

TEST_F(ConfigTest, Basic) {

  auto dbus = newDbus(ioCtx_.unixEventPort, ioCtx_.provider->getTimer());
  auto req = dbus.systemRequest();
  auto bus = req.send().getBus();
  {
    auto req = bus.callRequest();
    req.setDestination("org.freedesktop.systemd1");
    req.setPath("/org/freedesktop/systemd1");
    req.setIface("org.freedesktop.systemd1.Manager");
    req.setMember("ListUnits");

    //{
    //  auto fields = req.initFields(1);
    //  fields[0].setUint32(getpid());
    // }
    
    auto reply = req.send().wait(waitScope_);
    KJ_LOG(INFO, reply);
  }
}

int main(int argc, char* argv[]) {
  kj::TopLevelProcessContext processCtx{argv[0]};
  processCtx.increaseLoggingVerbosity();

  ::testing::InitGoogleTest(&argc, argv);
  return RUN_ALL_TESTS();
}
