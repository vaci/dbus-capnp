{
  pkgs ? import <nixpkgs> {}
, debug ? true
}:

let
  name = "sdcap";

  create-ekam-rules-link = ''
    ln --symbolic --force --target-directory=src \
      "${pkgs.ekam.src}/src/ekam/rules"

    mkdir --parents src/compiler
    ln --symbolic --force "${pkgs.capnproto}/bin/capnp" src/compiler/capnp
    ln --symbolic --force "${pkgs.capnproto}/bin/capnpc-c++" src/compiler/capnpc-c++
  '';

in

pkgs.stdenv.mkDerivation {

  inherit name;
  src = ./.;

  buildInputs = with pkgs; [
    capnproto
    dbus
    systemd
    zlib
  ];

  nativeBuildInputs = with pkgs; [
    ccache
    clang-tools
    ekam
    gtest
    which
  ];

  propagatedBuildInputs = with pkgs; [
  ];

  CAPNPC_FLAGS = with pkgs; [
    "-I${capnproto}/include"
  ];


  shellHook = create-ekam-rules-link;

  buildPhase = ''
    ${create-ekam-rules-link}
    make ${if debug then "debug" else "release"}
  '';

  # Pointless for static libraries, but uncomment if we ever move to a shared
  # object
  #separateDebugInfo = true;

  installPhase = ''
    install --verbose -D --mode=644 \
      --target-directory="''${!outputLib}/lib" \
      lib${name}.a

    install --verbose -D --mode=644 \
      --target-directory="''${!outputInclude}/include/${name}" \
      src/*.capnp \
      src/*.capnp.h \
      tmp/*.capnp.h \
      src/*.h 
  '';
}
