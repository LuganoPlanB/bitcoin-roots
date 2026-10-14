# Copyright (c) 2025-present The Bitcoin Roots developers
# Distributed under the MIT software license, see the accompanying
# file COPYING or https://opensource.org/license/mit/.

# Adapted from the Roots system-library checks for Core v31's MuSig API.
find_package(PkgConfig REQUIRED)
# Refresh this project's pkg-config prefix when an operator changes package
# search paths in an existing build. Symbol probes alone would otherwise keep
# checking the previously cached headers and library.
unset(__pkg_config_checked_libsecp256k1 CACHE)
get_cmake_property(secp_cache_variables CACHE_VARIABLES)
foreach(cache_variable IN LISTS secp_cache_variables)
  if(cache_variable MATCHES "^pkgcfg_lib_libsecp256k1_")
    unset(${cache_variable} CACHE)
  endif()
endforeach()
unset(secp_cache_variables)
unset(cache_variable)
pkg_check_modules(libsecp256k1 REQUIRED IMPORTED_TARGET libsecp256k1)

include(CheckCXXSymbolExists)
include(CMakePushCheckState)
cmake_push_check_state(RESET)
set(CMAKE_REQUIRED_LIBRARIES PkgConfig::libsecp256k1)
set(missing_symbols)
macro(check_secp256k1_symbol header symbol)
  string(TOUPPER "${symbol}" check_name)
  set(check_name "libsecp256k1_HAS_${check_name}")
  # Recheck after the selected package changes in an existing build directory.
  unset(${check_name} CACHE)
  check_cxx_symbol_exists(${symbol} ${header} ${check_name})
  if(NOT ${check_name})
    list(APPEND missing_symbols ${symbol})
  endif()
endmacro()
check_secp256k1_symbol(secp256k1_ellswift.h secp256k1_ellswift_create)
check_secp256k1_symbol(secp256k1_extrakeys.h secp256k1_xonly_pubkey_parse)
check_secp256k1_symbol(secp256k1_recovery.h secp256k1_ecdsa_recover)
check_secp256k1_symbol(secp256k1_schnorrsig.h secp256k1_schnorrsig_verify)
check_secp256k1_symbol(secp256k1_musig.h secp256k1_musig_pubkey_agg)
check_secp256k1_symbol(secp256k1_musig.h secp256k1_musig_pubkey_get)
cmake_pop_check_state()
if(missing_symbols)
  list(JOIN missing_symbols ", " missing_symbols)
  message(FATAL_ERROR "System libsecp256k1 lacks required v31 APIs: ${missing_symbols}.")
endif()
add_library(secp256k1 ALIAS PkgConfig::libsecp256k1)
