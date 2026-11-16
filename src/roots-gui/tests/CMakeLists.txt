# Exact public plan entry points; artifacts stay in private build directories.
set(ROOTS_GUI_TEST_ROOTS_DAEMON "" CACHE FILEPATH "Explicit Roots regtest daemon binary; no autodiscovery")
set(ROOTS_GUI_TEST_CORE_DAEMON "" CACHE FILEPATH "Explicit compatible Core regtest daemon binary; no autodiscovery")
add_test(NAME roots_gui.rpc.fake_http COMMAND "${Python3_EXECUTABLE}" -B
  "${CMAKE_CURRENT_SOURCE_DIR}/run_fixture.py" --kind rpc --output "${CMAKE_CURRENT_BINARY_DIR}/fixture-runs")
set_tests_properties(roots_gui.rpc.fake_http PROPERTIES TIMEOUT 30 LABELS "roots-gui;rpc;roots-gui.rpc")
add_test(NAME roots_gui.rpc.scratch COMMAND "${Python3_EXECUTABLE}" -B
  "${CMAKE_CURRENT_SOURCE_DIR}/support/test_runtime.py")
set_tests_properties(roots_gui.rpc.scratch PROPERTIES TIMEOUT 30 LABELS "roots-gui;rpc;roots-gui.rpc")
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
foreach(suite model rpc ui regtest visual boundary)
  add_custom_target(roots_gui_${suite}_tests
    COMMAND "${Python3_EXECUTABLE}" -B "${CMAKE_CURRENT_SOURCE_DIR}/run_suite.py"
      --suite "${suite}" --build "${CMAKE_BINARY_DIR}" --output "${CMAKE_CURRENT_BINARY_DIR}/runs"
    USES_TERMINAL VERBATIM)
endforeach()
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
add_test(NAME roots_gui.visual.contracts COMMAND "${Python3_EXECUTABLE}" -B
  "${CMAKE_CURRENT_SOURCE_DIR}/visual/contracts.py" --baseline "${ROOTS_GUI_VISUAL_BASELINE}"
  --output "${CMAKE_CURRENT_BINARY_DIR}/visual-contract-runs")
set_tests_properties(roots_gui.visual.native roots_gui.visual.contracts PROPERTIES TIMEOUT 900 RUN_SERIAL TRUE LABELS "roots-gui;visual;roots-gui.visual")
add_dependencies(roots_gui_boundary_tests roots-gui)
