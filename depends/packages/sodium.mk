package=sodium
lib_name=libsodium
$(package)_version_full=1.0.18-RELEASE
$(package)_version_short=1.0.18
$(package)_download_path=https://github.com/jedisct1/libsodium/releases/download/$($(package)_version_full)/
$(package)_file_name=$(lib_name)-$($(package)_version_short).tar.gz
$(package)_sha256_hash=6f504490b342a4f8a4c4a02fc9b866cbef8622d5df4e5452b46be121e46636c1
$(package)_patches=fix-whitespace.patch

define $(package)_set_vars
$(package)_config_opts=--enable-static --disable-shared --disable-pie
$(package)_config_opts+=--prefix=$(host_prefix)
endef

define $(package)_config_cmds
  ./autogen.sh &&\
  patch -p1 < $($(package)_patch_dir)/fix-whitespace.patch &&\
  $($(package)_autoconf) $($(package)_config_opts)
endef

define $(package)_build_cmds
  $(MAKE)
endef

define $(package)_stage_cmds
  $(MAKE) DESTDIR=$($(package)_staging_dir) install
endef