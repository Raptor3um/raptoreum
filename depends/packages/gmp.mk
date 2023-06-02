package=gmp
$(package)_version=6.2.1
$(package)_download_path=https://gmplib.org/download/gmp
$(package)_file_name=$(package)-$($(package)_version).tar.bz2
$(package)_sha256_hash=eae9326beb4158c386e39a356818031bd28f3124cf915f8c5b1dc4c7a36b4d7c

define $(package)_set_vars
#$(package)_config_opts=--disable-cxx --enable-fat --with-pic --enable-shared=no --enable-static=yes
ifeq ($(build_os),linux)
$(package)_config_opts_mingw32=--build=x86_64-linux-gnu --host=$(host) --prefix=$(host_prefix) ABI=64
$(package)_config_opts_mingw32 += CC=$(host)-gcc-posix CC_FOR_BUILD=gcc CFLAGS="-pipe -O3"
$(package)_config_opts_android=--build=x86_64-linux-gnu --host=$(host) --prefix=$(host_prefix) ABI=64
#$(package)_config_opts_darwin=--build=x86_64-linux-gnu --host=$(host) --prefix=$(host_prefix) ABI=64
endif
#$(package)_config_opts += --build=x86_64-linux-gnu --host=x86_64-linux-gnu --prefix=$(host_prefix) ABI=64 CFLAGS="-pipe -03"
$(package)_config_opts+=--enable-cxx --enable-fat --with-pic --disable-shared
$(package)_cflags_armv7l_linux=-march=armv7-a
$(package)_cflags_aarch64_darwin=-march=armv8-a
endef

define $(package)_config_cmds
  $($(package)_autoconf)
#  ./configure $($(package)_config_opts)
endef

define $(package)_build_cmds
  $(MAKE)
endef

define $(package)_stage_cmds
  $(MAKE) DESTDIR=$($(package)_staging_dir) install
endef

