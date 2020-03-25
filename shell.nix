with import <nixpkgs> { };

mkShell {

  buildInputs = [
    autoconf
    automake
    binutils
    cunit
    libaio
    libiscsi.dev
    liburing.dev
    libuuid
    nasm
    numactl
    openssl
    python
    rdma-core
    lcov
  ];
}
