cmake_minimum_required(VERSION 3.22)
project(RootsGuiProviderProbe LANGUAGES C CXX)
if(NOT CMAKE_SYSTEM_NAME STREQUAL "Linux")
  message(FATAL_ERROR "This offline provider probe requires Linux; other hosts remain unverified.")
endif()
set(ROOTS_GUI_PROVIDER_SOURCE "" CACHE PATH "Verified prepared provider source directory.")
set(ROOTS_GUI_PROVIDER_BUILD "" CACHE PATH "Completed provider build directory.")
if(NOT EXISTS "${ROOTS_GUI_PROVIDER_SOURCE}/include/wally_core.h" OR
   NOT EXISTS "${ROOTS_GUI_PROVIDER_BUILD}/src/libwallycore.a")
  message(FATAL_ERROR "Prepare and build the pinned local provider first.")
endif()
find_program(ROOTS_GUI_CCACHE ccache)
if(ROOTS_GUI_CCACHE)
  if(NOT CMAKE_C_COMPILER_LAUNCHER)
    set(CMAKE_C_COMPILER_LAUNCHER "${ROOTS_GUI_CCACHE}")
  endif()
  if(NOT CMAKE_CXX_COMPILER_LAUNCHER)
    set(CMAKE_CXX_COMPILER_LAUNCHER "${ROOTS_GUI_CCACHE}")
  endif()
endif()
add_executable(roots_gui_offline_exec offline_exec.c)
target_compile_features(roots_gui_offline_exec PRIVATE c_std_11)
add_executable(roots_gui_provider_probe provider_probe.cpp)
target_compile_features(roots_gui_provider_probe PRIVATE cxx_std_20)
target_compile_options(roots_gui_provider_probe PRIVATE -Wall -Wextra -Werror)
target_include_directories(roots_gui_provider_probe SYSTEM PRIVATE "${ROOTS_GUI_PROVIDER_SOURCE}/include")
target_link_libraries(roots_gui_provider_probe PRIVATE
  "${ROOTS_GUI_PROVIDER_BUILD}/src/libwallycore.a"
  "${ROOTS_GUI_PROVIDER_BUILD}/src/secp256k1/lib/libsecp256k1.a")
find_package(Python3 3.10 REQUIRED COMPONENTS Interpreter)
enable_testing()
add_test(NAME roots_gui_provider_contracts
  COMMAND roots_gui_offline_exec "${Python3_EXECUTABLE}" "${CMAKE_CURRENT_SOURCE_DIR}/check_provider.py"
    --probe "$<TARGET_FILE:roots_gui_provider_probe>"
    --provider-source "${ROOTS_GUI_PROVIDER_SOURCE}"
    --bips "${ROOTS_GUI_PROVIDER_SOURCE}/../bips-provider-vectors")
set_tests_properties(roots_gui_provider_contracts PROPERTIES LABELS "roots-gui.protocol" TIMEOUT 60)
