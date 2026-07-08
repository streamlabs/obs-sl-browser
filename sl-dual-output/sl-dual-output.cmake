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

if(SL_DUAL_OBS_VERSION VERSION_LESS "31.1.0")
  message(FATAL_ERROR
    "sl-dual-output: OBS ${SL_DUAL_OBS_VERSION} < 31.1 has no canvas API (obs_canvas_*, "
    "obs_frontend_add_canvas); refusing to build")
endif()

# 32.0+ additionally surfaces registered canvases in the Twitch Enhanced
# Broadcasting settings UI. Same API either way; kept as a define for logging
# and any future gating.
if(SL_DUAL_OBS_VERSION VERSION_GREATER_EQUAL "32.0.0")
  set(SL_DUAL_HAS_FRONTEND_CANVAS 1)
else()
  set(SL_DUAL_HAS_FRONTEND_CANVAS 0)
endif()

message(STATUS "sl-dual-output: building against OBS ${SL_DUAL_OBS_VER_RAW} "
               "(EB canvas UI: ${SL_DUAL_HAS_FRONTEND_CANVAS})")

# ---- Sources ----
set(_sl_dual_sources
  "${CMAKE_CURRENT_LIST_DIR}/SlDualOutput.hpp"
  "${CMAKE_CURRENT_LIST_DIR}/SlDualOutput.cpp"
  "${CMAKE_CURRENT_LIST_DIR}/SlDualOutputInternal.hpp"
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
  "${CMAKE_CURRENT_LIST_DIR}/SlDualSettingsDialog.hpp"
  "${CMAKE_CURRENT_LIST_DIR}/SlDualSettingsDialog.cpp"
)

target_sources(sl-browser-plugin PRIVATE ${_sl_dual_sources})
source_group("sl-dual-output" FILES ${_sl_dual_sources})

target_include_directories(sl-browser-plugin PRIVATE "${CMAKE_CURRENT_LIST_DIR}")

target_compile_definitions(sl-browser-plugin PRIVATE
  SL_DUAL_OBS_VERSION="${SL_DUAL_OBS_VERSION}"
  SL_DUAL_OBS_VERSION_RAW="${SL_DUAL_OBS_VER_RAW}"
  SL_DUAL_HAS_FRONTEND_CANVAS=${SL_DUAL_HAS_FRONTEND_CANVAS}
)
