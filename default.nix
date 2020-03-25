with import <nixpkgs> { };
with stdenv.lib;
let libiscsi = callPackage ./nix/libiscsi.nix {};
in stdenv.mkDerivation rec {

  enableDebug = true;
  name = "libspdk";
  src = lib.cleanSource ./.;

  buildInputs = [
    binutils
    libaio
    libiscsi.dev
    libuuid
    liburing
    nasm
    numactl
    openssl
    python
    rdma-core
  ] ++ stdenv.lib.optionals enableDebug [ cunit lcov ];

  #${enableFeature enableDebug "unit-tests"}

  configureFlags = [
    "${enableFeature enableDebug "debug"}"
    "${enableFeature enableDebug "tests"}"
    "--without-isal"
    "--with-iscsi-initiator"
    "--with-internal-vhost-lib"
    "--with-crypto"
    "--with-uring"
    "--with-rdma"
  ];

  enableParallelBuilding = true;

  preConfigure = ''
    patchShebangs ./.
    substituteInPlace dpdk/config/defconfig_x86_64-native-linux-gcc --replace native default
    # A workaround for https://bugs.dpdk.org/show_bug.cgi?id=356
    substituteInPlace dpdk/lib/Makefile --replace 'DEPDIRS-librte_vhost :=' 'DEPDIRS-librte_vhost := librte_hash'
  '';

  NIX_CFLAGS_COMPILE = "-mno-movbe -mno-lzcnt -mno-bmi -mno-bmi2 -march=corei7";
  hardeningDisable = [ "all" ];

  postBuild = ''

    # see README in spdk-sys why this needs to be done
    find . -type f -name 'libspdk_ut_mock.a' -delete
    find . -type f -name 'librte_vhost.a' -delete

    $CC -shared -o libspdk_fat.so \
    -lc -lrdmacm -laio -libverbs -liscsi -lnuma -ldl -lrt -luuid -lpthread -lcrypto -luring \
    -Wl,--whole-archive \
    $(find build/lib -type f -name 'libspdk_*.a*' -o -name 'librte_*.a*') \
    $(find dpdk/build/lib -type f -name 'librte_*.a*') \
    $(find intel-ipsec-mb -type f -name 'libIPSec_*.a*') \
    -Wl,--no-whole-archive
  '';

  # todo -- split out in dev and normal pkg
  postInstall = ''
    mkdir $out/include/spdk/spdk
    mv $out/include/spdk/*.h $out/include/spdk/spdk

    pushd include
    find . -type f -name "*.h" -exec install -D "{}" $out/include/spdk/{} \;
    popd

    pushd lib
    find . -type f -name "*.h" -exec install -D "{}" $out/include/spdk/{} \;
    popd

    # copy private headers from bdev modules needed for creating of bdevs
    pushd module
    find . -type f -name "*.h" -exec install -D "{}" $out/include/spdk/{} \;
    popd

    # copy over the library
    cp libspdk_fat.so $out/lib

    if [ $enableDebug ]
    then
      cp -ar test $out/test
    fi
  '';
  separateDebugInfo = !enableDebug;
}
