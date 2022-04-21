package=native_protobuf
$(package)_version=3.17.3
$(package)_download_path=https://github.com/google/protobuf/releases/download/v$($(package)_version)
$(package)_file_name=protobuf-cpp-$($(package)_version).tar.gz
$(package)_sha256_hash=51cec99f108b83422b7af1170afd7aeb2dd77d2bcbb7b6bad1f92509e9ccf8cb

define $(package)_set_vars
  $(package)_config_opts=--prefix=$(build_prefix) --disable-shared --without-zlib
  $(package)_config_opts_linux=--with-pic
  $(package)_cxxflags=-std=c++17
endef

define $(package)_config_cmds
  $($(package)_autoconf)
endef

define $(package)_build_cmds
  $(MAKE) -C src
endef

define $(package)_stage_cmds
  $(MAKE) DESTDIR=$($(package)_staging_dir) -C src install
endef

define $(package)_postprocess_cmds
  rm lib/libprotoc.a
endef