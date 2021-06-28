{
  description = "Fork of libspdk with mayastor specific patches";

  inputs = {
    nixpkgs.url = "github:NixOS/nixpkgs/nixpkgs-unstable";
    spdk = {
      type = "github";
      owner = "openebs";
      repo = "spdk";
      ref = "flake";
      flake = true;
      submodules = true;
    };
  };

  outputs = { self, nixpkgs }: { 
    defaultPackage.x86_64-linux =
      with import nixpkgs { system = "x86_64-linux"; };
      stdenv.mkDerivation {
        root = ./.;
        name = "libspdk";
        src = self;
        nativeBuildInputs = [
          meson
          ninja
          pkg-config
          python3
          llvmPackages_11.clang
          gcc
          cmake
        ];
        buildInputs = [
          fio
          jansson
          libaio
          libbpf
          libbsd
          libelf
          libexecinfo
          libiscsi
          libpcap
          libtool
          liburing
          libuuid
          nasm
          ncurses
          numactl
          openssl
          zlib
          binutils
        ];

        configurePhase = ''
          patchShebangs ./. > /dev/null
          ./configure
        '';

        buildPhase = ''
          make -j`nproc`
          find . -type f -name 'libspdk_event_nvmf.a' -delete
          find . -type f -name 'libspdk_sock_uring.a' -delete
          find . -type f -name 'libspdk_ut_mock.a' -delete

          $CC -shared -o libspdk.so \
          -lc  -laio -liscsi -lnuma -ldl -lrt -luuid -lpthread -lcrypto \
          -luring \
          -Wl,--whole-archive \
          $(find build/lib -type f -name 'libspdk_*.a*' -o -name 'librte_*.a*') \
          $(find dpdk/build/lib -type f -name 'librte_*.a*') \
          $(find intel-ipsec-mb -type f -name 'libIPSec_*.a*') \
          -Wl,--no-whole-archive
        '';

        installPhase = ''
          mkdir -p $out/lib
          mkdir $out/bin
          mkdir $out/fio

          pushd include
          find . -type f -name "*.h" -exec install -D "{}" $out/include/{} \;
          popd

          pushd lib
          find . -type f -name "*.h" -exec install -D "{}" $out/include/spdk/lib/{} \;
          popd

      # copy private headers from bdev modules needed for creating of bdevs
          pushd module
          find . -type f -name "*.h" -exec install -D "{}" $out/include/spdk/module/{} \;
          popd

      # copy over the library
          cp libspdk.so $out/lib

          echo $(find $out -type f -name '*.a*' -delete)
          find . -executable -type f -name 'bdevperf' -exec install -D "{}" $out/bin \;

          cp build/fio/spdk_* $out/fio
        '';
      };
  };
}
