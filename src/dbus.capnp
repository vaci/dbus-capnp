# Copyright (c) 2023 Vaci Koblizek.
# Licensed under the Apache 2.0 license found in the LICENSE file or at:
#     https://opensource.org/licenses/Apache-2.0

@0xbda5af28df39adf8;

$import "/capnp/c++.capnp".namespace("dbus");

struct KeyValue {
  key @0 :Text;
  value @1 :Field;
}

struct Field {
  union {
    byte @0 :UInt8;
    bool @1 :Bool;
    int16 @2 :Int16;
    uint16 @3 :UInt16;
    int32 @4 :Int32;
    uint32 @5 :UInt32;
    int64 @6 :Int64;
    uint64 @7 :UInt64;
    double @8 :Float64;
    string @9 :Text;
    objectPath @10 :Text;
    signature @11 :Text;
    array @12 :List(Field);
    unix @13 :Capability;
    structure @14 :List(Field);
    dictionary @15 :List(KeyValue);
    #objectPath @8 :Text;
    #structure @79 :List(Field);
  }
}

struct Message {
  destination @0 :Text;
  path @1 :Text;
  iface @2 :Text;
  member @3 :Text;
  fields @4 :List(Field);
}

interface Unix {
}

interface Bus {
  call @0 Message -> Message;
}

interface Dbus {
  user @0 (description :Text) -> (bus :Bus);
  system @1 (description :Text) -> (bus :Bus);
}

interface Manager {
  getUnit @0 (name :Text) -> (unit :Text);
}
