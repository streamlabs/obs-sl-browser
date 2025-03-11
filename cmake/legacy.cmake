cmake_minimum_required(VERSION 3.16...3.25)

find_package(CEF REQUIRED)
find_package(ZLIB REQUIRED)
find_package(Detours REQUIRED)

# removed: find_qt(COMPONENTS Widgets)
# this is untested, I've disabled legacy building completely, code is from plugin template
find_package(Qt6 COMPONENTS Widgets Core)
  target_link_libraries(${CMAKE_PROJECT_NAME} PRIVATE Qt6::Core Qt6::Widgets)
  target_compile_options(
    ${CMAKE_PROJECT_NAME}
    PRIVATE $<$<C_COMPILER_ID:Clang,AppleClang>:-Wno-quoted-include-in-framework-header -Wno-comma>
  )
  set_target_properties(
    ${CMAKE_PROJECT_NAME}
    PROPERTIES AUTOMOC ON AUTOUIC ON AUTORCC ON
)

include(FindGRPC.cmake)

## -- Grpc between plugin and proxy

# Proto file
get_filename_component(papi_proto "sl_browser_api.proto" ABSOLUTE)
get_filename_component(papi_proto_path "${papi_proto}" PATH)

message ("${CMAKE_CURRENT_BINARY_DIR}")

# Generated sources
set(papi_proto_srcs "${CMAKE_CURRENT_BINARY_DIR}/sl_browser_api.pb.cc")
set(papi_proto_hdrs "${CMAKE_CURRENT_BINARY_DIR}/sl_browser_api.pb.h")
set(papi_grpc_srcs "${CMAKE_CURRENT_BINARY_DIR}/sl_browser_api.grpc.pb.cc")
set(papi_grpc_hdrs "${CMAKE_CURRENT_BINARY_DIR}/sl_browser_api.grpc.pb.h")

add_custom_command(
      OUTPUT "${papi_proto_srcs}" "${papi_proto_hdrs}" "${papi_grpc_srcs}" "${papi_grpc_hdrs}"
      COMMAND ${CMAKE_COMMAND} -E echo "Running protoc"
      COMMAND ${_PROTOBUF_PROTOC}
      ARGS --grpc_out "${CMAKE_CURRENT_BINARY_DIR}"
        --cpp_out "${CMAKE_CURRENT_BINARY_DIR}"
        -I "${papi_proto_path}"
        --plugin=protoc-gen-grpc="${_GRPC_CPP_PLUGIN_EXECUTABLE}"
        "${papi_proto}"
      DEPENDS "${papi_proto}")

include_directories("${CMAKE_CURRENT_BINARY_DIR}")

# papi_grpc_proto
add_library(papi_grpc_proto
    ${papi_grpc_srcs}
    ${papi_grpc_hdrs}
    ${papi_proto_srcs}
    ${papi_proto_hdrs})
target_link_libraries(papi_grpc_proto
    ${_REFLECTION}
    ${_GRPC_GRPCPP}
    ${_PROTOBUF_LIBPROTOBUF})

## -- Browser Proxy Process

add_executable(sl-browser WIN32)

#target_compile_definitions(sl-browser PRIVATE SL_OBS_VERSION="")
#target_compile_definitions(sl-browser PRIVATE GITHUB_REVISION="")
#target_compile_definitions(sl-browser PRIVATE SL_REVISION="")
target_compile_options(sl-browser PRIVATE $<IF:$<CONFIG:DEBUG>,/MTd,/MT>)
target_link_options(sl-browser PRIVATE /NODEFAULTLIB:msvcrt)

set_target_properties(sl-browser PROPERTIES LINK_FLAGS_DEBUG "/NODEFAULTLIB:msvcrtd.lib")

if (CMAKE_BUILD_TYPE MATCHES Debug)
  target_link_options(sl-browser PRIVATE /NODEFAULTLIB:msvcrtd.lib)
endif()

target_sources(
  sl-browser
  PRIVATE main.cpp
          GrpcBrowser.cpp
          SlBrowser.cpp
          SlBrowserWidget.cpp
          browser-app.cpp
          browser-app.hpp
          browser-client.cpp
          browser-client.hpp
          browser-scheme.cpp
          browser-scheme.hpp
          browser-version.h
          cef-headers.hpp
          deps/json11/json11.cpp
          deps/json11/json11.hpp
          deps/base64/base64.cpp
          deps/base64/base64.hpp
          deps/signal-restore.cpp
          deps/signal-restore.hpp
		      ${papi_proto_srcs}
		      ${papi_grpc_srcs})

target_include_directories(sl-browser PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}/deps")

target_compile_features(sl-browser PRIVATE cxx_std_17)

target_link_libraries(sl-browser PRIVATE CEF::Wrapper CEF::Library d3d11 dxgi)
target_link_libraries(sl-browser PRIVATE Qt::Widgets Qt::Core Qt::Gui)
target_link_libraries(sl-browser PRIVATE Detours::Detours)

target_link_libraries(sl-browser PRIVATE
	  papi_grpc_proto
	  ${_REFLECTION}
	  ${_GRPC_GRPCPP}
	  ${_PROTOBUF_LIBPROTOBUF})

target_link_options(sl-browser PRIVATE
  "/IGNORE:4099" # Ignore PDB warnings
  "/IGNORE:4098"
)

target_compile_options(sl-browser PRIVATE $<IF:$<CONFIG:DEBUG>,/MTd,/MT>)
target_compile_definitions(sl-browser PRIVATE NOMINMAX)

add_custom_command(
  TARGET sl-browser POST_BUILD
  COMMAND ${CMAKE_COMMAND} -E copy
          ${CMAKE_CURRENT_SOURCE_DIR}/streamlabs-app-icon.png
          $<TARGET_FILE_DIR:sl-browser>/streamlabs-app-icon.png
)

## -- Plugin loaded by obs

add_library(sl-browser-plugin MODULE)

target_sources(
  sl-browser-plugin
  PRIVATE sl-browser-plugin.cpp
    GrpcPlugin.cpp
    PluginJsHandler.cpp
    QtGuiModifications.cpp
    WebServer.cpp
    WebServer.cpp
    SlBrowserDock.cpp
    deps/json11/json11.cpp
    deps/minizip/ioapi.c
    deps/minizip/iowin32.c
    deps/minizip/mztools.c
    deps/minizip/unzip.c
    deps/minizip/zip.c
    ${papi_proto_srcs}
    ${papi_grpc_srcs}
)

target_link_libraries(sl-browser-plugin PRIVATE Detours::Detours)
target_link_libraries(sl-browser-plugin PRIVATE ZLIB::ZLIB)
target_link_libraries(sl-browser-plugin PRIVATE Qt::Widgets)
target_link_libraries(sl-browser-plugin PRIVATE Qt::Core)
target_link_libraries(sl-browser-plugin PRIVATE Qt::Gui)
target_link_libraries(sl-browser-plugin PRIVATE CEF::Wrapper CEF::Library) # The plugin uses ->setURL etc files that are CEF based

target_include_directories(sl-browser-plugin PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}/deps")
target_include_directories(sl-browser-plugin PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}/deps/json11")
target_include_directories(sl-browser-plugin PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}/../obs-browser/panel")
target_include_directories(sl-browser-plugin PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}/UI")

target_compile_features(sl-browser-plugin PRIVATE cxx_std_17)
target_compile_definitions(sl-browser-plugin PRIVATE BROWSER_AVAILABLE)
#target_compile_definitions(sl-browser-plugin PRIVATE SL_OBS_VERSION="")
#target_compile_definitions(sl-browser-plugin PRIVATE GITHUB_REVISION="")
#target_compile_definitions(sl-browser-plugin PRIVATE SL_REVISION="")

target_link_libraries(sl-browser-plugin PRIVATE OBS::libobs OBS::frontend-api)
target_link_libraries(sl-browser-plugin PRIVATE ZLIB::ZLIB)

target_link_libraries(sl-browser-plugin PRIVATE
  papi_grpc_proto
  ${_REFLECTION}
  ${_GRPC_GRPCPP}
  ${_PROTOBUF_LIBPROTOBUF})

target_link_options(sl-browser-plugin PRIVATE
  "/IGNORE:4099" # Ignore PDB warnings
  "/IGNORE:4098"
)
target_compile_options(sl-browser-plugin PRIVATE
  "/wd4996" # 'obs_frontend_add_dock': was declared deprecated
)

target_compile_options(sl-browser-plugin PRIVATE $<IF:$<CONFIG:DEBUG>,/MTd,/MT>)
target_compile_definitions(sl-browser-plugin PRIVATE NOMINMAX)

## -- Helper proxy children

add_executable(sl-browser-page)

target_sources(sl-browser-page PRIVATE cef-headers.hpp sl-browser-page/sl-browser-page-main.cpp browser-app.cpp
                                        browser-app.hpp deps/json11/json11.cpp deps/json11/json11.hpp)

target_link_libraries(sl-browser-page PRIVATE CEF::Library)

target_include_directories(sl-browser-page PRIVATE ${CMAKE_CURRENT_SOURCE_DIR} ${CMAKE_CURRENT_SOURCE_DIR}/deps
                                                    ${CMAKE_CURRENT_SOURCE_DIR}/sl-browser-page)

target_sources(sl-browser-page PRIVATE sl-browser-page.manifest)

target_compile_features(sl-browser-page PRIVATE cxx_std_17)

if(TARGET CEF::Wrapper_Debug)
  target_link_libraries(sl-browser-page PRIVATE optimized CEF::Wrapper)
  target_link_libraries(sl-browser-page PRIVATE debug CEF::Wrapper_Debug)
else()
  target_link_libraries(sl-browser-page PRIVATE CEF::Wrapper)
endif()

target_compile_definitions(sl-browser-page PRIVATE ENABLE_BROWSER_SHARED_TEXTURE)
target_compile_options(sl-browser-page PRIVATE $<IF:$<CONFIG:DEBUG>,/MTd,/MT>)
target_link_options(sl-browser-page PRIVATE "LINKER:/IGNORE:4099" "LINKER:/SUBSYSTEM:WINDOWS")
