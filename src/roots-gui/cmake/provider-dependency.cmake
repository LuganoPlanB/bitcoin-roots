# The provider owns generic secp256k1 targets and top-level build assumptions.
# Keep its CMake process, cache, flags and source staging outside the parent graph.
include(ExternalProject)
function(roots_gui_add_provider source_dir)
  set(provider_binary "${CMAKE_CURRENT_BINARY_DIR}/provider-build")
  set(provider_archive "${provider_binary}/src/${CMAKE_STATIC_LIBRARY_PREFIX}wallycore${CMAKE_STATIC_LIBRARY_SUFFIX}")
  set(secp_archive "${provider_binary}/src/secp256k1/lib/${CMAKE_STATIC_LIBRARY_PREFIX}secp256k1${CMAKE_STATIC_LIBRARY_SUFFIX}")
  ExternalProject_Add(roots_gui_provider_build
    SOURCE_DIR "${source_dir}"
    BINARY_DIR "${provider_binary}"
    DOWNLOAD_COMMAND "" UPDATE_COMMAND "" INSTALL_COMMAND "" TEST_COMMAND ""
    CMAKE_ARGS
      "-DCMAKE_BUILD_TYPE=${CMAKE_BUILD_TYPE}"
      "-DCMAKE_C_COMPILER=${CMAKE_C_COMPILER}"
      "-DCMAKE_CXX_COMPILER=${CMAKE_CXX_COMPILER}"
      "-DCMAKE_C_COMPILER_LAUNCHER=${CMAKE_C_COMPILER_LAUNCHER}"
      "-DCMAKE_CXX_COMPILER_LAUNCHER=${CMAKE_CXX_COMPILER_LAUNCHER}"
      "-DCMAKE_TOOLCHAIN_FILE=${CMAKE_TOOLCHAIN_FILE}"
      -DBUILD_SHARED_LIBS=OFF -DWALLYCORE_BUILD_ELEMENTS=OFF
      -DWALLYCORE_ENABLE_TESTS=OFF -DWALLYCORE_INSTALL=OFF
    BUILD_COMMAND "${CMAKE_COMMAND}" --build <BINARY_DIR> --parallel 2 --config "$<CONFIG>"
    BUILD_BYPRODUCTS "${provider_archive}" "${secp_archive}"
    LOG_CONFIGURE ON LOG_BUILD ON LOG_OUTPUT_ON_FAILURE ON)
  add_library(roots_gui_wally STATIC IMPORTED GLOBAL)
  set_target_properties(roots_gui_wally PROPERTIES IMPORTED_LOCATION "${provider_archive}"
    INTERFACE_INCLUDE_DIRECTORIES "${source_dir}/include")
  add_library(roots_gui_provider_secp STATIC IMPORTED GLOBAL)
  set_target_properties(roots_gui_provider_secp PROPERTIES IMPORTED_LOCATION "${secp_archive}")
  add_dependencies(roots_gui_wally roots_gui_provider_build)
  add_dependencies(roots_gui_provider_secp roots_gui_provider_build)
  add_library(roots_gui_protocol INTERFACE)
  target_link_libraries(roots_gui_protocol INTERFACE roots_gui_wally roots_gui_provider_secp)
endfunction()
