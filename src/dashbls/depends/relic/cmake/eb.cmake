message(STATUS "Binary elliptic curve arithmetic configuration (EB module):\n")

message("   ** Options for the binary elliptic curve module (default = on, w = 4):\n")

message("      EB_PLAIN=[off|on] Support for ordinary curves without endomorphisms.")
message("      EB_KBLTZ=[off|on] Support for Koblitz anomalous binary curves.")
message("      EB_MIXED=[off|on] Use mixed coordinates.")
message("      EB_PRECO=[off|on] Build precomputation table for generator.")
message("      EB_DEPTH=w        Width w in [2,8] of precomputation table for fixed point methods.")
message("      EB_WIDTH=w        Width w in [2,6] of window processing for unknown point methods.\n")

message("   ** Available binary elliptic curve methods (default = PROJC;LWNAF;COMBS;INTER):\n")

message("      Point representation:")
message("      EB_METHD=BASIC    Affine coordinates.")
message("      EB_METHD=PROJC    Projective coordinates (L�pez-Dahab for ordinary curves).\n")

message("      Variable-base scalar multiplication:")
message("      EB_METHD=BASIC    Binary double-and-add method.")
message("      EB_METHD=LODAH    Lopez-Dahab constant-time point multiplication.")
message("      EB_METHD=LWNAF    Left-to-right window (T)NAF method.")
message("      EB_METHD=RWNAF    Right-to-left window (T)NAF method.")
message("      EB_METHD=HALVE    Halving method.\n")

message("      Fixed-base scalar multiplication:")
message("      EB_METHD=BASIC    Binary method for fixed point multiplication.")
message("      EB_METHD=COMBS    Single-table Comb method for fixed point multiplication.")
message("      EB_METHD=COMBD    Double-table Comb method for fixed point multiplication.")
message("      EB_METHD=LWNAF    Left-to-right window (T)NAF method.\n")

message("      Variable-base simultaneous scalar multiplication:")
message("      EB_METHD=BASIC    Multiplication-and-addition simultaneous multiplication.")
message("      EB_METHD=TRICK    Shamir's trick for simultaneous multiplication.")
message("      EB_METHD=INTER    Interleaving of window (T)NAFs.")
message("      EB_METHD=JOINT    Joint sparse form.\n")

if (NOT EB_DEPTH)
	set(EB_DEPTH 4)
endif(NOT EB_DEPTH)
if (NOT EB_WIDTH)
	set(EB_WIDTH 4)
endif(NOT EB_WIDTH)
set(EB_DEPTH "${EB_DEPTH}" CACHE STRING "Width of precomputation table for fixed point methods.")
set(EB_WIDTH "${EB_WIDTH}" CACHE STRING "Width of window processing for unknown point methods.")

option(EB_PLAIN "Support for ordinary curves without endomorphisms" on)
option(EB_KBLTZ "Support for Koblitz anomalous binary curves" on)
option(EB_MIXED "Use mixed coordinates" on)
option(EB_PRECO "Build precomputation table for generator" on)

# Choose the arithmetic methods.
if (NOT EB_METHD)
	set(EB_METHD "PROJC;LWNAF;COMBS;INTER")
endif(NOT EB_METHD)
list(LENGTH EB_METHD EB_LEN)
if (EB_LEN LESS 4)
	message(FATAL_ERROR "Incomplete EB_METHD specification: ${EB_METHD}")
endif(EB_LEN LESS 4)

list(GET EB_METHD 0 EB_ADD)
list(GET EB_METHD 1 EB_MUL)
list(GET EB_METHD 2 EB_FIX)
list(GET EB_METHD 3 EB_SIM)
set(EB_METHD ${EB_METHD} CACHE STRING "Method for binary elliptic curve arithmetic.")
