include(FetchContent)

# Pick the ONNX Runtime version to use
set(Onnxruntime_VERSION "1.17.1")
set(Onnxruntime_WINDOWS_VERSION "v${Onnxruntime_VERSION}-1")
set(Onnxruntime_BASEURL "https://github.com/occ-ai/occ-ai-dep-onnxruntime-static-win/releases/download/${Onnxruntime_WINDOWS_VERSION}")

# Default to Release; switch if Debug
if(CMAKE_BUILD_TYPE STREQUAL "Debug")
  set(Onnxruntime_BUILD_TYPE "Debug")
  # (If you also host a Debug archive, swap the URL/hash here)
else()
  set(Onnxruntime_BUILD_TYPE "Release")
endif()

set(Onnxruntime_URL  "${Onnxruntime_BASEURL}/onnxruntime-windows-${Onnxruntime_WINDOWS_VERSION}-Release.zip")
set(Onnxruntime_HASH SHA256=39E63850D9762810161AE1B4DEAE5E3C02363521273E4B894A9D9707AB626C38)

FetchContent_Declare(
  onnxruntime
  URL      ${Onnxruntime_URL}
  URL_HASH ${Onnxruntime_HASH}
)
FetchContent_MakeAvailable(onnxruntime)

# Define an INTERFACE target `Ort` you can link against
add_library(Ort INTERFACE)

# Core ONNX Runtime libs
set(Onnxruntime_LIB_NAMES
    session;providers_shared;providers_dml;optimizer;providers;
    framework;graph;util;mlas;common;flatbuffers
)
foreach(lib_name IN LISTS Onnxruntime_LIB_NAMES)
  add_library(Ort::${lib_name} STATIC IMPORTED)
  set_target_properties(Ort::${lib_name} PROPERTIES
    IMPORTED_LOCATION ${onnxruntime_SOURCE_DIR}/lib/onnxruntime_${lib_name}.lib
    INTERFACE_INCLUDE_DIRECTORIES ${onnxruntime_SOURCE_DIR}/include
  )
  target_link_libraries(Ort INTERFACE Ort::${lib_name})
endforeach()

# External deps ONNX ships with
set(Onnxruntime_EXTERNAL_LIB_NAMES
    onnx;onnx_proto;libprotobuf-lite;re2;
    absl_throw_delegate;absl_hash;absl_city;
    absl_low_level_hash;absl_raw_hash_set
)
foreach(lib_name IN LISTS Onnxruntime_EXTERNAL_LIB_NAMES)
  add_library(Ort::${lib_name} STATIC IMPORTED)
  set_target_properties(Ort::${lib_name} PROPERTIES
    IMPORTED_LOCATION ${onnxruntime_SOURCE_DIR}/lib/${lib_name}.lib
  )
  target_link_libraries(Ort INTERFACE Ort::${lib_name})
endforeach()

# DirectML (optional GPU acceleration)
add_library(Ort::DirectML SHARED IMPORTED)
set_target_properties(Ort::DirectML PROPERTIES
  IMPORTED_LOCATION ${onnxruntime_SOURCE_DIR}/bin/DirectML.dll
  IMPORTED_IMPLIB   ${onnxruntime_SOURCE_DIR}/bin/DirectML.lib
)
target_link_libraries(Ort INTERFACE Ort::DirectML d3d12.lib dxgi.lib dxguid.lib dxcore.lib)
