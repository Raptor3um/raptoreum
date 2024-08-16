package=openssl
$(package)_version=1.1.1w
$(package)_download_path=https://www.openssl.org/source
$(package)_file_name=$(package)-$($(package)_version).tar.gz
$(package)_sha256_hash=cf3098950cb4d853ad95c0841f1f9c6d3dc102dccfcacd521d93925208b76ac8

define $(package)_set_vars
$(package)_config_env=AR="$($(package)_ar)" RANLIB="$($(package)_ranlib)" CC="$($(package)_cc)"
$(package)_config_opts=--prefix=$(host_prefix) --openssldir=$(host_prefix)/etc/openssl
$(package)_config_opts+=no-afalgeng
$(package)_config_opts+=no-asm
$(package)_config_opts+=no-bf
$(package)_config_opts+=no-blake2
$(package)_config_opts+=no-camellia
$(package)_config_opts+=no-capieng
$(package)_config_opts+=no-cmac
$(package)_config_opts+=no-cms
$(package)_config_opts+=no-crypto-mdebug
$(package)_config_opts+=no-crypto-mdebug-backtrace
$(package)_config_opts+=no-deprecated
$(package)_config_opts+=no-dso
$(package)_config_opts+=no-egd
$(package)_config_opts+=no-engine
$(package)_config_opts+=no-gost
$(package)_config_opts+=no-heartbeats
$(package)_config_opts+=no-multiblock
$(package)_config_opts+=no-scrypt
$(package)_config_opts+=no-seed
$(package)_config_opts+=no-srp
$(package)_config_opts+=no-srtp
$(package)_config_opts+=no-sse2
$(package)_config_opts+=no-ssl3
$(package)_config_opts+=no-ssl3-method
$(package)_config_opts+=no-ssl-trace
$(package)_config_opts+=no-tls1
$(package)_config_opts+=no-tls1_1
$(package)_config_opts+=no-ts
$(package)_config_opts+=no-tests
$(package)_config_opts+=no-threads
$(package)_config_opts+=no-unit-test
$(package)_config_opts+=no-weak-ssl-ciphers
$(package)_config_opts+=-static --static

# Options below are needed to be switched ON
# due to Qt requirements. More info at:
# https://doc.qt.io/qt-5/ssl.html#enabling-and-disabling-ssl-support

$(package)_config_opts+=no-shared
$(package)_config_opts+=no-static-engine
$(package)_config_opts+=--with-rand-seed=os
$(package)_config_opts+=no-dtls1
$(package)_config_opts+=enable-dh
$(package)_config_opts+=enable-ecdh
$(package)_config_opts+=enable-ecdsa

$(package)_config_opts_android += $($(package)_cflags) $($(package)_cppflags)
$(package)_config_opts_linux += $($(package)_cflags) $($(package)_cppflags)
$(package)_config_opts_mingw32 += $($(package)_cflags) $($(package)_cppflags)
$(package)_config_opts_linux=-fPIC -Wa,--noexecstack
$(package)_config_opts_x86_64_linux=linux-x86_64
$(package)_config_opts_i686_linux=linux-generic32
$(package)_config_opts_arm_linux=linux-generic32
$(package)_config_opts_armv7l_linux=linux-generic32
$(package)_config_opts_aarch64_linux=linux-generic64
$(package)_config_opts_mipsel_linux=linux-generic32
$(package)_config_opts_mips_linux=linux-generic32
$(package)_config_opts_powerpc_linux=linux-generic32
$(package)_config_opts_riscv32_linux=linux-generic32
$(package)_config_opts_riscv64_linux=linux-generic64
$(package)_config_opts_x86_64_darwin=darwin64-x86_64-cc
$(package)_config_opts_arm64_darwin=darwin64-arm64-cc
$(package)_config_opts_aarch64_darwin=darwin64-arm64-cc
$(package)_config_opts_x86_64_mingw32=mingw64
$(package)_config_opts_android=-fPIC
$(package)_config_opts_aarch64_android=linux-generic64 -D__ANDROID_API=29
$(package)_config_opts_x86_64_android=linux-generic64 -D__ANDROID_API=29
$(package)_config_opts_armv7a_android=linux-generic32 -D__ANDROID_API=29
endef

define $(package)_preprocess_cmds
  sed -i.old 's|"engines", "apps", "test", "util", "tools", "fuzz"|"engines", "tools"|' Configure
endef

define $(package)_config_cmds
  ./Configure $($(package)_config_opts)
endef

define $(package)_build_cmds
  $(MAKE) -j4
endef

define $(package)_stage_cmds
  $(MAKE) DESTDIR=$($(package)_staging_dir) -j1 install_sw
endef

define $(package)_postprocess_cmds
  rm -rf share bin etc
endef