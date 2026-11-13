# Copyright (c) 2023-present The Bitcoin Core developers
# Distributed under the MIT software license, see the accompanying
# file COPYING or https://opensource.org/license/mit/.

# Do not use generator expressions in test sources because the
# SOURCES property is processed to gather test suite macros.
target_sources(test_bitcoin
  PRIVATE
    init_test_fixture.cpp
    wallet_test_fixture.cpp
    db_tests.cpp
    coinselector_tests.cpp
    coinselection_tests.cpp
    feebumper_tests.cpp
    group_outputs_tests.cpp
    init_tests.cpp
    ismine_tests.cpp
    legacy_wallet_export_tests.cpp
    psbt_wallet_tests.cpp
    scriptpubkeyman_tests.cpp
    spend_tests.cpp
    wallet_crypto_tests.cpp
    wallet_rpc_tests.cpp
    wallet_tests.cpp
    wallet_transaction_tests.cpp
    walletdb_tests.cpp
    walletload_tests.cpp
)
target_link_libraries(test_bitcoin bitcoin_wallet)

target_json_data_sources(test_bitcoin data/legacy_export.json)
# The consuming executable belongs to the parent directory. Attach this
# directory's generation rule explicitly before its compilation starts.
add_custom_target(legacy_export_test_data DEPENDS ${CMAKE_CURRENT_BINARY_DIR}/data/legacy_export.json.h)
add_dependencies(test_bitcoin legacy_export_test_data)
