# Copyright (c) 2025-present The Bitcoin Core developers
# Distributed under the MIT software license, see the accompanying
# file COPYING or https://opensource.org/license/mit/.

include(ts_files.cmake)

if(WITH_QT_VERSION STREQUAL "5")
  qt5_add_translation(qm_files ${ts_files} OPTIONS -silent)
  add_custom_target(bitcoinqt_lrelease DEPENDS ${qm_files})
elseif(Qt6_VERSION VERSION_GREATER_EQUAL 6.7)
  qt6_add_lrelease(TS_FILES ${ts_files} LRELEASE_TARGET bitcoinqt_lrelease OPTIONS -silent)
else()
  qt6_add_lrelease(bitcoinqt TS_FILES ${ts_files} OPTIONS -silent)
endif()

add_dependencies(bitcoinqt bitcoinqt_lrelease)
