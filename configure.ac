AC_PREREQ([2.69])
define(_CLIENT_VERSION_MAJOR, 0)
define(_CLIENT_VERSION_MINOR, 17)
define(_CLIENT_VERSION_REVISION, 0)
define(_CLIENT_VERSION_BUILD, 1)
define(_CLIENT_VERSION_RC, 0)
define(_CLIENT_VERSION_IS_RELEASE, false)
define(_COPYRIGHT_YEAR, 2022)
define(_COPYRIGHT_HOLDERS,[The %s developers])
define(_COPYRIGHT_HOLDERS_SUBSTITUTION,[[Raptoreum Core]])
AC_INIT([Raptoreum Core],m4_join([.], _CLIENT_VERSION_MAJOR, _CLIENT_VERSION_MINOR, _CLIENT_VERSION_REVISION, m4_if(_CLIENT_VERSION_BUILD, [0], [], _CLIENT_VERSION_BUILD))m4_if(_CLIENT_VERSION_RC, [0], [], [rc]_CLIENT_VERSION_RC),[https://github.com/Raptor3um/raptoreum/issues],[raptoreumcore],[https://raptoreum.com/])
AC_CONFIG_SRCDIR([src/validation.cpp])
AC_CONFIG_HEADERS([src/config/raptoreum-config.h])
AC_CONFIG_AUX_DIR([build-aux])
AC_CONFIG_MACRO_DIR([build-aux/m4])

m4_ifndef([PKG_PROG_PKG_CONFIG], [m4_fatal([PKG_PROG_PKG_CONFIG macro not found. Please install pkg-config and re-run autogen.sh])])
PKG_PROG_PKG_CONFIG
if test "$PKG_CONFIG" = ""; then
  AC_MSG_ERROR([pkg-config not found])
fi

BITCOIN_DAEMON_NAME=raptoreumd
BITCOIN_GUI_NAME=raptoreum-qt
BITCOIN_CLI_NAME=raptoreum-cli
BITCOIN_TX_NAME=raptoreum-tx
BITCOIN_WALLET_TOOL_NAME=raptoreum-wallet

dnl Unless the user specified ARFLAGS, force it to be cr
AC_ARG_VAR([ARFLAGS], [Flags for the archiver, defaults to <cr> if not set])
if test "${ARFLAGS+set}" != "set"; then
  ARFLAGS="cr"
fi

AC_CANONICAL_HOST

AH_TOP([#ifndef RAPTOREUM_CONFIG_H])
AH_TOP([#define RAPTOREUM_CONFIG_H])
AH_BOTTOM([#endif //RAPTOREUM_CONFIG_H])

dnl Automake init set-up and checks
AM_INIT_AUTOMAKE([1.13 no-define subdir-objects foreign])

AM_MAINTAINER_MODE([enable])

dnl make the compilation flags quiet unless V=1 is used
AM_SILENT_RULES([yes])

dnl Compiler checks (here before libtool).
if test "${CXXFLAGS+set}" = "set"; then
  CXXFLAGS_overridden=yes
else
  CXXFLAGS_overridden=no
fi
AC_PROG_CXX

dnl By default, libtool for mingw refuses to link static libs into a dll for
dnl fear of mixing pic/non-pic objects, and import/export complications. Since
dnl we have those under control, re-enable that functionality.
case $host in
  *mingw*) lt_cv_deplibs_check_method="pass_all" ;;
esac

dnl Require C++17 compiler (no GNU extensions)
AX_CXX_COMPILE_STDCXX([17], [noext], [mandatory])

dnl Check if -latomic is required for <std::atomic>
CHECK_ATOMIC

dnl Unless the user specified OBJCXX, force it to be the same as CXX. This ensures
dnl that we get the same -std flags for both.
m4_ifdef([AC_PROG_OBJCXX],[
if test "${OBJCXX+set}" = ""; then
  OBJCXX="${CXX}"
fi
AC_PROG_OBJCXX
])

dnl Since libtool 1.5.2 (released on 2004-01-25), on Linux libtool no longer
dnl sets RPATH for any directories in the dynamic linker search path.
dnl See more: https://wiki.debian.org/RpathIssue
LT_PREREQ([2.4.2])

dnl Libtool init checks.
LT_INIT([pic-only win32-dll])

dnl Check/return PATH for base programs.
AC_PATH_TOOL([AR], [ar])
AC_PATH_TOOL([RANLIB], [ranlib])
AC_PATH_TOOL([STRIP], [strip])
AC_PATH_TOOL([GCOV], [gcov])
AC_PATH_PROG([LLVM_COV], [llvm-cov])
AC_PATH_PROG([LCOV], [lcov])
dnl Python 3.x is supported from 3.4 on (see https://github.com/bitcoin/bitcoin/issues/7893)
AC_PATH_PROGS([PYTHON], [python3.6 python3.7 python3.8 python3.9 python3.10 python3.11 python3 python])
AC_PATH_PROG([GENHTML], [genhtml])
AC_PATH_PROG([GIT], [git])
AC_PATH_PROG([CCACHE], [ccache])
AC_PATH_PROG([XGETTEXT], [xgettext])
AC_PATH_PROG([HEXDUMP], [hexdump])
AC_PATH_TOOL([READELF], [readelf])
AC_PATH_TOOL([CPPFILT], [c++filt])
AC_PATH_TOOL([DSYMUTIL], [dsymutil])
AC_PATH_TOOL([OBJCOPY], [objcopy])
AC_PATH_PROG([DOXYGEN], [doxygen])
AM_CONDITIONAL([HAVE_DOXYGEN], [test -n "$DOXYGEN"])

AC_ARG_VAR([PYTHONPATH], [Augments the default search path for python module files])

AC_ARG_ENABLE([wallet],
    [AS_HELP_STRING([--disable-wallet], [disable wallet (enabled by default)])],
    [enable_wallet=$enableval],
    [enable_wallet=auto])

AC_ARG_WITH([bdb],
    [AS_HELP_STRING([--without-bdb], [disable bdb wallet support (default is enabled if wallet is enabled)])],
    [use_bdb=$withval],
    [use_bdb=auto])

AC_ARG_WITH([miniupnpc],
    [AS_HELP_STRING([--with-miniupnpc], [enable UPNP (default is yes if libminiupnpc is found)])],
    [use_upnp=$withval],
    [use_upnp=auto])

AC_ARG_ENABLE([upnp-default],
    [AS_HELP_STRING([--enable-upnp-default], [if UPNP is enabled, turn it on at startup (default is no)])],
    [use_upnp_default=$enableval],
    [use_upnp_default=no])

AC_ARG_WITH([natpmp],
    [AS_HELP_STRING([--with-natpmp], [enable NAT-PMP (default is yes if libnatpmp is found)])],
    [use_natpmp=$withval],
    [use_natpmp=auto])

AC_ARG_ENABLE([natpmp-default],
    [AS_HELP_STRING([--enabler-natpmp-default], [if NAT-PMP is enabled, turn it on at startup (default is no)])],
    [use_natpmp_default=$enableval],
    [use_natpmp_default=no])

AC_ARG_ENABLE([tests],
    [AS_HELP_STRING([--disable-tests], [do not compile tests (default is to compile)])],
    [use_tests=$enableval],
    [use_tests=yes])

AC_ARG_ENABLE([gui-tests],
    [AS_HELP_STRING([--disable-gui-tests],[do not compile GUI tests (default is to compile if GUI and tests enabled)])],
    [use_gui_tests=$enableval],
    [use_gui_tests=$use_tests])

AC_ARG_ENABLE([bench],
    [AS_HELP_STRING([--disable-bench],[do not compile benchmarks (default is to compile)])],
    [use_bench=$enableval],
    [use_bench=yes])

AC_ARG_ENABLE([extended-functional-tests],
    [AS_HELP_STRING([--enable-extended-functional-tests], [enable expensive functional tests when using lcov (default no)])],
    [use_extended_functional_tests=$enableval],
    [use_extended_functional_tests=no])

AC_ARG_WITH([qrencode],
    [AS_HELP_STRING([--with-qrencode], [enable QR code support (default is yes if qt is enabled and libqrencode is found)])],
    [use_qr=$withval],
    [use_qr=auto])

AC_ARG_ENABLE([hardening],
    [AS_HELP_STRING([--disable-hardening], [do not attempt to harden the resulting executables (default is to harden when possible)])],
    [use_hardening=$enableval],
    [use_hardening=auto])

AC_ARG_ENABLE([reduce-exports],
    [AS_HELP_STRING([--enable-reduce-exports], [attempt to reduce exported symbols in the resulting executables (default is no)])],
    [use_reduce_exports=$enableval],
    [use_reduce_exports=no])

AC_ARG_ENABLE([ccache],
    [AS_HELP_STRING([--disable-ccache], [disable ccache for building (default is AUTO)])],
    [use_ccache=$enableval],
    [use_ccache=auto])

dnl Suppress warnings from external headers like Boost, Qt.
dnl May be useful if warnings from external headers clutter the build output
dnl too much, so that it becomes difficult to spot Raptoreum Core warnings
dnl or if they cause a build failure with --enable-werror.
AC_ARG_ENABLE([suppress-external-warnings],
  [AS_HELP_STRING([--enable-suppress-external-warnings], [Suppress warnings from external headers (default is no)])],
  [suppress_external_warnings=$enableval],
  [suppress_external_warnings=no])

AC_ARG_ENABLE([lcov],
  [AS_HELP_STRING([--enable-lcov], [enable lcov testing (default is no)])],
  [use_lcov=$enableval],
  [use_lcov=no])

AC_ARG_ENABLE([lcov-branch-coverage],
  [AS_HELP_STRING([--enable-lcov-branch-coverage], [enable lcov testing branch coverage (default is no)])],
  [use_lcov_branch=yes],
  [use_lcov_branch=no])

AC_ARG_ENABLE([threadlocal], [AS_HELP_STRING([--enable-threadlocal],
  [enable features that depend on the c++ thread_local keyword (currently just thread names in debug logs). (default is to enable if there is platform support)])],
  [use_thread_local=$enableval], [use_thread_local=auto])

AC_ARG_ENABLE([asm],
  [AS_HELP_STRING([--disable-asm], [disable assembly routines (enabled by default)])],
  [use_asm=$enableval],
  [use_asm=yes])

if test "$use_asm" = "yes"; then
  AC_DEFINE([USE_ASM], [1], [Define this symbol to build in assembly routines])
fi

AC_ARG_ENABLE([zmq],
  [AS_HELP_STRING([--disable-zmq], [disable ZMQ notifications])],
  [use_zmq=$enableval],
  [use_zmq=yes])

AC_ARG_ENABLE([man], [AS_HELP_STRING([--disable-man], [do not install man pages (default is to install)])], [enable_man=$enableval], [enable_man=yes])
AM_CONDITIONAL([ENABLE_MAN], [test "$enable_man" != "no"])

dnl Enable debug
AC_ARG_ENABLE([debug],
  [AS_HELP_STRING([--enable-debug], [use debug compiler flags and macros (default is no)])],
  [enable_debug=$enableval],
  [enable_debug=no])

dnl Enable exception stacktrace
AC_ARG_ENABLE([stacktraces],
  [AS_HELP_STRING([--enable-stacktraces], [gather and print exception stack traces (default is yes)])],
  [enable_stacktraces=$enableval],
  [enable_stacktraces=yes])

dnl Enable crash hooks
AC_ARG_ENABLE([crash-hooks],
  [AS_HELP_STRING([--enable-crash-hooks], [hook into exception/signal/assert handling to gather stack traces (default is no)])],
  [enable_crashhooks=$enableval],
  [enable_crashhooks=yes])

dnl Enable in-wallet miner
AC_ARG_ENABLE([miner],
  [AS_HELP_STRING([--enable-miner], [enable in-wallet miner (default is yes)])],
  [enable_miner=$enableval],
  [enable_miner=yes])

AM_CONDITIONAL([ENABLE_MINER], [test "$enable_miner" = "yes"])
if test "$enable_miner" = "yes"; then
    AC_DEFINE([ENABLE_MINER], [1], [Define this symbol if in-wallet miner should be enabled])
fi

dnl Enable different -fsanitize options
AC_ARG_WITH([sanitizers],
    [AS_HELP_STRING([--with-sanitizers],
                    [comma separated list of extra sanitizers to build with (default is none enabled)])],
    [use_sanitizers=$withval])

dnl Enable gprof profiling
AC_ARG_ENABLE([gprof],
    [AS_HELP_STRING([--enable-gprof],
                    [use gprof profiling compiler flags (default is no)])],
    [enable_gprof=$enableval],
    [enable_gprof=no])

dnl Turn warnings into errors
AC_ARG_ENABLE([werror],
    [AS_HELP_STRING([--enable-werror],
                    [Treat certain compiler warnings as errors (default is no)])],
    [enable_werror=$enableval],
    [enable_werror=no])

AC_ARG_ENABLE([lto],
    [AS_HELP_STRING([--enable-lto],[build without using LTO (default is no)])],
    [enable_lto=$enableval],
    [enable_lto=no])

AC_LANG_PUSH([C++])

dnl Check for a flag to turn compiler warning into errors. This is helpful for checks which may
dnl appear to succeed because by default they morely emit warnings when they fail.
dnl
dnl Note that this is not necessarily a check to see if -Werror is supported, but rather to see if
dnl a compiler with -Werror fan succeed. This is important because the compiler may already be
dnl warning about something unrelated, for example about some path issue. If that is the case,
dnl -Werror cannot be used because all of those warnings would be turned into errors.
AX_CHECK_COMPILE_FLAG([-Werror], [CXXFLAG_WERROR="-Werror"], [CXXFLAG_WERROR=""])

dnl Check for a flag to turn linker warnings into errors. When flags are passed to linkers via the
dnl compiler driver using a -Wl,-foo flag, linker warnings may be swallowed rather than bubbling up.
dnl See note above, the same applies here as well.
dnl
dnl LDFLAG_WERROR Should only be used when testing -Wl,*
case $host in
  *darwin*)
    AX_CHECK_LINK_FLAG([-Wl,-fatal_warnings], [LDFLAG_WERROR="-Wl,-fatal_warnings"], [LDFLAG_WERROR=""])
  ;;
  *)
    AX_CHECK_LINK_FLAG([-Wl,--fatal-warnings], [LDFLAG_WERROR="-Wl,--fatal-warnings"], [LDFLAG_WERROR=""])
  ;;
esac

if test "$enable_debug" = "yes"; then
  dnl If debugging is enabled and the user hasn't overriden CXXFLAGS,
  dnl clear them to prevent autoconfs "-g -O2" being added.
  dnl Otherwise we'd end up with "-O0 -g3 -g -O2"
  if test "$CXXFLAGS_overridden" = "no"; then
  CXXFLAGS=""
  fi

  dnl Disable all optimizations
  AX_CHECK_COMPILE_FLAG([-O0], [DEBUG_CXXFLAGS="$DEBUG_CXXFLAGS -O0"], [], [$CXXFLAG_WERROR])

  dnl Prefer -g3, fall back to -g if that is unavailable.
  AX_CHECK_COMPILE_FLAG(
    [-g3],
    [DEBUG_CXXFLAGS="$DEBUG_CXXFLAGS -g3"],
    [AX_CHECK_COMPILE_FLAG([-g], [DEBUG_CXXFLAGS="$DEBUG_CXXFLAGS -g"], [], [$CXXFLAG_WERROR])],
    [$CXXFLAG_WERROR])

  AX_CHECK_PREPROC_FLAG([-DDEBUG_CORE],             [DEBUG_CPPFLAGS="$DEBUG_CPPFLAGS -DDEBUG_CORE"],             [], [$CXXFLAG_WERROR])
  AX_CHECK_PREPROC_FLAG([-DDEBUG_LOCKORDER],        [DEBUG_CPPFLAGS="$DEBUG_CPPFLAGS -DDEBUG_LOCKORDER"],        [], [$CXXFLAG_WERROR])
  AX_CHECK_PREPROC_FLAG([-DABORT_ON_FAILED_ASSUME], [DEBUG_CPPFLAGS="$DEBUG_CPPFLAGS -DABORT_ON_FAILED_ASSUME"], [], [£CXXFLAG_WERROR])
  AX_CHECK_COMPILE_FLAG([-ftrapv],                  [DEBUG_CXXFLAGS="$DEBUG_CXXFLAGS -ftrapv"],                  [], [$CXXFLAG_WERROR])
else
  if test "$GCC" = "yes"; then
    CFLAGS="$CFLAGS -g1 -fno-omit-frame-pointer"
  fi

  if test "$GXX" = "yes"; then
    CXXFLAGS="$CXXFLAGS -g1 -fno-omit-frame-pointer"
  fi
fi

if test "$enable_stacktraces" != "no"; then
  AC_CHECK_HEADERS([execinfo.h], [], [enable_stacktraces=no])
fi

AM_CONDITIONAL([ENABLE_STACKTRACES], [test "$enable_stacktraces" = "yes"])
if test "$enable_stacktraces" = "yes"; then
    AC_DEFINE([ENABLE_STACKTRACES], [1], [Define this symbol if stacktraces should be enabled])
else
    enable_crashhooks=no
fi

AM_CONDITIONAL([ENABLE_CRASH_HOOKS], [test "$enable_crashhooks" = "yes"])
if test "$enable_crashhooks" = "yes"; then
    AC_DEFINE([ENABLE_CRASH_HOOKS], [1], [Define this symbol if crash hooks should be enabled])
fi

AX_CHECK_LINK_FLAG([-Wl,-wrap=__cxa_allocate_exception], [LINK_WRAP_SUPPORTED=yes],,,)
AM_CONDITIONAL([CRASH_HOOKS_WRAPPED_CXX_ABI], [test "$LINK_WRAP_SUPPORTED" = "yes"])

if test "$LINK_WRAP_SUPPORTED" = "yes"; then
  AC_DEFINE([CRASH_HOOKS_WRAPPED_CXX_ABI], [1], [Define this symbol to use wrapped CXX ABIs for exception stacktraces])
fi

dnl Needed for MinGW targets when debug symbols are enabled as compiled objects get very large
AX_CHECK_COMPILE_FLAG([-Wa,-mbig-obj], [CXXFLAGS="$CXXFLAGS -Wa,-mbig-obj"],,,)

if test "$enable_lto" = "yes"; then
  AX_CHECK_COMPILE_FLAG([-flto], [LTO_CXXFLAGS="$LTO_CXXFLAGS -flto"], [AC_MSG_ERROR([compile failed with -flto])], [$CXXFLAG_WERROR])
  AX_CHECK_LINK_FLAG([-flto],    [LTO_LDFLAGS="$LTO_LDFLAGS -flto"],   [AC_MSG_ERROR([link failed with -flto])],    [$CXXFLAG_WERROR])
fi

if test "$use_sanitizers" != ""; then
  dnl First check if the compiler accepts flags. If an incompatible pair like
  dnl -fsanitize=address,thread is used here, this check will fail. This will also
  dnl fail if a bad argument is passed, e.g. -fsanitize=undefined
  AX_CHECK_COMPILE_FLAG([-fsanitize=$use_sanitizers], [SANITIZER_CXXFLAGS="-fsanitize=$use_sanitizers"],
    [AC_MSG_ERROR([compiler did not accept requested flags])])

  dnl Some compilers (e.g. GCC) require additional libraries like libasan,
  dnl libtsan, libubsan, etc. Make sure linking still works with the sanitize
  dnl flag. This is a separate check so we can give a better error message when
  dnl the sanitize flags are supported by the compiler but the actual sanitizer
  dnl libs are missing.
  AX_CHECK_LINK_FLAG([-fsanitize=$use_sanitizers], [SANITIZER_LDFLAGS="-fsanitize=$use_sanitizers"],
    [AC_MSG_ERROR([linker did not accept requested flags, you are missing required libraries])], [],
    [AC_LANG_PROGRAM([[
    #include <cstdint>
    #include <cstddef>
    extern "C" int LLVMFuzzerTestOneInput(const uint8_t *data, size_t size) { return 0; }
    __attribute__((weak)) // allow for libFuzzer linking
    ]],[[]])])
fi

ERROR_CXXFLAGS=
if test "$enable_werror" = "yes"; then
  if test "$CXXFLAG_WERROR" = ""; then
    AC_MSG_ERROR([enable-werror set but -Werror is not usable])
  fi
  ERROR_CXXFLAGS=$CXXFLAG_WERROR

  dnl -Wreturn-typeis broken in GCC for MinGW-w64.
  dnl https://sourceforge.net/p/mingw-w64/bugs/306/
  AX_CHECK_COMPILE_FLAG([-Werror=return-type], [], [ERROR_CXXFLAGS="$ERROR_CXXFLAGS -Wno-error=return-type"], [$CXXFLAG_WERROR],
                        [AC_LANG_SOURCE([[#include <cassert>
                                          int f(){ assert(false); }]])])
fi

if test "$CXXFLAGS_overridden" = "no"; then
  AX_CHECK_COMPILE_FLAG([-Wall],                       [WARN_CXXFLAGS="$WARN_CXXFLAGS -Wall"],                       [], [$CXXFLAG_WERROR])
  AX_CHECK_COMPILE_FLAG([-Wextra],                     [WARN_CXXFLAGS="$WARN_CXXFLAGS -Wextra"],                     [], [$CXXFLAG_WERROR])
  AX_CHECK_COMPILE_FLAG([-Wgnu],                       [WARN_CXXFLAGS="$WARN_CXXFLAGS -Wgnu"],                       [], [$CXXFLAG_WERROR])
  dnl some compilers will ignore -Wformat-security without -Wformat, so just combine the two here.
  AX_CHECK_COMPILE_FLAG([-Wformat -Wformat-security],  [WARN_CXXFLAGS="$WARN_CXXFLAGS -Wformat -Wformat-security"],  [], [$CXXFLAG_WERROR])
  AX_CHECK_COMPILE_FLAG([-Wvla],                       [WARN_CXXFLAGS="$WARN_CXXFLAGS -Wvla"],                       [], [$CXXFLAG_WERROR])
  AX_CHECK_COMPILE_FLAG([-Wshadow-field],              [WARN_CXXFLAGS="$WARN_CXXFLAGS -Wshadow-field"],              [], [$CXXFLAG_WERROR])
  AX_CHECK_COMPILE_FLAG([-Wthread-safety],             [WARN_CXXFLAGS="$WARN_CXXFLAGS -Wthread-safety"],             [], [$CXXFLAG_WERROR])
  AX_CHECK_COMPILE_FLAG([-Wloop-analysis],             [WARN_CXXFLAGS="$WARN_CXXFLAGS -Wloop-analysis"],             [], [$CXXFLAG_WERROR])
  AX_CHECK_COMPILE_FLAG([-Wredundant-decls],           [WARN_CXXFLAGS="$WARN_CXXFLAGS -Wredundant-decls"],           [], [$CXXFLAG_WERROR])
  AX_CHECK_COMPILE_FLAG([-Wunused-member-function],    [WARN_CXXFLAGS="$WARN_CXXFLAGS -Wunused-member-function"],    [], [$CXXFLAG_WERROR])
  AX_CHECK_COMPILE_FLAG([-Wdate-time],                 [WARN_CXXFLAGS="$WARN_CXXFLAGS -Wdate-time"],                 [], [$CXXFLAG_WERROR])
  AX_CHECK_COMPILE_FLAG([-Wconditional-uninitialized], [WARN_CXXFLAGS="$WARN_CXXFLAGS -Wconditional-uninitialized"], [], [$CXXFLAG_WERROR])
  AX_CHECK_COMPILE_FLAG([-Wduplicated-branches],       [WARN_CXXFLAGS="$WARN_CXXFLAGS -Wduplicated-branches"],       [], [$CXXFLAG_WERROR])
  AX_CHECK_COMPILE_FLAG([-Wduplicated-cond],           [WARN_CXXFLAGS="$WARN_CXXFLAGS -Wduplicated-cond"],           [], [$CXXFLAG_WERROR])
  AX_CHECK_COMPILE_FLAG([-Wlogical-op],                [WARN_CXXFLAGS="$WARN_CXXFLAGS -Wlogical-op"],                [], [$CXXFLAG_WERROR])
  AX_CHECK_COMPILE_FLAG([-Woverloaded-virtual],        [WARN_CXXFLAGS="$WARN_CXXFLAGS -Woverloded-virtual"],         [], [$CXXFLAG_WERROR])
  dnl -Wsuggest-override is broken with GCC before 9.2
  dnl https://gcc.gnu.org/bugzilla/show_bug.cgi?id=78010
  AX_CHECK_COMPILE_FLAG([-Wsuggest-override], [WARN_CXXFLAGS="$WARN_CXXFLAGS -Wsuggest-override"],                               [], [$CXXFLAG_WERROR],
                        [AC_LANG_SOURCE([[struct A { virtual void f(); }; struct B : A { void f() final; };]])])
  AX_CHECK_COMPILE_FLAG([-Wunreachable-code-loop-increment], [WARN_CXXFLAGS="$WARN_CXXFLAGS -Wunreachable-code-loop-increment"], [], [$CXXFLAG_WERROR])
  AX_CHECK_COMPILE_FLAG([-Wimplicit-fallthrough],            [WARN_CXXFLAGS="$WARN_CXXFLAGS -Wimplicit-fallthrough"],            [], [$CXXFLAG_WERROR])

  if test "$suppress_external_warnings" != "no" ; then
    AX_CHECK_COMPILE_FLAG([-Wdocumentation], [WARN_CXXFLAGS="$WARN_CXXFLAGS -Wdocumentation"], [], [$CXXFLAG_WERROR])
  fi

  dnl Some compilers (gcc) ignore unknown -Wno-* options, but warn about all
  dnl unknown options if any other warning is produced. Test the -Wfoo case, and
  dnl set the -Wno-foo case if it works.
  AX_CHECK_COMPILE_FLAG([-Wunused-parameter],        [NOWARN_CXXFLAGS="$NOWARN_CXXFLAGS -Wno-unused-parameter"],        [], [$CXXFLAG_WERROR])
  AX_CHECK_COMPILE_FLAG([-Wself-assign],             [NOWARN_CXXFLAGS="$NOWARN_CXXFLAGS -Wno-self-assign"],             [], [$CXXFLAG_WERROR])
  if test "$supress_external_warnings" != "yes" ; then
    AX_CHECK_COMPILE_FLAG([-Wdeprecated-copy], [NOWARN_CXXFLAGS="$NOWARN_CXXFLAGS -Wno-deprecated-copy"],  [], [$CXXFLAG_WERROR])
  fi
fi

dnl Do not allow extended (non-ASCII) symbols in identifiers. This is easier for code review.
AX_CHECK_COMPILE_FLAG([-fno-extended-identifiers], [CORE_CXXFLAGS="$CORE_CXXFLAGS -fno-extended-identifiers"], [], [$CXXFLAG_WERROR])

enable_arm_crc=no
enable_arm_shani=no
enable_sse42=no
enable_sse41=no
enable_avx2=no
enable_x86_shani=no

if test "$use_asm" = "yes"; then

dnl Check for optional instruction set support. Enabling these does _not_ imply that all code will
dnl be compiled with them, rather that specific objects/libs may use them after checking for runtime
dnl compatibility.

dnl x86
AX_CHECK_COMPILE_FLAG([-msse4.2],     [SSE42_CXXFLAGS="-msse4.2"],         [], [$CXXFLAG_WERROR])
AX_CHECK_COMPILE_FLAG([-msse4.1],     [SSE41_CXXFLAGS="-msse4.1"],         [], [$CXXFLAG_WERROR])
AX_CHECK_COMPILE_FLAG([-mavx -mavx2], [AVX2_CXXFLAGS="-mavx -mavx2"],      [], [$CXXFLAG_WERROR])
AX_CHECK_COMPILE_FLAG([-msse4 -msha], [X86_SHANI_CXXFLAGS="-msse4 -msha"], [], [$CXXFLAG_WERROR])

enable_clmul=
AX_CHECK_COMPILE_FLAG([-mpclmul], [enable_clmul=yes], [], [$CXXFLAG_WERROR], [AC_LANG_PROGRAM([
  #include <stdint.h>
  #include <x86intrin.h>
], [
  __m128i a = _mm_cvtsi64_si128((uint64_t)7);
  __m128i b = _mm_clmulepi64_si128(a, a, 37);
  __m128i c = _mm_srli_epi64(b, 41);
  __m128i d = _mm_xor_si128(b, c);
  uint64_t e = _mm_cvtsi128_si64(d);
  return e == 0;
])])

if test "$enable_clmul" = "yes"; then
  CLMUL_CXXFLAGS="-mpclmul"
  AC_DEFINE([HAVE_CLMUL], [1], [Define this symbol if clmul instructions can be used])
fi

TEMP_CXXFLAGS="$CXXFLAGS"
CXXFLAGS="$CXXFLAGS $SSE42_CXXFLAGS"
AC_MSG_CHECKING([for SSE4.2 intrinsics])
AC_COMPILE_IFELSE([AC_LANG_PROGRAM([[
    #include <stdint.h>
    #if defined(_MSC_VER)
    #include <intrin.h>
    #elif defined(__GNUC__) && defined(__SSE4_2__)
    #include <nmmintrin.h>
    #endif
  ]],[[
    uint64_t l = 0;
    l = _mm_crc32_u8(l, 0);
    l = _mm_crc32_u32(l, 0);
    l = _mm_crc32_u64(l, 0);
    return l;
  ]])],
 [ AC_MSG_RESULT([yes]); enable_sse42=yes],
 [ AC_MSG_RESULT([no])]
)
CXXFLAGS="$TEMP_CXXFLAGS"

TEMP_CXXFLAGS="$CXXFLAGS"
CXXFLAGS="$CXXFLAGS $SSE41_CXXFLAGS"
AC_MSG_CHECKING([for SSE4.1 intrinsics])
AC_COMPILE_IFELSE([AC_LANG_PROGRAM([[
    #include <stdint.h>
    #include <immintrin.h>
  ]],[[
    __m128i l = _mm_set1_epi32(0);
    return _mm_extract_epi32(l, 3);
  ]])],
 [ AC_MSG_RESULT([yes]); enable_sse41=yes; AC_DEFINE([ENABLE_SSE41], [1], [Define this symbol to build code that uses SSE4.1 intrinsics]) ],
 [ AC_MSG_RESULT([no])]
)
CXXFLAGS="$TEMP_CXXFLAGS"

TEMP_CXXFLAGS="$CXXFLAGS"
CXXFLAGS="$CXXFLAGS $AVX2_CXXFLAGS"
AC_MSG_CHECKING([for AVX2 intrinsics])
AC_COMPILE_IFELSE([AC_LANG_PROGRAM([[
    #include <stdint.h>
    #include <immintrin.h>
  ]],[[
    __m256i l = _mm256_set1_epi32(0);
    return _mm256_extract_epi32(l, 7);
  ]])],
 [ AC_MSG_RESULT([yes]); enable_avx2=yes; AC_DEFINE([ENABLE_AVX2], [1], [Define this symbol to build code that uses AVX2 intrinsics]) ],
 [ AC_MSG_RESULT([no])]
)
CXXFLAGS="$TEMP_CXXFLAGS"

TEMP_CXXFLAGS="$CXXFLAGS"
CXXFLAGS="$CXXFLAGS $X86_SHANI_CXXFLAGS"
AC_MSG_CHECKING([for X86 SHA-NI intrinsics])
AC_COMPILE_IFELSE([AC_LANG_PROGRAM([[
    #include <stdint.h>
    #include <immintrin.h>
  ]],[[
    __m128i i = _mm_set1_epi32(0);
    __m128i j = _mm_set1_epi32(1);
    __m128i k = _mm_set1_epi32(2);
    return _mm_extract_epi32(_mm_sha256rnds2_epu32(i, i, k), 0);
  ]])],
 [ AC_MSG_RESULT([yes]); enable_x86_shani=yes; AC_DEFINE([ENABLE_X86_SHANI], [1], [Define this symbol to build code that uses x86 SHA-NI intrinsics]) ],
 [ AC_MSG_RESULT([no])]
)
CXXFLAGS="$TEMP_CXXFLAGS"

# ARM
AX_CHECK_COMPILE_FLAG([-march=armv8-a+crc+crypto], [ARM_CRC_CXXFLAGS="-march=armv8-a+crc+crypto"],   [], [$CXXFLAG_WERROR])
AX_CHECK_COMPILE_FLAG([-march=armv8-a+crc+crypto], [ARM_SHANI_CXXFLAGS="-march=armv8-a+crc+crypto"], [], [$CXXFLAG_WERROR])

TEMP_CXXFLAGS="$CXXFLAGS"
CXXFLAGS="$CXXFLAGS $ARM_CRC_CXXFLAGS"
AC_MSG_CHECKING([for ARMv8 CRC32 intrinsics])
AC_COMPILE_IFELSE([AC_LANG_PROGRAM([[
    #include <arm_acle.h>
    #include <arm_neon.h>
  ]],[[
#ifdef __aarch64__
    __crc32cb(0, 0); __crc32ch(0, 0); __crc32cw(0, 0); __crc32cd(0, 0);
    vmull_p64(0, 0);
#else
#error "crc32 library does not support hardware acceleration on 32-bit ARM"
#endif
  ]])],
 [ AC_MSG_RESULT([yes]); enable_arm_crc=yes; ],
 [ AC_MSG_RESULT([no])]
)
CXXFLAGS="$TEMP_CXXFLAGS"

TEMP_CXXFLAGS="$CXXFLAGS"
CXXFLAGS="$CXXFLAGS $ARM_SHANI_CXXFLAGS"
AC_MSG_CHECKING([for ARMv8 SHA-NI intrinsics])
AC_COMPILE_IFELSE([AC_LANG_PROGRAM([[
    #include <arm_acle.h>
    #include <arm_neon.h>
  ]],[[
    uint32x4_t a, b, c;
    vsha256h2q_u32(a, b, c);
    vsha256hq_u32(a, b, c);
    vsha256su0q_u32(a, b);
    vsha256su1q_u32(a, b, c);
  ]])],
 [ AC_MSG_RESULT([yes]); enable_arm_shani=yes; AC_DEFINE([ENABLE_ARM_SHANI], [1], [Define this symbol to build code that uses ARMv8 SHA-NI intrinsics]) ],
 [ AC_MSG_RESULT([no])]
)
CXXFLAGS="$TEMP_CXXFLAGS"

fi

CORE_CPPFLAGS="$CORE_CPPFLAGS -DHAVE_BUILD_INFO"

AC_ARG_WITH([utils],
  [AS_HELP_STRING([--with-utils],
  [build raptoreum-cli raptoreum-tx raptoreum-wallet (default=yes)])],
  [build_bitcoin_utils=$withval],
  [build_bitcoin_utils=yes])

AC_ARG_ENABLE([util-cli],
  [AS_HELP_STRING([--enable-util-cli],
  [build raptoreum-cli])],
  [build_bitcoin_cli=$enableval],
  [build_bitcoin_cli=$build_bitcoin_utils])

AC_ARG_ENABLE([util-tx],
  [AS_HELP_STRING([--enable-util-tx],
  [build raptoreum-tx])],
  [build_bitcoin_tx=$enableval],
  [build_bitcoin_tx=$build_bitcoin_utils])

AC_ARG_ENABLE([util-wallet],
  [AS_HELP_STRING([--enable-util-wallet],
  [build raptoreum-wallet])],
  [build_bitcoin_wallet=$enableval],
  [build_bitcoin_wallet=$build_bitcoin_utils])

AC_ARG_WITH([libs],
  [AS_HELP_STRING([--with-libs],
  [build libraries (default=yes)])],
  [build_bitcoin_libs=$withval],
  [build_bitcoin_libs=yes])

AC_ARG_WITH([daemon],
  [AS_HELP_STRING([--with-daemon],
  [build raptoreumd daemon (default=yes)])],
  [build_bitcoind=$withval],
  [build_bitcoind=yes])

case $host in
  *mingw*)
    AC_CHECK_LIB([mingwthrd], [main], [], [AC_MSG_ERROR([libmingwthrd missing])])
    AC_CHECK_LIB([kernel32],  [main], [], [AC_MSG_ERROR([libkernel32 missing])])
    AC_CHECK_LIB([user32],    [main], [], [AC_MSG_ERROR([libuser32 missing])])
    AC_CHECK_LIB([gdi32],     [main], [], [AC_MSG_ERROR([libgdi32 missing])])
    AC_CHECK_LIB([comdlg32],  [main], [], [AC_MSG_ERROR([libcomdlg32 missing])])
    AC_CHECK_LIB([winspool],  [main], [], [AC_MSG_ERROR([libwinspool missing])])
    AC_CHECK_LIB([winmm],     [main], [], [AC_MSG_ERROR([libwinmm missing])])
    AC_CHECK_LIB([shell32],   [main], [], [AC_MSG_ERROR([libshell32 missing])])
    AC_CHECK_LIB([comctl32],  [main], [], [AC_MSG_ERROR([libcomctl32 missing])])
    AC_CHECK_LIB([ole32],     [main], [], [AC_MSG_ERROR([libole32 missing])])
    AC_CHECK_LIB([oleaut32],  [main], [], [AC_MSG_ERROR([liboleaut32 missing])])
    AC_CHECK_LIB([uuid],      [main], [], [AC_MSG_ERROR([libuuid missing])])
    AC_CHECK_LIB([rpcrt4],    [main], [], [AC_MSG_ERROR([librpcrt4 missing])])
    AC_CHECK_LIB([advapi32],  [main], [], [AC_MSG_ERROR([libadvapi32 missing])])
    AC_CHECK_LIB([ws2_32],    [main], [], [AC_MSG_ERROR([libws2_32 missing])])
    AC_CHECK_LIB([mswsock],   [main], [], [AC_MSG_ERROR([libmswsock missing])])
    AC_CHECK_LIB([shlwapi],   [main], [], [AC_MSG_ERROR([libshlwapi missing])])
    AC_CHECK_LIB([iphlpapi],  [main], [], [AC_MSG_ERROR([libiphlpapi missing])])
    AC_CHECK_LIB([crypt32],   [main], [], [AC_MSG_ERROR([libcrypt32 missing])])

    dnl -static is interpreted by libtool, where it has a different meaning.
    dnl In libtool-speak, it's -all-static.
    AX_CHECK_LINK_FLAG([-static], [LIBTOOL_APP_LDFLAGS="$LIBTOOL_APP_LDFLAGS -all-static"])

    AC_PATH_PROG([MAKENSIS], [makensis], [none])
    if test "$MAKENSIS" = "none"; then
      AC_MSG_WARN([makensis not found. Cannot create installer.])
    fi

    AC_PATH_TOOL([WINDRES], [windres], [none])
    if test "$WINDRES" = "none"; then
      AC_MSG_ERROR([windres not found])
    fi

    CORE_CPPFLAGS="$CORE_CPPFLAGS -D_MT -DWIN32 -D_WINDOWS -DBOOST_THREAD_USE_LIB -D_WIN32_WINNT=0x0601 -D_WIN32_IE=0x0501 -DWIN32_LEAN_AND_MEAN"

    dnl libtool insists upon adding -nostdlib and a list of objects/libs to link against.
    dnl That breaks our ability to build dll's with static libgcc/libstdc++/libssp. Override
    dnl its command here, with the predeps/postdeps removed, and -static inserted. Postdeps are
    dnl also overridden to prevent their insertion later.
    dnl This should only affect dll's.
    archive_cmds_CXX="\$CC -shared \$libobjs \$deplibs \$compiler_flags -static -o \$output_objdir/\$soname \${wl}--enable-auto-image-base -Xlinker --out-implib -Xlinker \$lib"
    postdeps_CXX=

    dnl We require Windows 7 (NT 6.1) or later
    AX_CHECK_LINK_FLAG([-Wl,--major-subsystem-version -Wl,6 -Wl,--minor-subsystem-version -Wl,1], [CORE_LDFLAGS="$CORE_LDFLAGS -Wl,--major-subsystem-version -Wl,6 -Wl,--minor-subsystem-version -Wl,1"], [], [$LDFLAG_WERROR])
  ;;
  *darwin*)
    if test $cross_compiling != "yes"; then
      AC_CHECK_PROG([BREW], [brew], [brew])
      if test "$BREW" = "brew"; then
        dnl These Homebrew packages may be keg-only, meaning that they won't be found
        dnl in expected paths because they may conflict with system files. Ask
        dnl Homebrew where each one is located, then adjust paths accordingly.
        dnl It's safe to add these paths even if the functionality is disabled by
        dnl the user (--without-wallet or --without-gui for example).

        if test "$use_bdb" != "no" && $BREW list --versions berkeley-db@4 >/dev/null && test "$BDB_CFLAGS" = "" && test "$BDB_LIBS" = ""; then
          bdb_prefix=$($BREW --prefix berkeley-db@4 2>/dev/null)
          dnl This must precede the call to BITCOIN_FIND_BDB48 below.
          BDB_CFLAGS="-I$bdb_prefix/include"
          BDB_LIBS="-L$bdb_prefix/lib -ldb_cxx-4.8"
        fi

        if $BREW list --versions qt@5 >/dev/null; then
          export PKG_CONFIG_PATH="$($BREW --prefix qt@5 2>/dev/null)/lib/pkgconfig:$PKG_CONFIG_PATH"
        fi

        case $host in
          *aarch64*)
            dnl The preffered Homebrew prefix for Apple Silicon is /opt/homebrew.
            dnl Therefore, as we do not use pkg-config to detect miniupnpc and lipnatpmp
            dnl packages, we should set the CPPFLAGS and LDFLAGS variables for them
            dnl explicitly
            if test "$use_upnp" != "no" && $BREW list --versions miniupnpc >/dev/null; then
              miniupnpc_prefix=$($BREW --prefix miniupnpc 2>/dev/null)
              if test "$suppress_external_warnings" != "no"; then
                CORE_CPPFLAGS="$CORE_CPPFLAGS -isystem $miniupnpc_prefix/include"
              else
                CORE_CPPFLAGS="$CORE_CPPFLAGS -I$miniupnpc_prefix/include"
              fi
              CORE_LDFLAGS="$CORE_LDFLAGS -L$miniupnpc_prefix/lib"
            fi
            if test "$use_natpmp" != "no" && $BREW list --version libnatpmp >/dev/null; then
              libnatpmp_prefix=$($BREW --prefix libnatpmp 2>/dev/null)
              if test "$suppress_external_warnings" != "no"; then
                CORE_CPPFLAGS="$CORE_CPPFLAGS -isystem $libnatpmp_prefix/include"
              else
                CORE_CPPFLAGS="$CORE_CPPFLAGS -I$libnatpmp_prefix/include"
              fi
              CORE_LDFLAGS="$CORE_LDFLAGS -L$libnatpmp_prefix/lib"
            fi
          ;;
        esac
      fi
    else
      case $build_os in
        *linux*)
          AC_PATH_TOOL([DSYMUTIL], [dsymutil], [dsymutil])
          AC_PATH_TOOL([INSTALL_NAME_TOOL], [install_name_tool], [install_name_tool])
          AC_PATH_TOOL([OTOOL], [otool], [otool])
          AC_PATH_PROGS([XORRISOFS], [xorrisofs], [xorrisofs])

          dnl libtool will try to strip the static lib, which is a problem for
          dnl cross-builds because strip attempts to call a hard-coded ld,
          dnl which may not exist in the path. Stripping the .a is not
          dnl necessary, so just disable it.
          old_striplib=
        ;;
      esac
    fi

    AX_CHECK_LINK_FLAG([-Wl,-headerpad_max_install_names], [CORE_LDFLAGS="$CORE_LDFLAGS -Wl,-headerpad_max_install_names"], [], [$LDFLAG_WERROR])
    CORE_CPPFLAGS="$CORE_CPPFLAGS -DMAC_OSX -DOBJC_OLD_DISPATCH_PROTOTYPES=0"
    OBJCXXFLAGS="$CXXFLAGS"
  ;;
  *android*)
    dnl make sure android stays above linux for hosts like *linux-android*
    case $host in
      *x86_64*) ANDROID_ARCH=x86_64 ;;
      *aarch64*) ANDROID_ARCH=arm64-v8a ;;
      *armv7a*) ANDROID_ARCH=armeabi-v7a ;;
      *) AC_MSG_ERROR("Could not determine Android arch or it is unsupported") ;;
    esac
    ;;
esac

if test "$use_extended_functional_tests" != "no"; then
  AC_SUBST(EXTENDED_FUNCTIONAL_TESTS, --extended)
fi

if test "$use_lcov" = "yes"; then
  if test "$LCOV" = ""; then
    AC_MSG_ERROR([lcov testing requested but lcov not found])
  fi
  if test "$PYTHON" = ""; then
    AC_MSG_ERROR([lcov testing requested but python not found])
  fi
  if test "$GENHTML" = ""; then
    AC_MSG_ERROR([lcov testing requested but genhtml not found])
  fi

  AC_MSG_CHECKING([whether compiler is Clang])
  AC_PREPROC_IFELSE([AC_LANG_SOURCE([[
      #if defined(__clang__) && defined(__llvm__)
      // Compiler is Clang
      #else
      # error Compiler is not Clang
      #endif
    ]])],[
      AC_MSG_RESULT([yes])
      if test "$LLVM_COV" = ""; then
        AC_MSG_ERROR([lcov testing requested but llvm-cov not found])
      fi
      COV_TOOL="$LLVM_COV gcov"
    ],[
      AC_MSG_RESULT([no])
      if test "$GCOV" = "x"; then
        AC_MSG_ERROR([lcov testing requested but gcov not found])
      fi
      COV_TOOL="$GCOV"
  ])
  AC_SUBST(COV_TOOL)
  AC_SUBST(COV_TOOL_WRAPPER, "cov_tool_wrapper.sh")
  LCOV="$LCOV --gcov-tool $(pwd)/$COV_TOOL_WRAPPER"

  AX_CHECK_LINK_FLAG([--coverage], [CORE_LDFLAGS="$CORE_LDFLAGS --coverage"], [AC_MSG_ERROR([lcov testing requested but --coverage linker flag does not work])])
  AX_CHECK_COMPILE_FLAG([--coverage],[CORE_CXXFLAGS="$CORE_CXXFLAGS --coverage"], [AC_MSG_ERROR([lcov testing requested but --coverage flag does not work])])
  dnl If coverage is enabled and the user hasn't overriden CXXFLAGS,
  dnl clear them to prevent autoconfs "-g -O2" being added.
  dnl Otherwise we'd end up with "--coverage -Og -O0 -g -O2".
  if test "$CXXFLAGS_overridden" = "no"; then
    CXXFLAGS=""
  fi
  CORE_CXXFLAGS="CORE_CXXFLAGS -Og -O0"
fi

if test "$use_lcov_branch" != "no"; then
  AC_SUBST(LCOV_OPTS, "$LCOV_OPTS --rc lcov_branch_coverage=1")
fi

dnl Check for endianness
AC_C_BIGENDIAN

dnl Check for pthread compile/link requirements
AX_PTHREAD

dnl The following macro will add the necessary defines to raptoreum-config.h, but
dnl they also need to be passed down to any subprojects. Pull the results out of
dnl the cache and add them to CPPFLAGS.
AC_SYS_LARGEFILE
dnl detect POSIX or GNU variant of strerror_r
AC_FUNC_STRERROR_R

if test "$ac_cv_sys_file_offset_bits" != "" &&
   test "$ac_cv_sys_file_offset_bits" != "no" &&
   test "$ac_cv_sys_file_offset_bits" != "unknown"; then
  CORE_CPPFLAGS="$CORE_CPPFLAGS -D_FILE_OFFSET_BITS=$ac_cv_sys_file_offset_bits"
fi

if test "$ac_cv_sys_large_files" != "" &&
   test "$ac_cv_sys_large_files" != "no" &&
   test "$ac_cv_sys_large_files" != "unknown"; then
  CORE_CPPFLAGS="$CORE_CPPFLAGS -D_LARGE_FILES=$ac_cv_sys_large_files"
fi

AC_SEARCH_LIBS([clock_gettime],[rt])

if test "$enable_gprof" = "yes"; then
    dnl -pg is incompatible with -pie. Since hardening and profiling together doesn't make sense,
    dnl we simply make them mutually exclusive here. Additionally, hardened toolchains may force
    dnl -pie by default, in which case it needs to be turned off with -no-pie.

    if test "$use_hardening" = "yes"; then
        AC_MSG_ERROR([gprof profiling is not compatible with hardening. Reconfigure with --disable-hardening or --disable-gprof])
    fi
    use_hardening=no
    AX_CHECK_COMPILE_FLAG([-pg],[GPROF_CXXFLAGS="-pg"],
        [AC_MSG_ERROR([gprof profiling requested but not available])], [$CXXFLAG_WERROR])

    AX_CHECK_LINK_FLAG([-no-pie], [GPROF_LDFLAGS="-no-pie"])
    AX_CHECK_LINK_FLAG([-pg], [GPROF_LDFLAGS="$GPROF_LDFLAGS -pg"],
        [AC_MSG_ERROR([gprof profiling requested but not available])], [$GPROF_LDFLAGS])
fi

dnl All windows code is PIC, forcing it on just adds useless compile warnings
AS_CASE([$host_os], [*mingw*], [], [AX_CHECK_COMPILE_FLAG([-fPIC], [PIC_FLAGS="-fPIC"])])

dnl All versions of gcc that we commonly use for building are subject to bug
dnl https://gcc.gnu.org/bugzilla/show_bug.cgi?id=90384. To work around that,
dnl set -fstack-reuse=none for all gcc builds. (Only gcc understands this flag)
AX_CHECK_COMPILE_FLAG([-fstack-reuse=none], [HARDENED_CXXFLAGS="$HARDENED_CXXFLAGS -fstack-reuse=none"])
if test "$use_hardening" != "no"; then
  use_hardening=yes
  AX_CHECK_COMPILE_FLAG([-Wstack-protector], [HARDENED_CXXFLAGS="$HARDENED_CXXFLAGS -Wstack-protector"])
  AX_CHECK_COMPILE_FLAG([-fstack-protector-all], [HARDENED_CXXFLAGS="$HARDENED_CXXFLAGS -fstack-protector-all"])

  dnl -fcf-protection used with Clang 7 causes ld to emit warnings:
  dnl ld: error: ... <corrupt x86 feature size: 0x8>
  dnl use CHECK_LINK_FLAG & --fatal-warnings to ensure we won't use the flag in this case.
  AX_CHECK_LINK_FLAG([-fcf-protection=full], [HARDENED_CXXFLAGS="$HARDENED_CXXFLAGS -fcf-protection=full"], [], [$LDFLAGS_WERROR])

  dnl stack-clash-protector doesn't currently work, and likely should just be skipped for Windows.
  dnl See https://gcc.gnu.org/bugzilla/show_bug.cgi?id=90458 for more details.
  AS_CASE([$host], [*mingw*], [], [
    AX_CHECK_COMPILE_FLAG([-fstack-clash-protection], [HARDENED_CXXFLAGS="$HARDENED_CXXFLAGS -fstack-clash-protection"], [], [$CXXFLAG_WERROR])
  ])

  dnl When enable_debug is yes, all optimizations are disabled.
  dnl However, FORTIFY_SOURCE requires that there is some level of ioptimization,
  dnl otherwise it does nothing and just creates a compiler warning.
  dnl Since FORTIFY_SOURCE is a no-op without optimization,
  dnl do not enable it when enable_debug is yes.
  if test "$enable_debug" != "yes"; then
    AX_CHECK_PREPROC_FLAG([-D_FORTIFY_SOURCE=2],[
      AX_CHECK_PREPROC_FLAG([-U_FORTIFY_SOURCE],[
        HARDENED_CPPFLAGS="$HARDENED_CPPFLAGS -U_FORTIFY_SOURCE"
      ])
      HARDENED_CPPFLAGS="$HARDENED_CPPFLAGS -D_FORTIFY_SOURCE=2"
    ])
  fi

  AX_CHECK_LINK_FLAG([-Wl,--enable-reloc-section], [HARDENED_LDFLAGS="$HARDENED_LDFLAGS -Wl,--enable-reloc-section"], [], [$LDFLAG_WERROR])
  AX_CHECK_LINK_FLAG([-Wl,--dynamicbase],          [HARDENED_LDFLAGS="$HARDENED_LDFLAGS -Wl,--dynamicbase"],          [], [$LDFLAG_WERROR])
  AX_CHECK_LINK_FLAG([-Wl,--nxcompat],             [HARDENED_LDFLAGS="$HARDENED_LDFLAGS -Wl,--nxcompat"],             [], [$LDFLAG_WERROR])
  AX_CHECK_LINK_FLAG([-Wl,--high-entropy-va],      [HARDENED_LDFLAGS="$HARDENED_LDFLAGS -Wl,--high-entropy-va"],      [], [$LDFLAG_WERROR])
  AX_CHECK_LINK_FLAG([-Wl,-z,relro],               [HARDENED_LDFLAGS="$HARDENED_LDFLAGS -Wl,-z,relro"],               [], [$LDFLAG_WERROR])
  AX_CHECK_LINK_FLAG([-Wl,-z,now],                 [HARDENED_LDFLAGS="$HARDENED_LDFLAGS -Wl,-z,now"],                 [], [$LDFLAG_WERROR])
  AX_CHECK_LINK_FLAG([-Wl,-z,separate-code],       [HARDENED_LDFLAGS="$HARDENED_LDFLAGS -Wl,-z,separate-code"],       [], [$LDFLAG_WERROR])

  AX_CHECK_LINK_FLAG([-fPIE -pie], [PIE_FLAGS="-fPIE"; HARDENED_LDFLAGS="$HARDENED_LDFLAGS -pie"], [], [$CXXFLAG_WERROR])

  AS_CASE([$host], [*mingw*], [AC_CHECK_LIB([ssp], [main], [], [AC_MSG_ERROR([libssp missing])])])
fi

dnl These flags are specific do ld64, and may cause issues with other linkers.
dnl For example: GNU ld will interpret -dead_strip as a -de and then try and use
dnl "ad_strip" as the symbol for the entry point.
AS_CASE([$host], [*darwin*], [
  AX_CHECK_LINK_FLAG([-Wl,-dead_strip],        [CORE_LDFLAGS="$CORE_LDFLAGS -Wl,-dead_strip"],           [], [$LDFLAG_WERROR])
  AX_CHECK_LINK_FLAG([-Wl,-dead_strip_dylibs], [CORE_LDFLAGS="$CORE_LDFLAGS -Wl,-dead_strip_dylibs"],    [], [$LDFLAG_WERROR])
  AX_CHECK_LINK_FLAG([-Wl,-bind_at_load],      [HARDENED_LDFLAGS="$HARDENED_LDFLAGS -Wl,-bind_at_load"], [], [$LDFLAG_WERROR])
])

AC_CHECK_HEADERS([endian.h sys/endian.h byteswap.h stdio.h stdlib.h unistd.h strings.h sys/types.h sys/stat.h sys/select.h sys/prctl.h sys/sysctl.h vm/vm_param.h sys/vmmeter.h sys/resources.h])

AC_CHECK_DECLS([getifaddrs, freeifaddrs], [CHECK_SOCKET],,
    [#include <sys/types.h>
    #include <ifaddrs.h>]
)

dnl Check for daemon(3), unrelated to --with-daemon (although used by it)
AC_CHECK_DECLS([daemon])

AC_CHECK_DECLS([pipe2])

AC_CHECK_DECLS([le16toh, le32toh, le64toh, htole16, htole32, htole64, be16toh, be32toh, be64toh, htobe16, htobe32, htobe64],,,
		[#if HAVE_ENDIAN_H
                 #include <endian.h>
                 #elif HAVE_SYS_ENDIAN_H
                 #include <sys/endian.h>
                 #endif])

AC_CHECK_DECLS([bswap_16, bswap_32, bswap_64],,,
		[#if HAVE_BYTESWAP_H
                 #include <byteswap.h>
                 #endif])

AC_MSG_CHECKING([for __builtin_clzl])
AC_COMPILE_IFELSE([AC_LANG_PROGRAM([[ ]], [[
 (void) __builtin_clzl(0);
  ]])],
 [ AC_MSG_RESULT([yes]); have_clzl=yes; AC_DEFINE([HAVE_BUILTIN_CLZL], [1], [Define this symbol if you have __builtin_clzl])],
 [ AC_MSG_RESULT([no]); have_clzl=no;]
)

AC_MSG_CHECKING([for __builtin_clzll])
AC_COMPILE_IFELSE([AC_LANG_PROGRAM([[ ]], [[
  (void) __builtin_clzll(0);
  ]])],
 [ AC_MSG_RESULT([yes]); have_clzll=yes; AC_DEFINE([HAVE_BUILTIN_CLZLL], [1], [Define this symbol if you have __builtin_clzll])],
 [ AC_MSG_RESULT([no]); have_clzll=no;]
)

dnl Check for malloc_info (for memory statistics information in getmemoryinfo)
AC_MSG_CHECKING([for getmemoryinfo])
AC_COMPILE_IFELSE([AC_LANG_PROGRAM([[#include <malloc.h>]],
 [[ int f = malloc_info(0, NULL); ]])],
 [ AC_MSG_RESULT([yes]); AC_DEFINE([HAVE_MALLOC_INFO], [1], [Define this symbol if you have malloc_info]) ],
 [ AC_MSG_RESULT([no])]
)

dnl Check for mallopt(M_ARENA_MAX) (to set glibc arenas)
AC_MSG_CHECKING([for mallopt M_ARENA_MAX])
AC_COMPILE_IFELSE([AC_LANG_PROGRAM([[#include <malloc.h>]],
  [[ mallopt(M_ARENA_MAX, 1); ]])],
  [ AC_MSG_RESULT([yes]); AC_DEFINE([HAVE_MALLOPT_ARENA_MAX], [1], [Define this symbol if you have mallopt with M_ARENA_MAX]) ],
  [ AC_MSG_RESULT([no])]
)

dnl Check for posix_fallocate
AC_MSG_CHECKING([for posix_fallocate])
AC_COMPILE_IFELSE([AC_LANG_PROGRAM([[
                   // same as in src/util.cpp
                   #ifdef __linux__
                   #ifdef _POSIX_C_SOURCE
                   #undef _POSIX_C_SOURCE
                   #endif
                   #define _POSIX_C_SOURCE 200112L
                   #endif // __linux__
                   #include <fcntl.h>]],
                   [[ int f = posix_fallocate(0, 0, 0); ]])],
 [ AC_MSG_RESULT([yes]); AC_DEFINE([HAVE_POSIX_FALLOCATE], [1], [Define this symbol if you have posix_fallocate]) ],
 [ AC_MSG_RESULT([no])]
)

AC_MSG_CHECKING([for default visibility attribute])
AC_COMPILE_IFELSE([AC_LANG_SOURCE([
  int foo(void) __attribute__((visibility("default")));
  int main(){}
  ])],
  [
    AC_DEFINE([HAVE_DEFAULT_VISIBILITY_ATTRIBUTE], [1], [Define if the visibility attribute is supported.])
    AC_MSG_RESULT([yes])
  ],
  [
    AC_MSG_RESULT([no])
    if test "$use_reduce_exports" = "yes"; then
      AC_MSG_ERROR([Cannot find a working visibility attribute. Use --disable-reduce-exports.])
    fi
  ]
)

AC_MSG_CHECKING([for dllexport attribute])
AC_COMPILE_IFELSE([AC_LANG_SOURCE([
  __declspec(dllexport) int foo(void);
  int main(){}
  ])],
  [
    AC_DEFINE([HAVE_DLLEXPORT_ATTRIBUTE], [1], [Define if the dllexport attribute is supported.])
    AC_MSG_RESULT([yes])
  ],
  [AC_MSG_RESULT([no])]
)

if test "$use_thread_local" = "yes" || test "$use_thread_local" = "auto"; then
  TEMP_LDFLAGS="$LDFLAGS"
  LDFLAGS="$TEMP_LDFLAGS $PTHREAD_CFLAGS"
  AC_MSG_CHECKING([for thread_local support])
  AC_LINK_IFELSE([AC_LANG_SOURCE([
    #include <thread>
    static thread_local int foo = 0;
    static void run_thread() { foo++; }
    int main() {
      for(int i = 0; i < 10; i++) { std::thread(run_thread).detach(); }
      return foo;
    }
    ])],
    [
      case $host in
        *mingw*)
          dnl mingw32's implementation of thread_local has also been shown to behave
          dnl erroneously under concurrent usage; see:
          dnl https://gist.github.com/jamesob/fe9a872051a88b2025b1aa37bfa98605
          AC_MSG_RESULT([no])
        ;;
        *freebsd*)
          dnl FreeBSD's implementation of thread_local is also buggy (per
          dnl https://groups.google.com/d/msg/bsdmailinglist/22ncTZAbDp4/Dii_pII5AwAJ)
          AC_MSG_RESULT([no])
        ;;
        *)
          AC_DEFINE([HAVE_THREAD_LOCAL], [1], [Define if thread_local is supported.])
          AC_MSG_RESULT([yes])
        ;;
      esac
    ],
    [
      AC_MSG_RESULT([no])
    ]
  )
  LDFLAGS="$TEMP_LDFLAGS"
fi

dnl check for gmtime_r(), fallback to gmtime_s() if that is unavailable
dnl fail if neither are available.
AC_MSG_CHECKING([for gmtime_r])
AC_COMPILE_IFELSE([AC_LANG_PROGRAM([[#include <ctime>]],
  [[ gmtime_r((const time_t *) nullptr, (struct tm *) nullptr); ]] ) ],
  [ AC_MSG_RESULT([yes]); AC_DEFINE([HAVE_GMTIME_R], [1], [Define this symbol if gmtime_r is available]) ],
  [ AC_MSG_RESULT([no]);
    AC_MSG_CHECKING([for gmtime_s]);
    AC_COMPILE_IFELSE([AC_LANG_PROGRAM([[#include <ctime>]],
       [[ gmtime_s((struct tm *) nullptr, (const time_t *) nullptr); ]] ) ],
       [ AC_MSG_RESULT([yes])],
       [ AC_MSG_RESULT([no]); AC_MSG_ERROR([Both gmtime_r and gmtime_s are unavailable]) ]
    )
  ]
)

dnl Check for different ways of gathering OS randomness
AC_MSG_CHECKING([for Linux getrandom syscall])
AC_COMPILE_IFELSE([AC_LANG_PROGRAM([[#include <unistd.h>
  #include <sys/syscall.h>
  #include <linux/random.h>]],
 [[ syscall(SYS_getrandom, nullptr, 32, 0); ]] ) ],
 [ AC_MSG_RESULT([yes]); AC_DEFINE([HAVE_SYS_GETRANDOM], [1], [Define this symbol if the Linux getrandom system call is available]) ],
 [ AC_MSG_RESULT([no])]
)

AC_MSG_CHECKING([for getentropy via random.h])
AC_COMPILE_IFELSE([AC_LANG_PROGRAM([[#include <unistd.h>
 #include <sys/random.h>]],
 [[ getentropy(nullptr, 32); ]] ) ],
 [ AC_MSG_RESULT([yes]); AC_DEFINE([HAVE_GETENTROPY_RAND], [1], [Define this symbol if the BSD getentropy system call is available with sys/random.h]) ],
 [ AC_MSG_RESULT([no])]
)

AC_MSG_CHECKING([for sysctl])
AC_COMPILE_IFELSE([AC_LANG_PROGRAM([[#include <sys/types.h>
  #include <sys/sysctl.h>]],
 [[ #ifdef __linux__
    # error "Do not use sysctl on Linux. It is deprecated even when it works"
    #endif
    sysctl(nullptr, 2, nullptr, nullptr, nullptr, 0); ]] ) ],
  [ AC_MSG_RESULT([yes]); AC_DEFINE([HAVE_SYSCTL], [1], [Define this symbol if the BSD sysctl() is available]) ],
  [ AC_MSG_RESULT([no])]
 )

AC_MSG_CHECKING([for sysctl KERN_ARND])
AC_COMPILE_IFELSE([AC_LANG_PROGRAM([[#include <sys/types.h>
  #include <sys/sysctl.h>]],
 [[ #ifdef __linux__
    #error "Do not use sysctl on Linux. It is deprecated even when it works"
    #endif
    static int name[2] = {CTL_KERN, KERN_ARND};
    sysctl(name, 2, nullptr, nullptr, nullptr, 0); ]] ) ],
 [ AC_MSG_RESULT([yes]); AC_DEFINE([HAVE_SYSCTL_ARND], [1], [Define this symbol if the BSD sysctl(KERN_ARND) is available])  ],
 [ AC_MSG_RESULT([no])]
)

AC_MSG_CHECKING([for if type char equals int8_t])
AC_COMPILE_IFELSE([AC_LANG_PROGRAM([[#include <stdint.h>
  #include <type_traits>]],
 [[ static_assert(std::is_same<int8_t, char>::value, ""); ]] ) ],
 [ AC_MSG_RESULT([yes]); AC_DEFINE([CHAR_EQUALS_INT8], [1], [Define this symbol if type char equals int8_t]) ],
 [ AC_MSG_RESULT([no])]
)

AS_CASE([$host], [*mingw*], [
  if test "$enable_stacktraces" != "no"; then
    AC_SEARCH_LIBS([backtrace], [execinfo], [], [AC_MSG_ERROR([Unable to find backtrace()])])
  fi
])

AC_MSG_CHECKING([for fdatasync])
AC_COMPILE_IFELSE([AC_LANG_PROGRAM([[#include <unistd.h>]],
 [[ fdatasync(0); ]])],
 [ AC_MSG_RESULT([yes]); HAVE_FDATASYNC=1 ],
 [ AC_MSG_RESULT([no]); HAVE_FDATASYNC=0 ]
)
AC_DEFINE_UNQUOTED([HAVE_FDATASYNC], [$HAVE_FDATASYNC], [Define to 1 if fdatasync is available.])

AC_MSG_CHECKING([for F_FULLFSYNC])
AC_COMPILE_IFELSE([AC_LANG_PROGRAM([[#include <fcntl.h>]],
 [[ fcntl(0, F_FULLFSYNC, 0); ]])],
 [ AC_MSG_RESULT([yes]); HAVE_FULLFSYNC=1 ],
 [ AC_MSG_RESULT([no]); HAVE_FULLFSYNC=0 ]
)

AC_MSG_CHECKING([for O_CLOEXEC])
AC_COMPILE_IFELSE([AC_LANG_PROGRAM([[#include <fcntl.h>]],
 [[ open("", O_CLOEXEC); ]])],
 [ AC_MSG_RESULT([yes]); HAVE_O_CLOEXEC=1 ],
 [ AC_MSG_RESULT([no]); HAVE_O_CLOEXEC=0 ]
)
AC_DEFINE_UNQUOTED([HAVE_O_CLOEXEC], [$HAVE_O_CLOEXEC], [Define to 1 if O_CLOEXEC flag is available.])

dnl crc32c platform checks
AC_MSG_CHECKING([for __builtin_prefetch])
AC_COMPILE_IFELSE([AC_LANG_PROGRAM([[ ]], [[
  char data = 0;
  const char* address = &data;
  __builtin_prefetch(address, 0, 0);
  ]])],
 [ AC_MSG_RESULT([yes]); HAVE_BUILTIN_PREFETCH=1 ],
 [ AC_MSG_RESULT([no]); HAVE_BUILTIN_PREFETCH=0 ]
)

AC_MSG_CHECKING([for _mm_prefetch])
AC_COMPILE_IFELSE([AC_LANG_PROGRAM([[#include <xmmintrin.h>]], [[
  char data = 0;
  const char* address = &data;
  _mm_prefetch(address, _MM_HINT_NTA);
  ]])],
 [ AC_MSG_RESULT([yes]); HAVE_MM_PREFETCH=1 ],
 [ AC_MSG_RESULT([no]); HAVE_MM_PREFETCH=0 ]
)

AC_MSG_CHECKING([for strong getauxval support in the system headers])
AC_COMPILE_IFELSE([AC_LANG_PROGRAM([[
    #include <sys/auxv.h>
  ]], [[
    getauxval(AT_HWCAP);
  ]])],
 [ AC_MSG_RESULT([yes]); HAVE_STRONG_GETAUXVAL=1; AC_DEFINE([HAVE_STRONG_GETAUXVAL], [1], [Define this symbol to build code that uses getauxval]) ],
 [ AC_MSG_RESULT([no]); HAVE_STRONG_GETAUXVAL=0 ]
)

have_any_system=no
AC_MSG_CHECKING([for std::system])
AC_LINK_IFELSE(
    [ AC_LANG_PROGRAM(
        [[ #include <cstdlib> ]],
        [[ int nErr = std::system(""); ]]
    )],
    [ AC_MSG_RESULT([yes]); have_any_system=yes],
    [ AC_MSG_RESULT([no]) ]
)

AC_MSG_CHECKING([for ::_wsystem])
AC_LINK_IFELSE(
    [ AC_LANG_PROGRAM(
        [[ ]],
        [[ int nErr = ::_wsystem(""); ]]
    )],
    [ AC_MSG_RESULT([yes]); have_any_system=yes],
    [ AC_MSG_RESULT([no]) ]
)

if test "$have_any_system" != "no"; then
  AC_DEFINE([HAVE_SYSTEM], [1], [Define if std::system or ::wsystem is available])
fi

dnl SUPPRESSED_CPPFLAGS=SUPPRESS_WARNINGS([$SOME_CPPFLAGS])
dnl Replace -I with -isystem in $SOME_CPPFLAGS to suppress warnings from
dnl headers from its include directories and return the result.
dnl See -isystem documentation:
dnl https://gcc.gnu.org/onlinedocs/gcc/Directory-Options.html
dnl https://clang.llvm.org/docs/ClangCommandLineReference.html#cmdoption-clang-isystem-directory
dnl Do not change "-I/usr/include" to "-isystem /usr/include" because that
dnl is not necessary (/usr/include is already a system directory) and because
dnl it would break GCC's #include_next.
AC_DEFUN([SUPPRESS_WARNINGS],
         [$(echo $1 |${SED} -E -e 's/(^| )-I/\1-isystem /g' -e 's;-isystem /usr/include([/ ]|$);-I/usr/include\1;g')])

dnl enable-fuzz should disable all other targets
if test "$enable_fuzz" = "yes"; then
  AC_MSG_WARN([enable-fuzz will disable all other targets and force --enable-fuzz-binary=yes])
  build_bitcoin_utils=no
  build_bitcoin_cli=no
  build_bitcoin_tx=no
  build_bitcoind=no
  build_bitcoin_libs=no
  bitcoin_enable_qt=no
  bitcoin_enable_qt_test=no
  bitcoin_enable_qt_dbus=no
  use_bench=no
  use_upnp=no
  use_natpmp=no
  use_zmq=no
  enable_fuzz_binary=yes

  AX_CHECK_PREPROC_FLAG([-DABORT_ON_FAILED_ASSUME], [DEBUG_CPPFLAGS="$DEBUG_CPPFLAGS -DABORT_ON_FAILED_ASSUME"], [], [$CXXFLAG_WERROR])
else
  BITCOIN_QT_INIT

  dnl sets $bitcoin_enable_qt, $bitcoin_enable_qt_test, $bitcoin_enable_qt_dbus.
  BITCOIN_QT_CONFIGURE([5.11.3])

  dnl Keep a copy of the original $QT_INCLUDES and use it when invoking qt's moc
  QT_INCLUDES_UNSUPPRESSED=$QT_INCLUDES
  if test "$suppress_external_warnings" != "no" ; then
    QT_INCLUDES=SUPPRESS_WARNINGS($QT_INCLUDES)
    QT_DBUS_INCLUDES=SUPPRESS_WARNINGS($QT_DBUS_INCLUDES)
    QT_TEST_INCLUDES=SUPPRESS_WARNINGS($QT_TEST_INCLUDES)
  fi
fi

if test "$enable_fuzz_binary" = "yes"; then
  AC_MSG_CHECKING([whether main function is needed for fuzz binary])
  AX_CHECK_LINK_FLAG(
    [],
    [AC_MSG_RESULT([no])],
    [AC_MSG_RESULT([yes]); CORE_CPPFLAGS="$CORE_CPPFLAGS -DPROVIDE_FUZZ_MAIN_FUNCTION"],
    [$SANITIZER_LDFLAGS],
    [AC_LANG_PROGRAM([[
      #include <cstdint>
      #include <cstddef>
      extern "C" int LLVMFuzzerTestOneInput(const uint8_t* data, size_t size) { return 0; }
      /* comment to remove the main function ...
     ]],[[
      */ int not_main() {
     ]])])

  CHECK_RUNTIME_LIB
fi

if test "$enable_wallet" != "no"; then
  dnl Check for libdb_cxx only if wallet enabled
  if test "$use_bdb" != "no"; then
    BITCOIN_FIND_BDB48
    if test "$suppress_external_warnings" != "no"; then
      BDB_CPPFLAGS=SUPPRESS_WARNINGS($BDB_CPPFLAGS)
    fi
  fi

  dnl Disable wallet if --without-bdb
  if test "$use_bdb" = "no"; then
    if test "$enable_wallet" = "yes"; then
      AC_MSG_ERROR([wallet functionality requested but no BDB support available.])
    fi
    enable_wallet=no
  fi
fi

if test "$build_bitcoin_cli$build_bitcoin_tx$build_bitcoind$bitcoin_enable_qt$use_bench$use_tests" = "nononononono"; then
  use_upno=no
  use_natpmp=no
  use_zmq=no
fi

dnl Check for libminiupnpc (optional)
if test "$use_upnp" != "no"; then
  TEMP_CPPFLAGS="$CPPFLAGS"
  CPPFLAGS="$CPPFLAGS $MINIUPNPC_CPPFLAGS"
  AC_CHECK_HEADERS(
    [miniupnpc/miniupnpc.h miniupnpc/upnpcommands.h miniupnpc/upnperrors.h],
    [AC_CHECK_LIB([miniupnpc], [upnpDiscover], [MINIUPNPC_LIBS="$MINIUPNPC_LIBS -lminiupnpc"], [have_miniupnpc=no], [$MINIUPNPC_LIBS])],
    [have_miniupnpc=no]
  )
  dnl Minimum miniupnpc API version supported is 10. This keeps compatibility
  dnl with Ubuntu 16.04 LTS and Debian 8 libminiupnpc-dev packages.
  if test "$have_miniupnpc" != "no"; then
    AC_MSG_CHECKING([whether miniUPnPc API version is supported])
    AC_PREPROC_IFELSE([AC_LANG_PROGRAM([[
        @%:@include <miniupnpc/miniupnpc.h>
      ]], [[
        #if MINIUPNPC_API_VERSION >= 10
        // Everything is OK
        #else
        #  error miniUPnPc API version is too old
        #endif
      ]])],[
        AC_MSG_RESULT([yes])
      ],[
      AC_MSG_RESULT([no])
      AC_MSG_WARN([miniUPnPc API version < 10 is unsupported, disabling UPnP support.])
      have_miniupnpc=no
    ])
  fi
  CPPFLAGS="$TEMP_CPPFLAGS"
fi

dnl Check fot libnatpmp
if test "$use_natpmp" != "no"; then
  TEMP_CPPFLAGS="$CPPFLAGS"
  CPPFLAGS="$CPPFLAGS $NATPMP_CPPFLAGS"
  AC_CHECK_HEADERS([natpmp.h],
                   [AC_CHECK_LIB([natpmp], [initnatpmp], [NATPMP_LIBS="$NATPMP_LIBS -lnatpmp"], [have_natpmp=no], [$NATPMP_LIBS])],
                   [have_natpmp=no])
  CPPFLAGS="$TEMP_CPPFLAGS"
fi

if test "$build_bitcoin_wallet$build_bitcoin_cli$build_bitcoin_tx$build_bitcoind$bitcoin_enable_qt$use_tests$use_bench" = "nonononononono"; then
    use_boost=no
else
    use_boost=yes
fi

if test "$use_boost" = "yes"; then

  dnl Check for boost libs
  AX_BOOST_BASE([1.64.0],[],[AC_MSG_ERROR([Boost is not available!])])
  if test "$want_boost" = "no"; then
    AC_MSG_ERROR([only libraptoreumconsensus can be built without boost])
  fi
  AX_BOOST_FILESYSTEM
  AX_BOOST_SYSTEM
  AX_BOOST_THREAD

  if test "$suppress_external_warnings" != "no"; then
    BOOST_CPPFLAGS=SUPPRESS_WARNINGS($BOOST_CPPFLAGS)
  fi
fi

dnl Check for reduced exports
if test "$use_reduce_exports" = "yes"; then
  AX_CHECK_COMPILE_FLAG([-fvisibility=hidden], [CORE_CXXFLAGS="$CORE_CXXFLAGS -fvisibility=hidden"],
  [AC_MSG_ERROR([Cannot set default symbol visibility. Use --disable-reduce-exports.])], [$CXXFLAG_WERROR])
  AX_CHECK_LINK_FLAG([-Wl,--exclude-libs,ALL], [RELDFLAGS="-Wl,--exclude-libs,ALL"], [], [$LDFLAG_WERROR])
fi

if test "$use_tests" = "yes"; then
  if test "$HEXDUMP" = ""; then
    AC_MSG_ERROR([hexdump is required for tests])
  fi

  if test "$use_boost" = "yes"; then
    AX_BOOST_UNIT_TEST_FRAMEWORK

    dnl Determine if -DBOOST_TEST_DYN_LINK is needed
    AC_MSG_CHECKING([for dynamic linked boost test])
    TEMP_LIBS="$LIBS"
    LIBS="$LIBS $BOOST_LDFLAGS $BOOST_UNIT_TEST_FRAMEWORK_LIB"
    TEMP_CPPFLAGS="$CPPFLAGS"
    CPPFLAGS="$CPPFLAGS $BOOST_CPPFLAGS"
    AC_LINK_IFELSE([AC_LANG_SOURCE([
        #define BOOST_TEST_DYN_LINK
        #define BOOST_TEST_MAIN
        #include <boost/test/unit_test.hpp>

        ])],
      [AC_MSG_RESULT([yes])]
      [TESTDEFS="$TESTDEFS -DBOOST_TEST_DYN_LINK"],
      [AC_MSG_RESULT([no])])
    LIBS="$TEMP_LIBS"
    CPPFLAGS="$TEMP_CPPFLAGS"
  fi
fi

if test "$use_boost" = "yes"; then
  BOOST_LIBS="$BOOST_LDFLAGS $BOOST_FILESYSTEM_LIB $BOOST_SYSTEM_LIB $BOOST_THREAD_LIB"
fi

dnl libevent check
if test "$build_bitcoin_cli$build_bitcoind$bitcoin_enable_qt$use_tests$use_bench" != "nonononono"; then
  PKG_CHECK_MODULES([EVENT], [libevent >= 2.1.8], [use_libevent=yes], [AC_MSG_ERROR([libevent version 2.1.8 or greater not found.])])
  AS_CASE([$host_os], [*mingw*], [], [
    PKG_CHECK_MODULES([EVENT_PTHREADS], [libevent_pthreads >= 2.1.8], [], [AC_MSG_ERROR([libevent_pthreads version 2.1.8 or greater not found.])])
  ])

  if test "$suppress_external_warnings" != "no"; then
    EVENT_CFLAGS=SUPPRESS_WARNINGS($EVENT_CFLAGS)
  fi
fi

if test "$use_libevent" = "yes"; then
  TEMP_CXXFLAGS="$CXXFLAGS"
  CXXFLAGS="$CXXFLAGS $EVENT_CFLAGS"
  AC_MSG_CHECKING([if evhttp_connection_get_peer expects const char**])
  AC_COMPILE_IFELSE([AC_LANG_PROGRAM([[
      #include <cstdint>
      #include <event2/http.h>
    ]], [[
      evhttp_connection* conn = (evhttp_connection *)1;
      const char* host;
      uint16_t port;

      evhttp_connection_get_peer(conn, &host, &port);
    ]])],
    [ AC_MSG_RESULT([yes]); AC_DEFINE([HAVE_EVHTTP_CONNECTION_GET_PEER_CONST_CHAR], [1], [Define this symbol if evhttp_connection_get_peer expects const char**]) ],
    [ AC_MSG_RESULT([no]) ]
  )
  CXXFLAGS="$TEMP_CXXFLAGS"
fi

dnl QR Code encoding library check
if test "$use_qr" != "no"; then
  BITCOIN_QT_CHECK([PKG_CHECK_MODULES([QR], [libqrencode], [have_qrencode=yes], [have_qrencode=no])])
  BITCOIN_QT_CHECK([AC_CHECK_HEADER([qrencode.h],, [have_qrencode=no])])
  BITCOIN_QT_CHECK([AC_CHECK_LIB([qrencode], [main], [QR_LIBS=-lqrencode], [have_qrencode=no])])
fi

dnl ZMQ check

if test "$use_zmq" = "yes"; then
  PKG_CHECK_MODULES([ZMQ], [libzmq >= 4],
    AC_DEFINE([ENABLE_ZMQ], [1], [Define to 1 to enable ZMQ functions]),
    [AC_DEFINE([ENABLE_ZMQ], [0], [Define to 1 to enable ZMQ functions])
    AC_MSG_WARN([libzmq version 4.x or greater not found, disabling])
    use_zmq=no])
else
  AC_DEFINE_UNQUOTED([ENABLE_ZMQ], [0], [Define to 1 to enable ZMQ functions])
fi

if test "$use_zmq" = "yes"; then
  dnl Assume libzmq was build for static linking
  AS_CASE([$host], [*mingw*], [ZMQ_CFLAGS="$ZMQ_CFLAGS -DZMQ_STATIC"])
fi

dnl Check for gmp
AC_CHECK_HEADER([gmp.h])
AC_CHECK_LIB([gmp], [__gmpz_init], [GMP_LIBS=-lgmp], [AC_MSG_ERROR([libgmp missing])])

dnl check for immer header
AC_CHECK_HEADER([immer/map.hpp],, [AC_MSG_ERROR([immer map header not found])])

AC_MSG_CHECKING([whether to build raptoreumd])
AM_CONDITIONAL([BUILD_BITCOIND], [test $build_bitcoind = "yes"])
AC_MSG_RESULT([$build_bitcoind])

AC_MSG_CHECKING([whether to build raptoreum-cli])
AM_CONDITIONAL([BUILD_BITCOIN_CLI], [test $build_bitcoin_cli = "yes"])
AC_MSG_RESULT([$build_bitcoin_cli])

AC_MSG_CHECKING([whether to build raptoreum-tx])
AM_CONDITIONAL([BUILD_BITCOIN_TX], [test $build_bitcoin_tx = "yes"])
AC_MSG_RESULT([$build_bitcoin_tx])

AC_MSG_CHECKING([whether to build raptoreum-wallet])
AM_CONDITIONAL([BUILD_BITCOIN_WALLET], [test $build_bitcoin_wallet = "yes"])
AC_MSG_RESULT($build_bitcoin_wallet)

AC_MSG_CHECKING([whether to build libraries])
AM_CONDITIONAL([BUILD_BITCOIN_LIBS], [test $build_bitcoin_libs = "yes"])
if test "$build_bitcoin_libs" = "yes"; then
  AC_DEFINE([HAVE_CONSENSUS_LIB], [1], [Define this symbol if the consensus lib has been built])
  AC_CONFIG_FILES([libraptoreumconsensus.pc:libraptoreumconsensus.pc.in])
fi
AC_MSG_RESULT($build_bitcoin_libs)

AC_LANG_POP

if test "$use_ccache" != "no"; then
  AC_MSG_CHECKING([if ccache should be used])
  if test "$CCACHE" = ""; then
    if test "$use_ccache" = "yes"; then
      AC_MSG_ERROR([ccache not found.]);
    else
      use_ccache=no
    fi
  else
    use_ccache=yes
    CC="$ac_cv_path_CCACHE $CC"
    CXX="$ac_cv_path_CCACHE $CXX"
  fi
  AC_MSG_RESULT($use_ccache)
  if test "$use_ccache" = "yes"; then
    AX_CHECK_COMPILE_FLAG([-fdebug-prefix-map=A=B], [DEBUG_CXXFLAGS="$DEBUG_CXXFLAGS -fdebug-prefix-map=\$(abs_top_srcdir)=."], [], [$CXXFLAG_WERROR])
    AX_CHECK_PREPROC_FLAG([-fmacro-prefix-map=A=B], [DEBUG_CPPFLAGS="$DEBUG_CPPFLAGS -fmacro-prefix-map=\$(abs_top_srcdir)=."], [], [$CXXFLAG_WERROR])
  fi
fi

dnl enable wallet
AC_MSG_CHECKING([if wallet should be enabled])
if test "$enable_wallet" != "no"; then
  AC_MSG_RESULT([yes])
  AC_DEFINE([ENABLE_WALLET], [1], [Define to 1 to enable wallet functions])
  enable_wallet=yes

else
  AC_MSG_RESULT([no])
fi

dnl enable upnp support
AC_MSG_CHECKING([whether to build with support for UPnP])
if test "$have_miniupnpc" = "no"; then
  if test "$use_upnp" = "yes"; then
     AC_MSG_ERROR([UPnP requested but cannot be built. Use --without-miniupnpc])
  fi
  AC_MSG_RESULT([no])
  use_upnp=no
else
  if test "$use_upnp" != "no"; then
    AC_MSG_RESULT([yes])
    AC_MSG_CHECKING([whether to build with UPnP enabled by default])
    use_upnp=yes
    upnp_setting=0
    if test "$use_upnp_default" != "no"; then
      use_upnp_default=yes
      upnp_setting=1
    fi
    AC_MSG_RESULT([$use_upnp_default])
    AC_DEFINE_UNQUOTED([USE_UPNP], [$upnp_setting], [UPnP support not compiled if undefined, otherwise value (0 or 1) determines default state])
    AS_CASE([$host_os], [*mingw*], [MINIUPNPC_CPPFLAGS="$MINIUPNPC_CPPFLAGS -DSTATICLIB -DMINIUPNP_STATICLIB"])
  else
    AC_MSG_RESULT([no])
  fi
fi

dnl Enable NAT-PMP support
AC_MSG_CHECKING([whether to build with support for NAT-PMP])
if test "$have_natpmp" = "yes"; then
  if test "$use_natpmp" = "yes"; then
    AC_MSG_ERROR([NAT-PMP requested but can not be built. Use --without-natpmp])
  fi
  AC_MSG_RESULT([no])
  use_natpmp=no
else
  if test "$use_natpmp" != "no"l; then
    AC_MSG_RESULT([yes])
    AC_MSG_CHECKING([whether to build with NAT-PMP enabled by default])
    use_natpmp=yes
    natpmp_setting=0
    if test "$use_natpmp_default" != "no"; then
      use_natpmp_default=yes
      natpmp_setting=1
    fi
    AC_MSG_RESULT([$use_natpmp_default])
    AC_DEFINE_UNQUOTED([USE_NATPMP], [$natpmp_setting], [NAT-PMP support not compiled if undefined, otherwise value (0 or 1) determine default state])
    AS_CASE([$host_os], [*mingw*], [NATPMP_CPPFLAGS="$NATPMP_CPPFLAGS -DSTATICLIB -DNATPMP_STATICLIB"])
  else
    AC_MSG_RESULT([no])
  fi
fi

dnl these are only used when qt is enabled
BUILD_TEST_QT=""
if test "$bitcoin_enable_qt" != "no"; then
  dnl enable dbus support
  AC_MSG_CHECKING([whether to build GUI with support for D-Bus])
  if test "$bitcoin_enable_qt_dbus" != "no"; then
    AC_DEFINE([USE_DBUS], [1], [Define if dbus support should be compiled in])
  fi
  AC_MSG_RESULT([$bitcoin_enable_qt_dbus])

  dnl enable qr support
  AC_MSG_CHECKING([whether to build GUI with support for QR codes])
  if test "$have_qrencode" = "no"; then
    if test "$use_qr" = "yes"; then
     AC_MSG_ERROR([QR support requested but cannot be built. use --without-qrencode])
    fi
    use_qr=no
  else
    if test "$use_qr" != "no"; then
      AC_DEFINE([USE_QRCODE], [1], [Define if QR support should be compiled in])
      use_qr=yes
    fi
  fi
  AC_MSG_RESULT([$use_qr])

  if test "$XGETTEXT" = ""; then
    AC_MSG_WARN([xgettext is required to update qt translations])
  fi

  AC_MSG_CHECKING([whether to build test_raptoreum-qt])
  if test "$use_gui_tests$bitcoin_enable_qt_test" = "yesyes"; then
    AC_MSG_RESULT([yes])
    BUILD_TEST_QT="yes"
  else
    AC_MSG_RESULT([no])
  fi
fi

AM_CONDITIONAL([ENABLE_ZMQ], [test "$use_zmq" = "yes"])

AC_MSG_CHECKING([whether to build test_raptoreum])
if test "$use_tests" = "yes"; then
  AC_MSG_RESULT([yes])
  BUILD_TEST="yes"
else
  AC_MSG_RESULT([no])
  BUILD_TEST=""
fi

AC_MSG_CHECKING([whether to reduce exports])
if test "$use_reduce_exports" = "yes"; then
  AC_MSG_RESULT([yes])
else
  AC_MSG_RESULT([no])
fi

# dnl When compiled natively on MacOS, we need to specify -flat to avoid producing a dSYM bundle.
# dnl When cross-compiled on linux, we are using a differewnt version of the tool that only support flat symbol files.
AC_MSG_CHECKING([whether dsymutil needs -flat])
if test "$DSYMUTIL" != "" && ($DSYMUTIL --help | grep -q \\-flat); then
  AC_MSG_RESULT([yes])
  DSYMUTIL_FLAT="$DSYMUTIL -flat"
else
  AC_MSG_RESULT([no])
  DSYMUTIL_FLAT="$DSYMUTIL"
fi
AC_MSG_RESULT([$dsymutil_needs_flat])

if test "$build_bitcoin_wallet$build_bitcoin_cli$build_bitcoin_tx$build_bitcoin_libs$build_bitcoind$bitcoin_enable_qt$use_bench$use_tests" = "nononononononono"; then
  AC_MSG_ERROR([No targets! Please specify at least one of: --with-utils --with-libs --with-daemon --with-gui --enable-bench or --enable-tests])
fi

AM_CONDITIONAL([TARGET_DARWIN], [AS_CASE([$host_os], [*darwin*], [true], [false])])
AM_CONDITIONAL([BUILD_DARWIN], [AS_CASE([$build_os], [*darwin*], [true], [false])])
AM_CONDITIONAL([TARGET_LINUX], [AS_CASE([$host_os], [*linux*], [true], [false])])
AM_CONDITIONAL([TARGET_WINDOWS], [AS_CASE([$host_os], [*mingw*], [true], [false])])
AM_CONDITIONAL([ENABLE_WALLET], [test "$enable_wallet" = "yes"])
AM_CONDITIONAL([USE_BDB], [test "$use_bdb" = "yes"])
AM_CONDITIONAL([ENABLE_TESTS], [test "$BUILD_TEST" = "yes"])
AM_CONDITIONAL([ENABLE_QT], [test "$bitcoin_enable_qt" = "yes"])
AM_CONDITIONAL([ENABLE_QT_TESTS], [test "$BUILD_TEST_QT" = "yes"])
AM_CONDITIONAL([ENABLE_BENCH], [test "$use_bench" = "yes"])
AM_CONDITIONAL([USE_QRCODE], [test "$use_qr" = "yes"])
AM_CONDITIONAL([USE_LCOV], [test "$use_lcov" = "yes"])
AM_CONDITIONAL([USE_LIBEVENT], [test "$use_libevent" = "yes"])
AM_CONDITIONAL([HARDEN], [test "$use_hardening" = "yes"])
AM_CONDITIONAL([ENABLE_SSE42], [test "$enable_sse42" = "yes"])
AM_CONDITIONAL([ENABLE_SSE41], [test "$enable_sse41" = "yes"])
AM_CONDITIONAL([ENABLE_AVX2], [test "$enable_avx2" = "yes"])
AM_CONDITIONAL([ENABLE_X86_SHANI], [test "$enable_x86_shani" = "yes"])
AM_CONDITIONAL([ENABLE_ARM_CRC], [test "$enable_arm_crc" = "yes"])
AM_CONDITIONAL([ENABLE_ARM_SHANI], [test "$enable_arm_shani" = "yes"])
AM_CONDITIONAL([USE_ASM], [test "$use_asm" = "yes"])
AM_CONDITIONAL([WORDS_BIGENDIAN], [test "$ac_cv_c_bigendian" = "yes"])
AM_CONDITIONAL([USE_NATPMP], [test "$use_natpmp" = "yes"])
AM_CONDITIONAL([USE_UPNP], [test "$use_upnp" = "yes"])

dnl for minisketch
AM_CONDITIONAL([ENABLE_CLMUL], [test "$enable_clmul" = "yes"])
AM_CONDITIONAL([HAVE_CLZ], [test "$have_clzl$have_clzll" = "yesyes"])

AC_DEFINE([CLIENT_VERSION_MAJOR], [_CLIENT_VERSION_MAJOR], [Major version])
AC_DEFINE([CLIENT_VERSION_MINOR], [_CLIENT_VERSION_MINOR], [Minor version])
AC_DEFINE([CLIENT_VERSION_REVISION], [_CLIENT_VERSION_REVISION], [Build revision])
AC_DEFINE([CLIENT_VERSION_BUILD], [_CLIENT_VERSION_BUILD], [Version Build])
AC_DEFINE([CLIENT_VERSION_IS_RELEASE], [_CLIENT_VERSION_IS_RELEASE], [Version is release])
AC_DEFINE([COPYRIGHT_YEAR], [_COPYRIGHT_YEAR], [Copyright year])
AC_DEFINE([COPYRIGHT_HOLDERS], ["_COPYRIGHT_HOLDERS"], [Copyright holder(s) before %s replacement])
AC_DEFINE([COPYRIGHT_HOLDERS_SUBSTITUTION], ["_COPYRIGHT_HOLDERS_SUBSTITUTION"], [Replacement for %s in copyright holders string])
define(_COPYRIGHT_HOLDERS_FINAL, [patsubst(_COPYRIGHT_HOLDERS, [%s], [_COPYRIGHT_HOLDERS_SUBSTITUTION])])
AC_DEFINE([COPYRIGHT_HOLDERS_FINAL], ["_COPYRIGHT_HOLDERS_FINAL"], [Copyright holder(s)])
AC_SUBST(CLIENT_VERSION_MAJOR, _CLIENT_VERSION_MAJOR)
AC_SUBST(CLIENT_VERSION_MINOR, _CLIENT_VERSION_MINOR)
AC_SUBST(CLIENT_VERSION_REVISION, _CLIENT_VERSION_REVISION)
AC_SUBST(CLIENT_VERSION_BUILD, _CLIENT_VERSION_BUILD)
AC_SUBST(CLIENT_VERSION_IS_RELEASE, _CLIENT_VERSION_IS_RELEASE)
AC_SUBST(COPYRIGHT_YEAR, _COPYRIGHT_YEAR)
AC_SUBST(COPYRIGHT_HOLDERS, "_COPYRIGHT_HOLDERS")
AC_SUBST(COPYRIGHT_HOLDERS_SUBSTITUTION, "_COPYRIGHT_HOLDERS_SUBSTITUTION")
AC_SUBST(COPYRIGHT_HOLDERS_FINAL, "_COPYRIGHT_HOLDERS_FINAL")
AC_SUBST(BITCOIN_DAEMON_NAME)
AC_SUBST(BITCOIN_GUI_NAME)
AC_SUBST(BITCOIN_CLI_NAME)
AC_SUBST(BITCOIN_TX_NAME)
AC_SUBST(BITCOIN_WALLET_TOOL_NAME)

AC_SUBST(RELDFLAGS)
AC_SUBST(CORE_LDFLAGS)
AC_SUBST(CORE_CPPFLAGS)
AC_SUBST(CORE_CXXFLAGS)
AC_SUBST(DEBUG_CPPFLAGS)
AC_SUBST(WARN_CXXFLAGS)
AC_SUBST(NOWARN_CXXFLAGS)
AC_SUBST(DEBUG_CXXFLAGS)
AC_SUBST(ERROR_CXXFLAGS)
AC_SUBST(GPROF_CXXFLAGS)
AC_SUBST(GPROF_LDFLAGS)
AC_SUBST(HARDENED_CXXFLAGS)
AC_SUBST(HARDENED_CPPFLAGS)
AC_SUBST(HARDENED_LDFLAGS)
AC_SUBST(LTO_CXXFLAGS)
AC_SUBST(LTO_LDFLAGS)
AC_SUBST(PIC_FLAGS)
AC_SUBST(PIE_FLAGS)
AC_SUBST(SANITIZER_CXXFLAGS)
AC_SUBST(SANITIZER_LDFLAGS)
AC_SUBST(SSE42_CXXFLAGS)
AC_SUBST(SSE41_CXXFLAGS)
AC_SUBST(AVX2_CXXFLAGS)
AC_SUBST(X86_SHANI_CXXFLAGS)
AC_SUBST(ARM_CRC_CXXFLAGS)
AC_SUBST(ARM_SHANI_CXXFLAGS)
AC_SUBST(LIBTOOL_APP_LDFLAGS)
AC_SUBST(USE_BDB)
AC_SUBST(USE_UPNP)
AC_SUBST(USE_QRCODE)
AC_SUBST(BOOST_LIBS)
AC_SUBST(TESTDEFS)
AC_SUBST(MINIUPNPC_CPPFLAGS)
AC_SUBST(MINIUPNPC_LIBS)
AC_SUBST(NATPMP_CPPFLAGS)
AC_SUBST(NATPMP_LIBS)
AC_SUBST(GMP_LIBS)
AC_SUBST(DSYMUTIL_FLAT)
AC_SUBST(HAVE_GMTIME_R)
AC_SUBST(HAVE_FDATASYNC)
AC_SUBST(HAVE_FULLFSYNC)
AC_SUBST(HAVE_O_CLOEXEC)
AC_SUBST(HAVE_BUILTIN_PREFETCH)
AC_SUBST(HAVE_MM_PREFETCH)
AC_SUBST(HAVE_STRONG_GETAUXVAL)
AC_SUBST(HAVE_EVHTTP_CONNECTION_GET_PEER_CONST_CHAR)
AC_SUBST(ANDROID_ARCH)
AC_CONFIG_FILES([Makefile src/Makefile doc/man/Makefile share/setup.nsi share/qt/Info.plist test/config.ini])
AC_CONFIG_FILES([contrib/devtools/split-debug.sh],[chmod +x contrib/devtools/split-debug.sh])
AM_COND_IF([HAVE_DOXYGEN], [AC_CONFIG_FILES([doc/Doxyfile])])
AC_CONFIG_LINKS([contrib/filter-lcov.py:contrib/filter-lcov.py])
AC_CONFIG_LINKS([contrib/macdeploy/background.tiff:contrib/macdeploy/background.tiff])
AC_CONFIG_LINKS([test/functional/test_runner.py:test/functional/test_runner.py])
AC_CONFIG_LINKS([test/util/bitcoin-util-test.py:test/util/bitcoin-util-test.py])
AC_CONFIG_LINKS([test/util/rpcauth-test.py:test/util/rpcauth-test.py])

dnl boost's m4 checks do something really nasty: they export these vars. As a
dnl result, they leak into secp256k1's configure and crazy things happen.
dnl Until this is fixed upstream and we've synced, we'll just un-export them.
CPPFLAGS_TEMP="$CPPFLAGS"
unset CPPFLAGS
CPPFLAGS="$CPPFLAGS_TEMP"

LDFLAGS_TEMP="$LDFLAGS"
unset LDFLAGS
LDFLAGS="$LDFLAGS_TEMP"

LIBS_TEMP="$LIBS"
unset LIBS
LIBS="$LIBS_TEMP"

ac_configure_args="${ac_configure_args} --disable-shared --with-pic --enable-benchmark=no --enable-module-recovery --enable-module-schnorrsig"
AC_CONFIG_SUBDIRS([src/secp256k1])

AC_OUTPUT

dnl Replace the BUILDDIR path with the correct Windows path if compiling on Native Windows
case ${OS} in
   *Windows*)
     sed  's/BUILDDIR="\/\([[a-z]]\)/BUILDDIR="\1:/'  test/config.ini > test/config-2.ini
     mv test/config-2.ini test/config.ini
   ;;
esac

echo
echo "Options used to compile and link:"
echo "  with libs           = $build_bitcoin_libs"
echo "  with wallet         = $enable_wallet"
if test "$enable_wallet" != "no"; then
    echo "    with bdb          = $use_bdb"
fi
echo "  with gui / qt       = $bitcoin_enable_qt"
if test $bitcoin_enable_qt != "no"; then
    echo "    with qr           = $use_qr"
fi
echo "  with zmq            = $use_zmq"
echo "  with test           = $use_tests"
echo "  with bench          = $use_bench"
echo "  with upnp           = $use_upnp"
echo "  with natpmp         = $use_natpmp"
echo "  use asm             = $use_asm"
echo "  sanitizers          = $use_sanitizers"
echo "  debug enabled       = $enable_debug"
echo "  stacktraces enabled = $enable_stacktraces"
echo "  crash hooks enabled = $enable_crashhooks"
echo "  miner enabled       = $enable_miner"
echo "  gprof enabled       = $enable_gprof"
echo "  werror              = $enable_werror"
echo "  LTO                 = $enable_lto"
echo
echo "  target os           = $host_os"
echo "  build os            = $build_os"
echo
echo "  CC                  = $CC"
echo "  CFLAGS              = $PTHREAD_CFLAGS $CFLAGS"
echo "  CPPFLAGS            = $DEBUG_CPPFLAGS $HARDENED_CPPFLAGS $CORE_CPPFLAGS $CPPFLAGS"
echo "  CXX                 = $CXX"
echo "  CXXFLAGS            = $LTO_CXXFLAGS $DEBUG_CXXFLAGS $HARDENED_CXXFLAGS $WARN_CXXFLAGS $NOWARN_CXXFLAGS $ERROR_CXXFLAGS $GPROF_CXXFLAGS $CORE_CXXFLAGS $CXXFLAGS"
echo "  LDFLAGS             = $LTO_LDFLAGS $PTHREAD_LIBS $HARDENED_LDFLAGS $GPROF_LDFLAGS $CORE_LDFLAGS $LDFLAGS"
echo "  ARFLAGS             = $ARFLAGS"
echo
