#!/bin/bash

export LC_ALL=C
set -eu
set +x

cmd_pref() {
    if command -v "$2" >/dev/null; then
        eval "$1=$2"
    else
        eval "$1=$3"
    fi
}

# If a g-prefixed version of the command exists, use it preferentially.
gprefix() {
    cmd_pref "$1" "g$2" "$2"
}

gprefix READLINK readlink
cd "$(dirname "$("$READLINK" -f "$0")")/.."

# Allow user overrides to $MAKE. Typical usage for users who need it:
#   MAKE=gmake ./util/build.sh -j$(nproc)
if [ -z "${MAKE-}" ]; then
    MAKE=make
fi

# Allow overrides to $BUILD and $HOST for porters. Most users will not need it.
#   BUILD=i686-pc-linux-gnu ./util/build.sh
if [ -z "${BUILD-}" ]; then
    BUILD="$(./depends/config.guess)"
fi
if [ -z "${HOST-}" ]; then
    HOST="$BUILD"
fi

# Allow users to set arbitrary compile flags. Most users will not need this.
if [ -z "${CONFIGURE_FLAGS-}" ]; then
    CONFIGURE_FLAGS=""
fi

if [ "x$*" = 'x--help' ]
then
    cat <<EOF
Usage:

$0 --help
  Show this help message and exit.

$0 [ MAKEARGS... ]
  Build Raptoreum and most of its transitive dependencies from
  source. MAKEARGS are applied to both dependencies and Raptoreum itself.

  Pass flags to ./configure using the CONFIGURE_FLAGS environment variable.
  For example, to enable coverage instrumentation (thus enabling "make cov"
  to work), call:

      CONFIGURE_FLAGS="--enable-lcov --disable-hardening" ./util/build.sh

  For verbose output, use:
      ./util/build.sh V=1
EOF
    exit 0
fi

set -x

eval "$MAKE" --version
as --version

case "$CONFIGURE_FLAGS" in
(*"--enable-debug"*)
    DEBUG=1
;;
(*)
    DEBUG=
;;esac

HOST="$HOST" BUILD="$BUILD" "$MAKE" "$@" -C depends DEBUG="$DEBUG"

if [ "${BUILD_STAGE:-all}" = "depends" ]
then
    exit 0
fi

./autogen.sh
CONFIG_SITE="$PWD/depends/$HOST/share/config.site" ./configure $CONFIGURE_FLAGS
"$MAKE" "$@"
