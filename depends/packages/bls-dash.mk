package=bls-dash
$(package)_version=1.1.0
$(package)_download_path=https://github.com/dashpay/bls-signatures/archive
$(package)_download_file=$($(package)_version).tar.gz
$(package)_file_name=$(package)-$($(package)_download_file)
$(package)_build_subdir=build
$(package)_sha256_hash=276c8573104e5f18bb5b9fd3ffd49585dda5ba5f6de2de74759dda8ca5a9deac
$(package)_dependencies=gmp sodium cmake
$(package)_patches=gcc_alignment_cast.patch bn_init.patch

$(package)_relic_version=03f86cb53c8bb75d96ce9e353b7dc8324dbf3e8b
$(package)_relic_download_path=https://github.com/relic-toolkit/relic/archive
$(package)_relic_download_file=$($(package)_relic_version).tar.gz
$(package)_relic_file_name=relic-toolkit-$($(package)_relic_download_file)
$(package)_relic_build_subdir=relic
$(package)_relic_sha256_hash=f3b09185499edc994d43cf168604ee4ef101dd78f17e275533b0a7df560e8947
$(package)_extra_sources=$($(package)_relic_file_name)

define $(package)_fetch_cmds
$(call fetch_file,$(package),$($(package)_download_path),$($(package)_download_file),$($(package)_file_name),$($(package)_sha256_hash)) && \
$(call fetch_file,$(package),$($(package)_relic_download_path),$($(package)_relic_download_file),$($(package)_relic_file_name),$($(package)_relic_sha256_hash))
endef

define $(package)_extract_cmds
  mkdir -p $($(package)_extract_dir) && \
  echo "$($(package)_sha256_hash)  $($(package)_source)" > $($(package)_extract_dir)/.$($(package)_file_name).hash && \
  echo "$($(package)_relic_sha256_hash)  $($(package)_source_dir)/$($(package)_relic_file_name)" >> $($(package)_extract_dir)/.$($(package)_file_name).hash && \
  $(build_SHA256SUM) -c $($(package)_extract_dir)/.$($(package)_file_name).hash && \
  tar --strip-components=1 -xf $($(package)_source) -C . && \
  cp $($(package)_source_dir)/$($(package)_relic_file_name) .
endef

define $(package)_set_vars
  $(package)_config_opts = -DCMAKE_INSTALL_PREFIX=$(host_prefix) -DCMAKE_PREFIX_PATH=$(host_prefix)
  $(package)_config_opts += -DSTLIB=ON -DSHLIB=OFF -DSTBIN=OFF -DBUILD_BLS_PYTHON_BINDINGS=0 -DBUILD_BLS_TESTS=0 -DBUILD_BLS_BENCHMARKS=0
  $(package)_cppflags += --with-pic -UBLSALLOC_SODIUM
endef

define $(package)_preprocess_cmds
  patch -p1 -i $($(package)_patch_dir)/bn_init.patch
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
