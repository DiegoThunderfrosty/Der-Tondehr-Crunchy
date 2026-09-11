cmake_minimum_required(VERSION 3.25)
project(DerTondehrCrunchy VERSION 0.10.24 LANGUAGES C CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
option(CRUNCHY_BUILD_PLUGIN "Build the Windows VST3 and standalone" ON)
option(CRUNCHY_BUILD_TESTS "Build the circuit validation tests" ON)
set(IPLUG_DEPLOY_PLUGINS OFF CACHE BOOL "Keep build outputs in this project")

# setup_dependencies.ps1 installs the compatible checkout under external/iPlug2.
# An explicit -DIPLUG2_DIR remains available for advanced local setups.
if(NOT DEFINED IPLUG2_DIR OR IPLUG2_DIR STREQUAL "")
  if(DEFINED ENV{IPLUG2_DIR} AND EXISTS "$ENV{IPLUG2_DIR}/iPlug2.cmake")
    set(IPLUG2_DIR "$ENV{IPLUG2_DIR}" CACHE PATH "iPlug2 checkout (with SDKs)" FORCE)
  elseif(EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/external/iPlug2/iPlug2.cmake")
    set(IPLUG2_DIR "${CMAKE_CURRENT_SOURCE_DIR}/external/iPlug2" CACHE PATH "iPlug2 checkout (with SDKs)" FORCE)
  else()
    set(IPLUG2_DIR "${CMAKE_CURRENT_SOURCE_DIR}/external/iPlug2" CACHE PATH "iPlug2 checkout (with SDKs)")
  endif()
endif()

if(CRUNCHY_BUILD_TESTS)
  enable_testing()
  add_executable(crunchy_control_tests tests/control_tests.cpp)
  target_include_directories(crunchy_control_tests PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}/DerTondehrCrunchy")
  if(MSVC)
    target_compile_options(crunchy_control_tests PRIVATE /EHsc)
  endif()
  add_test(NAME crunchy_front_panel_controls COMMAND crunchy_control_tests)

  add_executable(crunchy_pass9_regression_tests tests/pass9_regression_tests.cpp)
  target_include_directories(crunchy_pass9_regression_tests PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}/DerTondehrCrunchy")
  if(MSVC)
    target_compile_options(crunchy_pass9_regression_tests PRIVATE /EHsc)
  endif()
  add_test(NAME crunchy_pass9_regression COMMAND crunchy_pass9_regression_tests)

  # Full routing/state stress test. The two tiny headers under tests/stubs expose
  # only the HIIR API surface needed by CrunchyAmp.h; production builds continue
  # to use iPlug2's real oversampler implementation.
  add_executable(crunchy_dsp_stress_tests tests/dsp_tests.cpp)
  target_include_directories(crunchy_dsp_stress_tests PRIVATE
    "${CMAKE_CURRENT_SOURCE_DIR}/tests/stubs"
    "${CMAKE_CURRENT_SOURCE_DIR}/DerTondehrCrunchy")
  if(MSVC)
    target_compile_options(crunchy_dsp_stress_tests PRIVATE /EHsc)
    # The stress harness intentionally keeps several complete amp engines in
    # one process. Give this test a normal development-sized Windows stack.
    target_link_options(crunchy_dsp_stress_tests PRIVATE /STACK:8388608)
  endif()
  add_test(NAME crunchy_dsp_stress COMMAND crunchy_dsp_stress_tests)


  add_executable(crunchy_meter_tests tests/meter_tests.cpp)
  target_include_directories(crunchy_meter_tests PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}/DerTondehrCrunchy")
  if(MSVC)
    target_compile_options(crunchy_meter_tests PRIVATE /EHsc)
  endif()
  add_test(NAME crunchy_meter_bridge COMMAND crunchy_meter_tests)

  add_executable(crunchy_editor_scale_tests tests/editor_scale_tests.cpp)
  target_include_directories(crunchy_editor_scale_tests PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}/DerTondehrCrunchy")
  if(MSVC)
    target_compile_options(crunchy_editor_scale_tests PRIVATE /EHsc)
  endif()
  add_test(NAME crunchy_editor_scale COMMAND crunchy_editor_scale_tests)

  add_test(NAME crunchy_standalone_audio_policy
    COMMAND ${CMAKE_COMMAND} -P "${CMAKE_CURRENT_SOURCE_DIR}/scripts/validate_standalone_audio_patch.cmake")
endif()

if(CRUNCHY_BUILD_PLUGIN)
  if(NOT WIN32)
    message(FATAL_ERROR "Plugin packaging currently targets Windows. Use -DCRUNCHY_BUILD_PLUGIN=OFF to run the control tests elsewhere.")
  endif()
  if(NOT EXISTS "${IPLUG2_DIR}/iPlug2.cmake")
    message(FATAL_ERROR
      "iPlug2 was not found. Run scripts/setup_dependencies.ps1, set IPLUG2_DIR, "
      "or configure with -DIPLUG2_DIR=C:/path/to/iPlug2")
  endif()
  include("${IPLUG2_DIR}/iPlug2.cmake")
  find_package(iPlug2 REQUIRED)
  if(NOT IPLUG2_VST3_SUPPORTED)
    message(FATAL_ERROR "The selected iPlug2 checkout needs Dependencies/IPlug/VST3_SDK.")
  endif()
  add_subdirectory(DerTondehrCrunchy)
endif()
