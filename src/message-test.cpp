// Copyright (c) 2023 Vaci Koblizek.
// Licensed under the Apache 2.0 license found in the LICENSE file or at:
//     https://opensource.org/licenses/Apache-2.0

#include "message.h"

#include <capnp/message.h>

#include <kj/async-io.h>
#include <kj/debug.h>
#include <kj/main.h>

#include <gtest/gtest.h>

using namespace dbus;

struct MsgTest
  : testing::Test {

  MsgTest() {
    //KJ_REQUIRE(::sd_bus_new(&bus_) >= 0);
    KJ_REQUIRE(::sd_bus_open_system(&bus_) >= 0);
    KJ_REQUIRE(::sd_bus_message_new_method_call(
	  bus_, &msg_,
	  "org.freedesktop.systemd1",
	  "/org/freedesktop/systemd1",
	  "org.freedesktop.systemd1.Manager", "ListUnits") >= 0);
  }

  ~MsgTest() noexcept {
    ::sd_bus_message_unref(msg_);
    ::sd_bus_unref(bus_);
  }

  kj::AsyncIoContext ioCtx_{kj::setupAsyncIo()};
  kj::WaitScope& waitScope_{ioCtx_.waitScope};
  kj::Network& network_{ioCtx_.provider->getNetwork()};
  ::sd_bus* bus_;
  ::sd_bus_message* msg_;
};

#ifdef XOUT
TEST_F(MsgTest, Basic2) {

  EXPECT_GE(::sd_bus_message_append(msg_, "s", "a string"), 0);

  ::sd_bus_message_rewind(msg_, 0);
  capnp::MallocMessageBuilder mb;
  auto builder = mb.initRoot<Message>();
  _::build(builder, msg_);
}

TEST_F(MsgTest, Basic) {

  EXPECT_GE(0, ::sd_bus_message_append(msg_, "s", "a string"));
  
  uint8_t y = 1;
  int16_t n = 2;
  uint16_t q = 3;
  int32_t i = 4;
  uint32_t u = 5;
  int32_t x = 6;
  uint32_t t = 7;
  double d = 8.0;
  EXPECT_GE(::sd_bus_message_append(msg_, "ynqiuxtd", y, n, q, i, u, x, t, d), 0);
  //::sd_bus_message_seal(msg_, 0, 0);

  ::sd_bus_message_rewind(msg_, 0);
  capnp::MallocMessageBuilder mb;
  auto builder = mb.initRoot<Message>();
  _::build(builder, msg_);
}

#endif
int main(int argc, char* argv[]) {
  kj::TopLevelProcessContext processCtx{argv[0]};
  processCtx.increaseLoggingVerbosity();

  ::testing::InitGoogleTest(&argc, argv);
  return RUN_ALL_TESTS();
}
