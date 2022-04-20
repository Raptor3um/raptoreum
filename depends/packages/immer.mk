package=immer
$(package)_version=v0.6.2
$(package)_download_path=https://github.com/arximboldi/immer/archive
$(package)_download_file=$($(package)_version).tar.gz
$(package)_file_name=$(package)-$($(package)_download_file)
$(package)_sha256_hash=c3bb8847034437dee64adacb04e1e0163ae640b596c582eb4c0aa1d7c6447cd7
$(package)_build_subdir=build_tmp
$(package)_dependencies=cmake boost

define $(package)_fetch_cmds
$(call fetch_file,$(package),$($(package)_download_path),$($(package)_download_file),$($(package)_file_name),$($(package)_sha256_hash))
endef

define $(package)_set_vars
  $(package)_config_opts=-DCMAKE_INSTALL_INCLUDEDIR=$(host_prefix)/include
  $(package)_config_opts+=-DCMAKE_INSTALL_LIBDIR=$(host_prefix)/lib
endef

define $(package)_config_cmds
  $(host_prefix)/bin/cmake ../ $($(package)_config_opts)
endef

define $(package)_build_cmds
  $(MAKE)
endef

define $(package)_stage_cmds
  $(MAKE) DESTDIR=$($(package)_staging_dir) install
endef
