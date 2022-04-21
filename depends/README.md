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

    ./configure --prefix=$PWD/depends/x86_64-w64-mingw32

Common `host-platform-triplets` for cross compilation are:

- `x86_64-pc-linux-gnu` for x86 Linux
- `x86_64-w64-mingw32` for Win64
- `x86_64-apple-darwin` for macOS
- `arm64-apple-darwin` for ARM macOS <-> Apple Silicon M1 Family CPU's
- `arm-linux-gnueabihf` for Linux ARM 32 bit
- `aarch64-linux-gnu` for Linux ARM 64 bit
- `armv7a-linux-android` for Android ARM 32 bit
- `aarch64-linux-android` for Android ARM 64 bit
- `x86_64-linux-android` for Android x86 64 bit

The paths are automatically configured and no other options are needed unless targeting [Android](../doc/build-android.md).

### Install the required dependencies: Ubuntu & Debian


#### For macOS cross compilation:

    sudo apt-get install curl bsdmainutils cmake libz-dev libbz2-dev python3-setuptools libtinfo5 xorriso

Note: You must obtain the macOS SDK before proceeding with a cross-compile.
Under the depends directory, create a subdirectory `SDKs`.
Then, place the extracted SDK under this new directory.
For more information, see [SDK Extraction](../contrib/macdeploy/README.md#sdk-extraction).

#### For Win64 cross compilation:

- see [build-windows.md](../doc/build-windows.md#cross-compilation-for-ubuntu-and-windows-subsystem-for-linux)

#### For linux (including i386, ARM) cross compilation:

    sudo apt-get install make automake cmake curl g++-multilib libtool binutils-gold bsdmainutils pkg-config python3 patch bison

For linux ARM cross compilation

	sudo apt-get install g++-arm-linux-gnueabihf binutils-arm-linux-gnueabihf

For linux AARCH64 cross compilation

	sudo apt-get install g++-aarch64-linux-gnu binutils-aarch64-linux-gnu

### Dependency Options:

The following can be set when running make: `make FOO=bar`

- `SOURCES_PATH`: Downloaded sources will be placed here
- `BASE_CACHE`: Built packages will be placed here
- `SDK_PATH`: Path where sdk's can be found (used by OSX)
- `FALLBACK_DOWNLOAD_PATH`: If a source file can't be fetched, try here before giving up
- `NO_QT`: Don't download/build/cache qt and its dependencies
- `NO_QR`: Don't download/build/cache libs supporting QR Code reading
- `NO_PROTOBUF`: build protobuf (used for BIP70 support)
- `NO_WALLET`: Don't download/build/cache libs needed to enable the wallet
- `NO_BDB`: Don't download/build/cache BerkeleyDB
- `NO_UPNP`: Don't download/build/cache packages needed for enabling upnp
- `NO_NATPMP`: Don't download/build/cache packages needed for enabling NAT-PMP
- `ALLOW_HOST_PACKAGES`: Packages that are missed in dependencies (due to <code>NO_*</code> option
   or build script logic) are searched for among the host system packaging
   using <code>pkg-config</code> it allows building with packages of other (newer) versions.
- `DEBUG`: disable some optimizations and enable more runtime checking
- `HOST_ID_SALT`: Optional salt to use when generating host package ids
- `BUILD_ID_SALT`: Optional salt to use when generating build package ids
- `FORCE_USE_SYSTEM_CLANG`: (EXPERTS_ONLY!!!) When cross-compiling for macOS,
   use Clang found in the system's <code>$PATH</code> rather than the default prebuilt
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

