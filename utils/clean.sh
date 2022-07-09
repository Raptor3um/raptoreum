#!/bin/sh
# Copyright (c) 2020 The Zcash developers

export LC_ALL=C

rm -f src/Makefile
rm -f src/Makefile.in
rm -f doc/man/Makefile
rm -f doc/man/Makefile.in

rm -f src/config/stamp-h1
rm -f src/config/raptoreum-config.h
rm -f src/obj/build.h
rm -f src/leveldb/build_config.mk

rm -f src/test/buildenv.py
rm -f src/test/data/*.json.h
rm -f src/test/data/*.raw.h

rm -rf test_bitcoin.coverage/ raptoreum-gtest.coverage/ total.coverage/

rm -rf cache
rm -rf target
rm -rf depends/work

find src -type f -and \( -name '*.Po' -or -name '*.Plo' -or -name '*.o' -or -name '*.a' -or -name '*.lib' -or -name '*.la' -or -name '*.lo' -or -name '*.lai' -or -name '*.pc' -or -name '.dirstamp' -or -name '*.gcda' -or -name '*.gcno' -or -name '*.sage.py' -or -name '*.trs' \) -delete

clean_dirs()
{
    find . -depth -path "*/$1/*" -delete
    find . -type d -name "$1" -delete
}

clean_exe()
{
    rm -f "$1" "$1.exe"
}

clean_dep()
{
    rm -rf "$1/autom4te.cache"
    rm -f "$1/build-aux/compile"
    rm -f "$1/build-aux/config.guess"
    rm -f "$1/build-aux/config.sub"
    rm -f "$1/build-aux/depcomp"
    rm -f "$1/build-aux/install-sh"
    rm -f "$1/build-aux/ltmain.sh"
    rm -f "$1/build-aux/missing"
    rm -f "$1/build-aux/test-driver"
    rm -f "$1/build-aux/m4/libtool.m4"
    rm -f "$1/build-aux/m4/lt~obsolete.m4"
    rm -f "$1/build-aux/m4/ltoptions.m4"
    rm -f "$1/build-aux/m4/ltsugar.m4"
    rm -f "$1/build-aux/m4/ltversion.m4"
    rm -f "$1/aclocal.m4"
    rm -f "$1/config.log"
    rm -f "$1/config.status"
    rm -f "$1/gen_context"
    rm -f "$1/configure"
    rm -f "$1/libtool"
    rm -f "$1/Makefile"
    rm -f "$1/Makefile.in"
    rm -f "$1/$2"
    rm -f "$1/$2~"
}

clean_dirs .deps
clean_dirs .libs
clean_dirs __pycache__

clean_exe src/bench/bench_bitcoin
clean_exe src/raptoreum-cli
clean_exe src/raptoreumd
clean_exe src/raptoreum-gtest
clean_exe src/raptoreum-tx
clean_exe src/test/test_raptoreum
clean_exe src/test/test_raptoreum_fuzzy

clean_exe src/leveldb/db_bench
clean_exe src/leveldb/leveldbutil
rm -f src/leveldb/*_test src/leveldb/*_test.exe
rm -f src/leveldb/*.so src/leveldb/*.so.*

clean_dep . src/config/raptoreum-config.h.in

clean_dep src/secp256k1 src/libsecp256k1-config.h.in
rm -f src/secp256k1/src/ecmult_static_context.h
rm -f src/secp256k1/src/libsecp256k1-config.h
rm -f src/secp256k1/src/stamp-h1
rm -f src/secp256k1/.so_locations
clean_exe src/secp256k1/tests
clean_exe src/secp256k1/exhaustive_tests
rm -f src/secp256k1/tests.log src/secp256k1/exhaustive_tests.log src/secp256k1/test-suite.log

clean_dep src/univalue univalue-config.h.in
rm -f src/univalue/univalue-config.h
rm -f src/univalue/stamp-h1
clean_exe src/univalue/test_json
clean_exe src/univalue/unitester
clean_exe src/univalue/no_nul
rm -f src/univalue/test/*.log
