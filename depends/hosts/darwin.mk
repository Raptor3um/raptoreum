OSX_MIN_VERSION=13.0
XCODE_VERSION=15.0
XCODE_BUILD_ID=15A240d
LLD_VERSION=711

# Defaults for the SDK extracted from Xcode $(XCODE_VERSION), as described in
# contrib/macdeploy/README.md. Both can be overridden to build against a
# different SDK, which is what the CI job does with the one it takes off a
# macOS runner.
OSX_SDK_VERSION ?= 14.0
OSX_SDK ?= $(SDK_PATH)/Xcode-$(XCODE_VERSION)-$(XCODE_BUILD_ID)-extracted-SDK-with-libcxx-headers

# We can't just use $(shell command -v clang) because GNU Make handles builtins
# in a special way and doesn't know that `command` is a POSIX-standard builtin
# prior to 1af314465e5dfe3e8baa839a32a72e83c04f26ef, first released in v4.2.90.
# At the time of writing, GNU Make v4.2.1 is still being used in supported
# distro releases.
#
# Source: https://lists.gnu.org/archive/html/bug-make/2017-11/msg00017.html
clang_prog=$(shell $(SHELL) $(.SHELLFLAGS) "command -v clang")
clangxx_prog=$(shell $(SHELL) $(.SHELLFLAGS) "command -v clang++")

darwin_AR=$(shell $(SHELL) $(.SHELLFLAGS) "command -v llvm-ar")
darwin_DSYMUTIL=$(shell $(SHELL) $(.SHELLFLAGS) "command -v dsymutil")
darwin_LD=$(shell $(SHELL) $(.SHELLFLAGS) "command -v ld64.lld")
darwin_LIBTOOL=$(shell $(SHELL) $(.SHELLFLAGS) "command -v llvm-libtool-darwin")
darwin_NM=$(shell $(SHELL) $(.SHELLFLAGS) "command -v llvm-nm")
darwin_OBJCOPY=$(shell $(SHELL) $(.SHELLFLAGS) "command -v llvm-objcopy")
darwin_OBJDUMP=$(shell $(SHELL) $(.SHELLFLAGS) "command -v llvm-objdump")
darwin_RANLIB=$(shell $(SHELL) $(.SHELLFLAGS) "command -v llvm-ranlib")
darwin_STRIP=$(shell $(SHELL) $(.SHELLFLAGS) "command -v llvm-strip")

# Flag explanations:
#
#     -mlinker-version
#
#         Ensures that modern linker features are enabled. See here for more
#         details: https://github.com/bitcoin/bitcoin/pull/19407.
#
#     -isysroot$(OSX_SDK) -nostdlibinc
#
#         Disable default include paths built into the compiler as well as
#         those normally included for libc and libc++. The only path that
#         remains implicitly is the clang resource dir.
#
#     -iwithsysroot / -iframeworkwithsysroot
#
#         Adds the desired paths from the SDK
#
#     -platform_version
#
#         Indicate to the linker the platform, the oldest supported version,
#         and the SDK used.
#
#     adhoc codesigning
#
#         Left enabled, which is lld's default: arm64 macOS refuses to execute
#         a binary that carries no signature at all, so a cross-compiled build
#         that suppressed it would produce binaries nobody can run. This is the
#         same signature a native build gets from Apple's linker. Suppressing it
#         (-Wl,-no_adhoc_codesign) is only worthwhile for deterministic builds,
#         where the Identifier field is a source of non-determinism.

darwin_CC=$(clang_prog) --target=$(host) \
              -isysroot$(OSX_SDK) -nostdlibinc \
              -iwithsysroot/usr/include -iframeworkwithsysroot/System/Library/Frameworks

darwin_CXX=$(clangxx_prog) --target=$(host) \
               -isysroot$(OSX_SDK) -nostdlibinc \
               -iwithsysroot/usr/include/c++/v1 \
               -iwithsysroot/usr/include -iframeworkwithsysroot/System/Library/Frameworks

darwin_CFLAGS=-pipe -std=$(C_STANDARD) -mmacos-version-min=$(OSX_MIN_VERSION)
darwin_CXXFLAGS=-pipe -std=$(CXX_STANDARD) -mmacos-version-min=$(OSX_MIN_VERSION)
darwin_LDFLAGS=-Wl,-platform_version,macos,$(OSX_MIN_VERSION),$(OSX_SDK_VERSION)

ifneq ($(build_os),darwin)
# -fuse-ld=lld goes on the compiler itself, not just on the linker flags.
# Anything less leaves clang free to fall back to the build machine's GNU ld,
# which cannot produce Mach-O and fails with "unrecognised emulation mode:
# llvm". Two common cases reach the linker without CFLAGS or LDFLAGS anywhere
# in sight:
#
#   - a configure script linking its test program with just $CC (gmp does this,
#     and concludes "could not find a working compiler");
#   - libtool's own recipe for a darwin shared library, whose first step is
#     `$CC -r -keep_private_externs -nostdlib -o $lib-master.o $libobjs`.
darwin_CC += -fuse-ld=lld
darwin_CXX += -fuse-ld=lld
darwin_CFLAGS += -mlinker-version=$(LLD_VERSION)
darwin_CXXFLAGS += -mlinker-version=$(LLD_VERSION)
darwin_LDFLAGS += -fuse-ld=lld
endif

darwin_release_CFLAGS=-O2
darwin_release_CXXFLAGS=$(darwin_release_CFLAGS)

darwin_debug_CFLAGS=-O1 -g
darwin_debug_CXXFLAGS=$(darwin_debug_CFLAGS)

darwin_cmake_system_name=Darwin
# Darwin version, which corresponds to OSX_MIN_VERSION.
# See https://en.wikipedia.org/wiki/Darwin_(operating_system)
darwin_cmake_system_version=20.1
