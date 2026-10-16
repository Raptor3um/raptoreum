packages:=boost libevent gmp backtrace cmake immer zeromq openssl

qrencode_linux_packages = qrencode
qrencode_android_packages = qrencode
qrencode_darwin_packages = qrencode
qrencode_mingw32_packages = qrencode

qt_linux_packages:=qt expat dbus libxcb xcb_proto libXau xproto freetype fontconfig libxkbcommon libxcb_util libxcb_util_render libxcb_util_keysyms libxcb_util_image libxcb_util_wm

qt_android_packages=qt
qt_darwin_packages=qt
qt_mingw32_packages=qt

bdb_packages=bdb

upnp_packages=miniupnpc
natpmp_packages=libnatpmp

#darwin_native_packages = native_ds_store native_mac_alias

$(host_arch)_$(host_os)_native_packages += native_b2

# Cross-compiling for macOS needs no packages of its own: hosts/darwin.mk
# drives the clang, lld and llvm binutils installed on the build machine.
