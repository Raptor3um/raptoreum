package=immer
$(package)_version=v0.7.0
$(package)_download_path=https://github.com/arximboldi/immer/archive
$(package)_download_file=$($(package)_version).tar.gz
$(package)_file_name=$(package)-$($(package)_download_file)
$(package)_sha256_hash=cf67ab428aa3610eb0f72d0ea936c15cce3f91df26ee143ab783acd053507fe4
$(package)_build_subdir=build_tmp
$(package)_dependencies=cmake boost

define $(package)_fetch_cmds
$(call fetch_file,$(package),$($(package)_download_path),$($(package)_download_file),$($(package)_file_name),$($(package)_sha256_hash))
endef

define $(package)_set_vars
  $(package)_config_opts=-DCMAKE_INSTALL_INCLUDEDIR=$(host_prefix)/include
  $(package)_config_opts+=-DCMAKE_INSTALL_LIBDIR=$(host_prefix)/lib
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
