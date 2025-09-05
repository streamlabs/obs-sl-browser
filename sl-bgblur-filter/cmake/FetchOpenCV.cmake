include(FetchContent)

# OpenCV release version and base URL
set(OpenCV_VERSION "v4.8.1-1")
set(OpenCV_BASEURL "https://github.com/obs-ai/obs-backgroundremoval-dep-opencv/releases/download/${OpenCV_VERSION}")

# Pick archive based on build type (default to Release if unset)
if(CMAKE_BUILD_TYPE STREQUAL "Debug")
  set(OpenCV_BUILD_TYPE "Debug")
  set(OpenCV_URL  "${OpenCV_BASEURL}/opencv-windows-${OpenCV_VERSION}-Debug.zip")
  set(OpenCV_HASH SHA256=0c5ef12cf4b4e4db7ea17a24db156165b6f01759f3f1660b069d0722e5d5dc37)
else()
  set(OpenCV_BUILD_TYPE "Release")
  set(OpenCV_URL  "${OpenCV_BASEURL}/opencv-windows-${OpenCV_VERSION}-Release.zip")
  set(OpenCV_HASH SHA256=5e468f71d41d3a3ea46cc4f247475877f65d3655a2764a2c01074bda3b3e6864)
endif()

# Fetch the prebuilt OpenCV package
FetchContent_Declare(
  opencv
  URL      ${OpenCV_URL}
  URL_HASH ${OpenCV_HASH}
)
FetchContent_MakeAvailable(opencv)

# Provide an interface target for linking
add_library(OpenCV INTERFACE)

target_link_libraries(OpenCV INTERFACE
    ${opencv_SOURCE_DIR}/x64/vc17/staticlib/opencv_imgproc481.lib
    ${opencv_SOURCE_DIR}/x64/vc17/staticlib/opencv_core481.lib
    ${opencv_SOURCE_DIR}/x64/vc17/staticlib/zlib.lib
)

target_include_directories(OpenCV SYSTEM INTERFACE
    ${opencv_SOURCE_DIR}/include
)
