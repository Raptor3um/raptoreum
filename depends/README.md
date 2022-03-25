### Usage

To build dependencies for the current arch+OS:

    make

To build for another arch/OS:

    make HOST=host-platform-triplet

For example:

    make HOST=x86_64-w64-mingw32 -j4

A prefix will be generated that's suitable for plugging into Raptoreum's
configure. In the above example, a dir named x86_64-w64-mingw32 will be
created. To use it for Raptoreum:

    ./configure --prefix=`pwd`/depends/x86_64-w64-mingw32

Common `host-platform-triplets` for cross compilation are:

- `x86_64-pc-linux-gnu` for x86 Linux
- `x86_64-w64-mingw32` for Win64
- `x86_64-apple-darwin` for macOS
- `arm64-apple-darwin` for ARM macOS <-> Apple Silicon M1 Family CPU's
- `arm-linux-gnueabihf` for Linux ARM 32 bit
- `aarch64-linux-gnu` for Linux ARM 64 bit
- `riscv32-linux-gnu` for Linux RISC-V 32 bit
- `riscv64-linux-gnu` for Linux RISC-V 64 bit

No other options are needed, the paths are automatically configured.

Install the required dependencies: Ubuntu & Debian
--------------------------------------------------

For macOS cross compilation:

    sudo apt-get install cmake curl librsvg2-bin libtiff-tools bsdmainutils imagemagick libcap-dev libz-dev libbz2-dev python3-setuptools libtinfo5 xorriso

For Win64 cross compilation:

- see [build-windows.md](../doc/build-windows.md#cross-compilation-for-ubuntu-and-windows-subsystem-for-linux)

For linux (including i386, ARM) cross compilation:

    sudo apt-get install make automake cmake curl g++-multilib libtool binutils-gold bsdmainutils pkg-config python3 patch bison

For linux ARM cross compilation

	sudo apt-get install g++-arm-linux-gnueabihf binutils-arm-linux-gnueabihf

For linux AARCH64 cross compilation

	sudo apt-get install g++-aarch64-linux-gnu binutils-aarch64-linux-gnu

Dependency Options:

The following can be set when running make: `make FOO=bar`

    - `SOURCES_PATH`: downloaded sources will be placed here
    - `BASE_CACHE`: built packages will be placed here
    - `SDK_PATH`: Path where sdk's can be found (used by OSX)
    - `FALLBACK_DOWNLOAD_PATH`: If a source file can't be fetched, try here before giving up
    - `NO_QT`: Don't download/build/cache qt and its dependencies
    - `NO_QR`: Don't download/build/cache packages needed for enabling qrencode
    - `NO_ZMQ`: Don't download/build/cache packages needed for enabling ZeroMQ
    - `NO_WALLET`: Don't download/build/cache libs needed to enable the wallet
    - `NO_BDB`: Don't download/build/cache BerkeleyDB
    - `NO_UPNP`: Don't download/build/cache packages needed for enabling upnp
    - `NO_NATPMP`: Don't download/build/cache packages needed for enabling NAT-PMP</dd>
    - `ALLOW_HOST_PACKAGES`: Packages that are missed in dependencies (due to `NO_*` option
       or build script logic) are searched for among the host system packaging using
      `pkg-config`. It allows building with packages of other (newer) versions.
    - `MULTIPROCESS`: build libmultiprocess (experimental, require cmake)
    - `DEBUG`: disable some optimizations and enable more runtime checking
    - `HOST_ID_SALT`: Optional salt to use when generating host package ids
    - `BUILD_ID_SALT`: Optional salt to use when generating build package ids
    - `FORCE_USE_SYSTEM_CLANG`: (EXPERTS_ONLY!!!) When cross-compiling for macOS,
       use Clang found in the system's `$PATH` rather than the default prebuilt
       release of Clang from llvm.org. Clang 8 or later is required.

If some packages are not built, for example `make NO_WALLET=1`, the appropriate
options will be passed to Raptoreum Core's configure. In this case, `--disable-wallet`.

Additional targets:

    download: run 'make download' to fetch all sources without building them
    download-osx: run 'make download-osx' to fetch all sources needed for osx builds
    download-win: run 'make download-win' to fetch all sources needed for win builds
    download-linux: run 'make download-linux' to fetch all sources needed for linux builds

### Other documentation

- [description.md](description.md): General description of the depends system
- [packages.md](packages.md): Steps for adding packages

