# sl-dual-output: compiled directly into sl-browser-plugin (single module).
# Include from the top-level CMakeLists.txt AFTER the sl-browser-plugin target:
#   include(${CMAKE_CURRENT_SOURCE_DIR}/sl-dual-output/sl-dual-output.cmake)

if(NOT TARGET sl-browser-plugin)
  message(FATAL_ERROR "sl-dual-output: include this after the sl-browser-plugin target is defined")
endif()

# ---- OBS version gate (obs.ver next to the including CMakeLists.txt) ----
set(_sl_dual_ver_file "${CMAKE_CURRENT_SOURCE_DIR}/obs.ver")
if(NOT EXISTS "${_sl_dual_ver_file}")
  message(FATAL_ERROR "sl-dual-output: obs.ver not found at ${_sl_dual_ver_file}")
endif()
set_property(DIRECTORY APPEND PROPERTY CMAKE_CONFIGURE_DEPENDS "${_sl_dual_ver_file}")

file(READ "${_sl_dual_ver_file}" SL_DUAL_OBS_VER_RAW)
string(STRIP "${SL_DUAL_OBS_VER_RAW}" SL_DUAL_OBS_VER_RAW)

# "31.1.2", "32.1.2-beta", "32.1" -> leading semver for comparisons
string(REGEX MATCH "^[0-9]+\\.[0-9]+(\\.[0-9]+)?" SL_DUAL_OBS_VERSION "${SL_DUAL_OBS_VER_RAW}")
if(NOT SL_DUAL_OBS_VERSION)
  message(FATAL_ERROR "sl-dual-output: could not parse obs.ver ('${SL_DUAL_OBS_VER_RAW}')")
endif()

# OBS 32.1.0 is the functional floor: earlier versions never save
# plugin-canvas scenes to the scene collection (the save loop over frontend
# canvases first shipped in 32.1.0), so dual output cannot persist on them.
# Below the floor the build still succeeds, but only the facade is compiled,
# as a no-op stub (SL_DUAL_ENABLED=0); initialize()/shutdown() do nothing.
if(SL_DUAL_OBS_VERSION VERSION_LESS "32.1.0")
  set(SL_DUAL_ENABLED 0)
  message(STATUS "sl-dual-output: OBS ${SL_DUAL_OBS_VER_RAW} < 32.1 cannot persist "
                 "plugin-canvas scenes; compiling dual output as a no-op stub")
else()
  set(SL_DUAL_ENABLED 1)
  message(STATUS "sl-dual-output: building against OBS ${SL_DUAL_OBS_VER_RAW}")
endif()

# ---- Sources ----
if(SL_DUAL_ENABLED)
  set(_sl_dual_sources
    "${CMAKE_CURRENT_LIST_DIR}/SlDualOutput.hpp"
    "${CMAKE_CURRENT_LIST_DIR}/SlDualOutput.cpp"
    "${CMAKE_CURRENT_LIST_DIR}/SlDualController.hpp"
    "${CMAKE_CURRENT_LIST_DIR}/SlDualController.cpp"
    "${CMAKE_CURRENT_LIST_DIR}/SlDualConfig.hpp"
    "${CMAKE_CURRENT_LIST_DIR}/SlDualCanvas.hpp"
    "${CMAKE_CURRENT_LIST_DIR}/SlDualCanvas.cpp"
    "${CMAKE_CURRENT_LIST_DIR}/SlDualEditor.hpp"
    "${CMAKE_CURRENT_LIST_DIR}/SlDualEditor.cpp"
    "${CMAKE_CURRENT_LIST_DIR}/SlDualUndo.hpp"
    "${CMAKE_CURRENT_LIST_DIR}/SlDualUndo.cpp"
    "${CMAKE_CURRENT_LIST_DIR}/SlDualSourceList.hpp"
    "${CMAKE_CURRENT_LIST_DIR}/SlDualSourceList.cpp"
    "${CMAKE_CURRENT_LIST_DIR}/SlDualStreamOutput.hpp"
    "${CMAKE_CURRENT_LIST_DIR}/SlDualStreamOutput.cpp"
    "${CMAKE_CURRENT_LIST_DIR}/SlDualDock.hpp"
    "${CMAKE_CURRENT_LIST_DIR}/SlDualDock.cpp"
    "${CMAKE_CURRENT_LIST_DIR}/SlDualScenesDock.hpp"
    "${CMAKE_CURRENT_LIST_DIR}/SlDualScenesDock.cpp"
    "${CMAKE_CURRENT_LIST_DIR}/SlDualSourcesDock.hpp"
    "${CMAKE_CURRENT_LIST_DIR}/SlDualSourcesDock.cpp"
    "${CMAKE_CURRENT_LIST_DIR}/SlDualToolbar.hpp"
    "${CMAKE_CURRENT_LIST_DIR}/SlDualSettingsDialog.hpp"
    "${CMAKE_CURRENT_LIST_DIR}/SlDualSettingsDialog.cpp"
  )
else()
  # Stub build: the facade only. Nothing else compiles, so OBS trees without
  # the canvas API build cleanly too.
  set(_sl_dual_sources
    "${CMAKE_CURRENT_LIST_DIR}/SlDualOutput.hpp"
    "${CMAKE_CURRENT_LIST_DIR}/SlDualOutput.cpp"
    "${CMAKE_CURRENT_LIST_DIR}/SlDualConfig.hpp"
  )
endif()

target_sources(sl-browser-plugin PRIVATE ${_sl_dual_sources})
source_group("sl-dual-output" FILES ${_sl_dual_sources})

target_include_directories(sl-browser-plugin PRIVATE "${CMAKE_CURRENT_LIST_DIR}")

target_compile_definitions(sl-browser-plugin PRIVATE
  SL_DUAL_ENABLED=${SL_DUAL_ENABLED}
  SL_DUAL_OBS_VERSION="${SL_DUAL_OBS_VERSION}"
  SL_DUAL_OBS_VERSION_RAW="${SL_DUAL_OBS_VER_RAW}"
)
