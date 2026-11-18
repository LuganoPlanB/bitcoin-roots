# Exact public plan entry points; artifacts stay in private build directories.
set(ROOTS_GUI_TEST_ROOTS_DAEMON "" CACHE FILEPATH "Explicit Roots regtest daemon binary; no autodiscovery")
set(ROOTS_GUI_TEST_CORE_DAEMON "" CACHE FILEPATH "Explicit compatible Core regtest daemon binary; no autodiscovery")
add_test(NAME roots_gui.rpc.fake_http COMMAND "${Python3_EXECUTABLE}" -B
  "${CMAKE_CURRENT_SOURCE_DIR}/run_fixture.py" --kind rpc --output "${CMAKE_CURRENT_BINARY_DIR}/fixture-runs")
set_tests_properties(roots_gui.rpc.fake_http PROPERTIES TIMEOUT 30 LABELS "roots-gui;rpc;roots-gui.rpc")
add_test(NAME roots_gui.rpc.scratch COMMAND "${Python3_EXECUTABLE}" -B
  "${CMAKE_CURRENT_SOURCE_DIR}/support/test_runtime.py")
set_tests_properties(roots_gui.rpc.scratch PROPERTIES TIMEOUT 30 LABELS "roots-gui;rpc;roots-gui.rpc")
add_executable(roots_gui_rpc_client rpc/transport_client.cpp)
target_link_libraries(roots_gui_rpc_client PRIVATE roots_gui_core)
add_executable(roots_gui_rpc_json rpc/json_tests.cpp)
target_link_libraries(roots_gui_rpc_json PRIVATE roots_gui_core)
add_test(NAME roots_gui.rpc.json COMMAND roots_gui_rpc_json)
add_test(NAME roots_gui.rpc.transport COMMAND "${Python3_EXECUTABLE}" -B
  "${CMAKE_CURRENT_SOURCE_DIR}/rpc/transport_tests.py" --client "$<TARGET_FILE:roots_gui_rpc_client>"
  --output "${CMAKE_CURRENT_BINARY_DIR}/transport-runs")
set_tests_properties(roots_gui.rpc.json roots_gui.rpc.transport PROPERTIES TIMEOUT 60 LABELS "roots-gui;rpc;roots-gui.rpc")
set(daemon_args)
foreach(kind ROOTS CORE)
  if(ROOTS_GUI_TEST_${kind}_DAEMON)
    if(NOT EXISTS "${ROOTS_GUI_TEST_${kind}_DAEMON}")
      message(FATAL_ERROR "Explicit ${kind} daemon does not exist")
    endif()
    string(TOLOWER "${kind}" kind_lower)
    list(APPEND daemon_args "--${kind_lower}-daemon" "${ROOTS_GUI_TEST_${kind}_DAEMON}")
  endif()
endforeach()
if(daemon_args)
  add_test(NAME roots_gui.regtest.owned COMMAND "${Python3_EXECUTABLE}" -B
    "${CMAKE_CURRENT_SOURCE_DIR}/run_fixture.py" --kind regtest --output "${CMAKE_CURRENT_BINARY_DIR}/fixture-runs" -- ${daemon_args})
  set_tests_properties(roots_gui.regtest.owned PROPERTIES TIMEOUT 180 RUN_SERIAL TRUE LABELS "roots-gui;regtest;roots-gui.regtest")
endif()
foreach(suite model rpc ui regtest visual boundary applicable)
  add_custom_target(roots_gui_${suite}_tests
    COMMAND "${Python3_EXECUTABLE}" -B "${CMAKE_CURRENT_SOURCE_DIR}/run_suite.py"
      --suite "${suite}" --build "${CMAKE_BINARY_DIR}" --output "${CMAKE_CURRENT_BINARY_DIR}/runs"
    USES_TERMINAL VERBATIM)
endforeach()
add_dependencies(roots_gui_rpc_tests roots_gui_rpc_client roots_gui_rpc_json)
add_executable(roots_gui_capability_client rpc/capability_client.cpp)
target_link_libraries(roots_gui_capability_client PRIVATE roots_gui_core)
add_executable(roots_gui_connection_models model/connection_tests.cpp)
target_link_libraries(roots_gui_connection_models PRIVATE roots_gui_core)
add_test(NAME roots_gui.connection.models COMMAND roots_gui_connection_models)
set(capability_daemon_args)
if(ROOTS_GUI_TEST_ROOTS_DAEMON)
  list(APPEND capability_daemon_args --daemon "${ROOTS_GUI_TEST_ROOTS_DAEMON}")
endif()
add_test(NAME roots_gui.rpc.capabilities COMMAND "${Python3_EXECUTABLE}" -B
  "${CMAKE_CURRENT_SOURCE_DIR}/rpc/capability_tests.py" --client "$<TARGET_FILE:roots_gui_capability_client>"
  --output "${CMAKE_CURRENT_BINARY_DIR}/capability-runs" ${capability_daemon_args})
set_tests_properties(roots_gui.connection.models roots_gui.rpc.capabilities PROPERTIES TIMEOUT 60 LABELS "roots-gui;rpc;roots-gui.rpc")
set_tests_properties(roots_gui.connection.models PROPERTIES LABELS "roots-gui;model;rpc;roots-gui.model;roots-gui.rpc")
add_dependencies(roots_gui_rpc_tests roots_gui_capability_client roots_gui_connection_models)
add_dependencies(roots_gui_model_tests roots_gui_connection_models)
add_executable(roots_gui_refresh_models model/refresh_tests.cpp)
target_link_libraries(roots_gui_refresh_models PRIVATE roots_gui_core)
add_test(NAME roots_gui.refresh.models COMMAND roots_gui_refresh_models)
set_tests_properties(roots_gui.refresh.models PROPERTIES TIMEOUT 30 LABELS "roots-gui;model;rpc;roots-gui.model;roots-gui.rpc")
add_dependencies(roots_gui_model_tests roots_gui_refresh_models)
add_executable(roots_gui_history_models model/history_tests.cpp)
target_link_libraries(roots_gui_history_models PRIVATE roots_gui_core)
add_test(NAME roots_gui.history.model COMMAND roots_gui_history_models)
set_tests_properties(roots_gui.history.model PROPERTIES TIMEOUT 30 LABELS "roots-gui;model;roots-gui.model")
add_dependencies(roots_gui_model_tests roots_gui_history_models)
add_executable(roots_gui_history_client rpc/history_client.cpp)
target_link_libraries(roots_gui_history_client PRIVATE roots_gui_core)
add_dependencies(roots_gui_rpc_tests roots_gui_history_client)
add_executable(roots_gui_history_bounds rpc/history_bounds.cpp)
target_link_libraries(roots_gui_history_bounds PRIVATE roots_gui_core)
add_test(NAME roots_gui.rpc.history_bounds COMMAND "${Python3_EXECUTABLE}" -B
  "${CMAKE_CURRENT_SOURCE_DIR}/rpc/history_bounds.py" --binary "$<TARGET_FILE:roots_gui_history_bounds>"
  --output "${CMAKE_CURRENT_BINARY_DIR}/history-bounds")
set_tests_properties(roots_gui.rpc.history_bounds PROPERTIES TIMEOUT 20 LABELS "roots-gui;rpc;roots-gui.rpc")
add_dependencies(roots_gui_rpc_tests roots_gui_history_bounds)
if(ROOTS_GUI_TEST_ROOTS_DAEMON)
  add_test(NAME roots_gui.rpc.history COMMAND "${Python3_EXECUTABLE}" -B
    "${CMAKE_CURRENT_SOURCE_DIR}/rpc/history_tests.py" --binary "$<TARGET_FILE:roots_gui_history_client>"
    --daemon "${ROOTS_GUI_TEST_ROOTS_DAEMON}" --output "${CMAKE_CURRENT_BINARY_DIR}/history-runs")
  set_tests_properties(roots_gui.rpc.history PROPERTIES TIMEOUT 180 RUN_SERIAL TRUE LABELS "roots-gui;rpc;regtest;roots-gui.rpc;roots-gui.regtest")
  add_dependencies(roots_gui_regtest_tests roots_gui_history_client)
endif()
add_executable(roots_gui_refresh_client rpc/refresh_client.cpp)
target_link_libraries(roots_gui_refresh_client PRIVATE roots_gui_core)
add_test(NAME roots_gui.rpc.refresh COMMAND "${Python3_EXECUTABLE}" -B
  "${CMAKE_CURRENT_SOURCE_DIR}/rpc/refresh_tests.py" --client "$<TARGET_FILE:roots_gui_refresh_client>"
  --output "${CMAKE_CURRENT_BINARY_DIR}/refresh-runs" ${capability_daemon_args})
set_tests_properties(roots_gui.rpc.refresh PROPERTIES TIMEOUT 60 LABELS "roots-gui;rpc;roots-gui.rpc")
add_dependencies(roots_gui_rpc_tests roots_gui_refresh_client roots_gui_refresh_models)
add_executable(roots_gui_store_tests storage/store_tests.cpp)
add_executable(roots_gui_protected_tests storage/protected_tests.cpp)
target_link_libraries(roots_gui_protected_tests PRIVATE roots_gui_ui roots_gui_core)
add_test(NAME roots_gui.storage.protected COMMAND "${Python3_EXECUTABLE}" -B
  "${CMAKE_CURRENT_SOURCE_DIR}/storage/protected_runner.py" --binary "$<TARGET_FILE:roots_gui_protected_tests>"
  --output "${CMAKE_CURRENT_BINARY_DIR}/protected-runs")
set_tests_properties(roots_gui.storage.protected PROPERTIES TIMEOUT 90 LABELS "roots-gui;model;storage;roots-gui.model")
if(CMAKE_SYSTEM_NAME STREQUAL "Linux")
  add_executable(roots_gui_conversion_tests storage/conversion_tests.cpp)
  target_link_libraries(roots_gui_conversion_tests PRIVATE roots_gui_ui roots_gui_core roots_gui_sqlite)
  add_library(roots_gui_conversion_stop MODULE storage/conversion_stop.c)
  target_link_libraries(roots_gui_conversion_stop PRIVATE ${CMAKE_DL_LIBS})
  add_test(NAME roots_gui.storage.conversion COMMAND "${Python3_EXECUTABLE}" -B
    "${CMAKE_CURRENT_SOURCE_DIR}/storage/conversion_runner.py" --binary "$<TARGET_FILE:roots_gui_conversion_tests>"
    --stop-library "$<TARGET_FILE:roots_gui_conversion_stop>" --output "${CMAKE_CURRENT_BINARY_DIR}/conversion-runs")
  set_tests_properties(roots_gui.storage.conversion PROPERTIES TIMEOUT 120 LABELS "roots-gui;model;storage;roots-gui.model")
endif()
add_executable(roots_gui_recovery_tests storage/recovery_tests.cpp)
target_link_libraries(roots_gui_recovery_tests PRIVATE roots_gui_ui roots_gui_core)
add_test(NAME roots_gui.storage.recovery COMMAND "${Python3_EXECUTABLE}" -B
  "${CMAKE_CURRENT_SOURCE_DIR}/storage/recovery_runner.py" --binary "$<TARGET_FILE:roots_gui_recovery_tests>"
  --output "${CMAKE_CURRENT_BINARY_DIR}/recovery-runs")
set_tests_properties(roots_gui.storage.recovery PROPERTIES TIMEOUT 60 LABELS "roots-gui;model;storage;roots-gui.model")
target_link_libraries(roots_gui_store_tests PRIVATE roots_gui_core roots_gui_sqlite)
add_test(NAME roots_gui.storage.schema COMMAND "${Python3_EXECUTABLE}" -B
  "${CMAKE_CURRENT_SOURCE_DIR}/storage/store_runner.py" --binary "$<TARGET_FILE:roots_gui_store_tests>"
  --output "${CMAKE_CURRENT_BINARY_DIR}/store-runs")
set_tests_properties(roots_gui.storage.schema PROPERTIES TIMEOUT 60 LABELS "roots-gui;model;storage;roots-gui.model")
add_dependencies(roots_gui_model_tests roots_gui_store_tests)
add_dependencies(roots_gui_model_tests roots_gui_protected_tests roots_gui_recovery_tests)
if(TARGET roots_gui_conversion_tests)
  add_dependencies(roots_gui_model_tests roots_gui_conversion_tests roots_gui_conversion_stop)
endif()
add_dependencies(roots_gui_model_tests roots_gui_models roots_gui_preference_models)
add_dependencies(roots_gui_ui_tests roots_gui_engine_tests roots_gui_imkit_theme roots_gui_imkit_design_system
  roots_gui_imkit_workflow roots_gui_imkit_charts roots_gui_imkit_api_compile roots_gui_imkit_design_api_compile roots_gui_imkit_charts_api_compile roots_gui_theme_tests)

# External inspected baselines and optional parent matrices are explicit inputs.
set(ROOTS_GUI_VISUAL_BASELINE "" CACHE PATH "Inspected external native baseline directory")
set(boundary_args)
foreach(profile PARENT_OFF PARENT_ON PARENT_QT_OFF PARENT_QT_ON)
  set(ROOTS_GUI_BOUNDARY_${profile} "" CACHE PATH "Optional existing boundary matrix build directory")
  if(ROOTS_GUI_BOUNDARY_${profile})
    string(TOLOWER "${profile}" flag)
    string(REPLACE "_" "-" flag "${flag}")
    list(APPEND boundary_args "--${flag}" "${ROOTS_GUI_BOUNDARY_${profile}}")
  endif()
endforeach()
add_test(NAME roots_gui.boundary.actual COMMAND "${Python3_EXECUTABLE}" -B
  "${CMAKE_CURRENT_SOURCE_DIR}/boundary/run_boundary.py" --build "${CMAKE_BINARY_DIR}"
  --output "${CMAKE_CURRENT_BINARY_DIR}/boundary-runs" ${boundary_args})
add_test(NAME roots_gui.boundary.rejections COMMAND "${Python3_EXECUTABLE}" -B
  "${CMAKE_CURRENT_SOURCE_DIR}/boundary/rejections.py" --output "${CMAKE_CURRENT_BINARY_DIR}/boundary-runs")
set_tests_properties(roots_gui.boundary.actual roots_gui.boundary.rejections PROPERTIES TIMEOUT 90 LABELS "roots-gui;boundary;roots-gui.boundary")
if(CMAKE_SYSTEM_NAME STREQUAL "Linux")
  foreach(kind gallery appearance components shell shell_appearance documents commands semantics)
    if(kind STREQUAL "gallery")
      set(theme_binary roots_gui_theme_gallery)
    elseif(kind STREQUAL "appearance")
      set(theme_binary roots_gui_native_appearance)
    elseif(kind STREQUAL "shell_appearance")
      set(theme_binary roots_gui_shell_appearance)
    elseif(kind STREQUAL "semantics")
      set(theme_binary roots_gui_semantic_capture)
    elseif(kind STREQUAL "commands")
      set(theme_binary roots_gui_command_capture)
    elseif(kind STREQUAL "documents")
      set(theme_binary roots_gui_document_capture)
    elseif(kind STREQUAL "shell")
      set(theme_binary roots_gui_shell_capture)
    else()
      set(theme_binary roots_gui_component_gallery)
    endif()
    add_test(NAME roots_gui.theme.native_${kind} COMMAND "${Python3_EXECUTABLE}" -B
      "${CMAKE_CURRENT_SOURCE_DIR}/theme/native_runner.py" --kind "${kind}" --binary "$<TARGET_FILE:${theme_binary}>"
      --fonts "$<TARGET_FILE_DIR:roots-gui>/roots-gui-assets/fonts" --output "${CMAKE_CURRENT_BINARY_DIR}/theme-native")
    set_tests_properties(roots_gui.theme.native_${kind} PROPERTIES TIMEOUT 180 RUN_SERIAL TRUE LABELS "roots-gui;theme;visual;roots-gui.visual")
    add_dependencies(roots_gui_visual_tests ${theme_binary})
  endforeach()
  set(baseline_args)
  if(ROOTS_GUI_VISUAL_BASELINE)
    list(APPEND baseline_args --baseline "${ROOTS_GUI_VISUAL_BASELINE}")
  endif()
  add_test(NAME roots_gui.visual.native COMMAND "${Python3_EXECUTABLE}" -B
    "${CMAKE_CURRENT_SOURCE_DIR}/visual/run_visual.py" --native-capture "$<TARGET_FILE:roots_gui_visual_capture>"
    --native-lifecycle "$<TARGET_FILE:roots_gui_native_lifecycle>" --fonts "$<TARGET_FILE_DIR:roots-gui>/roots-gui-assets/fonts"
    --output "${CMAKE_CURRENT_BINARY_DIR}/visual-runs" ${baseline_args})
  add_dependencies(roots_gui_visual_tests roots_gui_visual_capture roots_gui_native_lifecycle roots-gui)
else()
  add_test(NAME roots_gui.visual.native COMMAND "${Python3_EXECUTABLE}" -B
    "${CMAKE_CURRENT_SOURCE_DIR}/visual/run_visual.py" --native-capture unavailable --native-lifecycle unavailable
    --fonts unavailable --output "${CMAKE_CURRENT_BINARY_DIR}/visual-runs" --force-unavailable)
endif()

# Build prerequisites only: depending on another *_tests dispatcher could run
# tests while this target is still relinking their binaries.
add_dependencies(roots_gui_applicable_tests roots-gui roots_gui_engine_tests
  roots_gui_models roots_gui_preference_models roots_gui_theme_tests
  roots_gui_imkit_theme roots_gui_imkit_design_system roots_gui_imkit_workflow
  roots_gui_imkit_charts roots_gui_imkit_api_compile roots_gui_imkit_design_api_compile roots_gui_imkit_charts_api_compile
  roots_gui_rpc_client roots_gui_rpc_json roots_gui_capability_client
  roots_gui_connection_models roots_gui_refresh_models roots_gui_refresh_client
  roots_gui_history_models roots_gui_history_client roots_gui_history_bounds
  roots_gui_store_tests roots_gui_protected_tests roots_gui_recovery_tests
  roots_gui_history_workspace_tests roots_gui_history_states_tests
  roots_gui_address_models roots_gui_address_probe roots_gui_coin_models roots_gui_coin_control_models)
if(TARGET roots_gui_conversion_tests)
  add_dependencies(roots_gui_applicable_tests roots_gui_conversion_tests roots_gui_conversion_stop)
endif()
add_test(NAME roots_gui.visual.contracts COMMAND "${Python3_EXECUTABLE}" -B
  "${CMAKE_CURRENT_SOURCE_DIR}/visual/contracts.py" --baseline "${ROOTS_GUI_VISUAL_BASELINE}"
  --output "${CMAKE_CURRENT_BINARY_DIR}/visual-contract-runs")
set_tests_properties(roots_gui.visual.native roots_gui.visual.contracts PROPERTIES TIMEOUT 900 RUN_SERIAL TRUE LABELS "roots-gui;visual;roots-gui.visual")
add_dependencies(roots_gui_boundary_tests roots-gui roots_gui_engine_tests)

add_executable(roots_gui_history_workspace_tests model/history_workspace_tests.cpp)
target_link_libraries(roots_gui_history_workspace_tests PRIVATE roots_gui_ui roots_gui_core)
add_test(NAME roots_gui.history.workspace COMMAND "${Python3_EXECUTABLE}" -B
  "${CMAKE_CURRENT_SOURCE_DIR}/storage/single_store_runner.py" --binary "$<TARGET_FILE:roots_gui_history_workspace_tests>"
  --output "${CMAKE_CURRENT_BINARY_DIR}/history-workspace-runs")
set_tests_properties(roots_gui.history.workspace PROPERTIES TIMEOUT 30 LABELS "roots-gui;model;roots-gui.model")

add_dependencies(roots_gui_model_tests roots_gui_history_workspace_tests)
if(CMAKE_SYSTEM_NAME STREQUAL "Linux" AND ROOTS_GUI_TEST_ROOTS_DAEMON)
  add_test(NAME roots_gui.theme.history COMMAND "${Python3_EXECUTABLE}" -B
    "${CMAKE_CURRENT_SOURCE_DIR}/theme/history_runner.py" --binary "$<TARGET_FILE:roots_gui_history_capture>"
    --fonts "$<TARGET_FILE_DIR:roots-gui>/roots-gui-assets/fonts" --daemon "${ROOTS_GUI_TEST_ROOTS_DAEMON}"
    --output "${CMAKE_CURRENT_BINARY_DIR}/history-native")
  set_tests_properties(roots_gui.theme.history PROPERTIES TIMEOUT 90 RUN_SERIAL TRUE LABELS "roots-gui;ui;regtest;roots-gui.ui;roots-gui.regtest")
  add_dependencies(roots_gui_ui_tests roots_gui_history_capture)
endif()

add_executable(roots_gui_history_states_tests model/history_states_tests.cpp)
target_link_libraries(roots_gui_history_states_tests PRIVATE roots_gui_ui roots_gui_core)
add_test(NAME roots_gui.history.states COMMAND roots_gui_history_states_tests)
set_tests_properties(roots_gui.history.states PROPERTIES TIMEOUT 30 LABELS "roots-gui;model;roots-gui.model")
add_dependencies(roots_gui_model_tests roots_gui_history_states_tests)

add_executable(roots_gui_address_models model/address_tests.cpp)
target_link_libraries(roots_gui_address_models PRIVATE roots_gui_ui roots_gui_core)
add_test(NAME roots_gui.address.models COMMAND roots_gui_address_models)
set_tests_properties(roots_gui.address.models PROPERTIES TIMEOUT 30 LABELS "roots-gui;model;roots-gui.model")
add_dependencies(roots_gui_model_tests roots_gui_address_models)

add_executable(roots_gui_address_probe rpc/address_probe.cpp)
target_link_libraries(roots_gui_address_probe PRIVATE roots_gui_core)
add_test(NAME roots_gui.rpc.addresses COMMAND "${Python3_EXECUTABLE}" -B
  "${CMAKE_CURRENT_SOURCE_DIR}/rpc/address_tests.py" --binary "$<TARGET_FILE:roots_gui_address_probe>"
  --output "${CMAKE_CURRENT_BINARY_DIR}/address-rpc")
set_tests_properties(roots_gui.rpc.addresses PROPERTIES TIMEOUT 45 LABELS "roots-gui;rpc;roots-gui.rpc")
add_dependencies(roots_gui_rpc_tests roots_gui_address_probe)

if(CMAKE_SYSTEM_NAME STREQUAL "Linux" AND ROOTS_GUI_TEST_ROOTS_DAEMON)
  add_test(NAME roots_gui.theme.addresses COMMAND "${Python3_EXECUTABLE}" -B
    "${CMAKE_CURRENT_SOURCE_DIR}/theme/history_runner.py" --addresses --binary "$<TARGET_FILE:roots_gui_address_capture>"
    --fonts "$<TARGET_FILE_DIR:roots-gui>/roots-gui-assets/fonts" --daemon "${ROOTS_GUI_TEST_ROOTS_DAEMON}"
    --output "${CMAKE_CURRENT_BINARY_DIR}/address-native")
  set_tests_properties(roots_gui.theme.addresses PROPERTIES TIMEOUT 90 RUN_SERIAL TRUE LABELS "roots-gui;ui;regtest;roots-gui.ui;roots-gui.regtest")
  add_dependencies(roots_gui_ui_tests roots_gui_address_capture)
endif()

add_executable(roots_gui_coin_models model/coin_tests.cpp)
target_link_libraries(roots_gui_coin_models PRIVATE roots_gui_ui roots_gui_core)
add_test(NAME roots_gui.coin.models COMMAND roots_gui_coin_models)
set_tests_properties(roots_gui.coin.models PROPERTIES TIMEOUT 30 LABELS "roots-gui;model;roots-gui.model")
add_dependencies(roots_gui_model_tests roots_gui_coin_models)

if(CMAKE_SYSTEM_NAME STREQUAL "Linux" AND ROOTS_GUI_TEST_ROOTS_DAEMON)
  add_test(NAME roots_gui.theme.coins COMMAND "${Python3_EXECUTABLE}" -B
    "${CMAKE_CURRENT_SOURCE_DIR}/theme/history_runner.py" --coins --binary "$<TARGET_FILE:roots_gui_coin_capture>"
    --fonts "$<TARGET_FILE_DIR:roots-gui>/roots-gui-assets/fonts" --daemon "${ROOTS_GUI_TEST_ROOTS_DAEMON}"
    --output "${CMAKE_CURRENT_BINARY_DIR}/coin-native")
  set_tests_properties(roots_gui.theme.coins PROPERTIES TIMEOUT 90 RUN_SERIAL TRUE LABELS "roots-gui;ui;regtest;roots-gui.ui;roots-gui.regtest")
  add_dependencies(roots_gui_ui_tests roots_gui_coin_capture)
endif()

add_executable(roots_gui_coin_control_models model/coin_controls_tests.cpp)
target_link_libraries(roots_gui_coin_control_models PRIVATE roots_gui_ui roots_gui_core)
add_test(NAME roots_gui.coin.controls COMMAND roots_gui_coin_control_models)
set_tests_properties(roots_gui.coin.controls PROPERTIES TIMEOUT 45 LABELS "roots-gui;model;roots-gui.model")
add_dependencies(roots_gui_model_tests roots_gui_coin_control_models)

if(CMAKE_SYSTEM_NAME STREQUAL "Linux" AND ROOTS_GUI_TEST_ROOTS_DAEMON)
  add_test(NAME roots_gui.theme.coin_controls COMMAND "${Python3_EXECUTABLE}" -B
    "${CMAKE_CURRENT_SOURCE_DIR}/theme/history_runner.py" --controls --binary "$<TARGET_FILE:roots_gui_coin_control_capture>"
    --fonts "$<TARGET_FILE_DIR:roots-gui>/roots-gui-assets/fonts" --daemon "${ROOTS_GUI_TEST_ROOTS_DAEMON}"
    --output "${CMAKE_CURRENT_BINARY_DIR}/coin-control-native")
  set_tests_properties(roots_gui.theme.coin_controls PROPERTIES TIMEOUT 90 RUN_SERIAL TRUE LABELS "roots-gui;ui;regtest;roots-gui.ui;roots-gui.regtest")
  add_dependencies(roots_gui_ui_tests roots_gui_coin_control_capture)
endif()

if(CMAKE_SYSTEM_NAME STREQUAL "Linux")
  add_dependencies(roots_gui_ui_tests roots_gui_recovery_capture roots_gui_lock_capture roots_gui_refresh_capture)
  if(ROOTS_GUI_TEST_ROOTS_DAEMON)
    add_dependencies(roots_gui_regtest_tests roots_gui_history_capture roots_gui_address_capture roots_gui_coin_capture roots_gui_coin_control_capture)
  endif()
endif()
