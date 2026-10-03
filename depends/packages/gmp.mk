package=gmp
$(package)_version=6.2.1
$(package)_download_path=https://ftp.gnu.org/gnu/gmp
$(package)_file_name=gmp-$($(package)_version).tar.bz2
$(package)_sha256_hash=eae9326beb4158c386e39a356818031bd28f3124cf915f8c5b1dc4c7a36b4d7c
$(package)_patches=applem1.patch

define $(package)_set_vars
# GCC 15 defaults to -std=gnu23, where an empty parameter list means "(void)".
# GMP 6.2.1's configure probes call a `void g(){}` helper with arguments, which
# is a hard error under C23, so configure aborts with "could not find a working
# compiler". Pin the C dialect until GMP is bumped to a release that fixes this.
$(package)_cflags+=-std=gnu17
$(package)_cxxflags+=-std=c++17
$(package)_config_opts+=--enable-cxx --enable-fat --with-pic --disable-shared
$(package)_cflags_armv7l_linux+=-march=armv7-a
$(package)_config_opts_arm_darwin+=--build=$(subst arm,aarch64,$(BUILD)) --host=$(subst arm,aarch64,$(HOST))
endef

define $(package)_config_cmds
  $($(package)_autoconf)
endef

define $(package)_preprocess_cmds
  patch -p1 <$($(package)_patch_dir)/applem1.patch
endef

define $(package)_build_cmds
  $(MAKE)
endef

define $(package)_stage_cmds
  $(MAKE) DESTDIR=$($(package)_staging_dir) install
endef
