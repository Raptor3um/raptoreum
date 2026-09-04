package=evmone
$(package)_version=0.12.0
$(package)_download_path=https://github.com/ethereum/evmone/archive/refs/tags
$(package)_download_file=v$($(package)_version).tar.gz
$(package)_file_name=$(package)-$($(package)_version).tar.gz
$(package)_sha256_hash=5dddc1fbb816b951ef3c5b5065830814c19f70064d0dc8ab27b1bb2b1012dbea
$(package)_build_subdir=build_tmp
$(package)_dependencies=cmake

# ============================================================================
# PHASE 0 KNOWN ISSUE — evmone NOT currently built via this .mk
# ============================================================================
#
# evmone v0.12.0+ has two dependency-fetch mechanisms:
#   1. Git submodules for evmc and evm-benchmarks.
#   2. Hunter package manager (https://hunter.readthedocs.io/) for intx, GTest, etc.
#
# Two problems with depends/ integration:
#
# Problem 1 — Missing submodules in source tarball.
#   The GitHub source tarball does NOT include submodules. cmake configure fails with:
#       "Git submodules not initialized, execute: git submodule update --init"
#   Workaround: clone with --recurse-submodules instead of fetching tarball.
#
# Problem 2 — depends-cmake lacks HTTPS support.
#   depends/packages/cmake.mk builds cmake with -DCMAKE_USE_OPENSSL=OFF.
#   Hunter downloads packages over HTTPS via cmake's file(DOWNLOAD ...). Fails with:
#       "Protocol \"https\" not supported or disabled in libcurl"
#   Workaround: build evmone with the SYSTEM cmake (which has HTTPS),
#   not the depends-built cmake.
#
# Phase 0 spike workaround (manual install — see docs/evm/PHASE-0-HANDOFF.md):
#   1. After `make -C depends` succeeds (without evmone),
#      git clone --branch v0.12.0 --recurse-submodules https://github.com/ethereum/evmone
#   2. Build with system cmake, install to $PREFIX/depends/$HOST/:
#      /usr/bin/cmake -B build -DCMAKE_INSTALL_PREFIX=$PREFIX -DBUILD_SHARED_LIBS=OFF \
#                     -DEVMONE_TESTING=OFF -DEVMONE_FUZZING=OFF
#      make -C build install
#   3. Manually copy evmc headers (not installed by evmone's install target):
#      cp -r evmone-src/evmc/include/evmc $PREFIX/include/
#
# Phase 1 cleanup plan (TODOs CQ3) — two options for proper depends/ integration:
#
#   Option A: patch cmake.mk to enable HTTPS
#     Modify depends/packages/cmake.mk: add `-DCMAKE_USE_OPENSSL=ON` and depend on openssl.
#     Then Hunter can download intx/gtest/etc. on its own.
#     Pro: minimal change.
#     Con: each fresh depends/ build re-downloads Hunter packages (no caching benefit).
#
#   Option B: vendor evmc, intx, ethash, evm-benchmarks as separate depends packages,
#     and patch evmone's CMakeLists.txt to disable Hunter entirely (use find_package).
#     Pro: fully reproducible offline builds, matches Bitcoin Core's depends philosophy,
#          enables Guix integration (CQ3 in TODOs).
#     Con: more code, more pinning to maintain.
#     RECOMMENDED for production.
#
# ============================================================================

define $(package)_fetch_cmds
$(call fetch_file,$(package),$($(package)_download_path),$($(package)_download_file),$($(package)_file_name),$($(package)_sha256_hash))
endef

define $(package)_set_vars
  $(package)_config_opts=-DCMAKE_BUILD_TYPE=Release
  $(package)_config_opts+=-DCMAKE_INSTALL_PREFIX=$(host_prefix)
  $(package)_config_opts+=-DCMAKE_INSTALL_INCLUDEDIR=$(host_prefix)/include
  $(package)_config_opts+=-DCMAKE_INSTALL_LIBDIR=$(host_prefix)/lib
  $(package)_config_opts+=-DBUILD_SHARED_LIBS=OFF
  $(package)_config_opts+=-DEVMONE_TESTING=OFF
  $(package)_config_opts+=-DEVMONE_FUZZING=OFF
  $(package)_config_opts_mingw32=-DCMAKE_SHARED_LIBRARY_LINK_C_FLAGS=""
endef

define $(package)_config_cmds
  export CC="$($(package)_cc)" && \
  export CXX="$($(package)_cxx)" && \
  export CFLAGS="$($(package)_cflags) $($(package)_cppflags)" && \
  export CXXFLAGS="$($(package)_cxxflags) $($(package)_cppflags)" && \
  export LDFLAGS="$($(package)_ldflags)" && \
  $(host_prefix)/bin/cmake ../ $($(package)_config_opts)
endef

define $(package)_build_cmds
  $(MAKE)
endef

define $(package)_stage_cmds
  $(MAKE) DESTDIR=$($(package)_staging_dir) install
endef
