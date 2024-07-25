# Building
Only building for Windows is currently possible.

## Windows

### Prerequisites
You will need to have the following installed:

TODO

### Example Build

1. Clone OBS-Studio

    git clone --recurse-submodules https://github.com/obsproject/obs-studio.git

2. Enter the obs-studio folder

    cd obs-studio

3. Choose the OBS-Studio version you want to build the plugin for (https://github.com/obsproject/obs-studio/tags)

    git checkout 30.1.2

4. Update submodules for the version

    git submodule update

5. Clone the plugin into the plugins subfolder

    git clone --recurse-submodules git@github.com:stream-labs/obs-sl-browser.git plugins\obs-sl-browser

6. Download and unpack the plugin dependencies (use the version from https://github.com/stream-labs/obs-sl-browser/blob/main/.github/workflows/main.yml)

    set GRPC_VERSION=v1.58.0
   
    start /WAIT /B /D %CD%\plugins\obs-sl-browser CI\install_deps.cmd

8. Set environment variables to specify dependency paths

    set PROTOBUF_DIR=%CD%\plugins\obs-sl-browser\grpc_dist\cmake
   
    set ABSL_DIR=%CD%\plugins\obs-sl-browser\grpc_dist\lib\cmake\absl
   
    set GRPC_DIR=%CD%\plugins\obs-sl-browser\grpc_dist\lib\cmake\grpc
   
    set UTF8_RANGE_DIR=%CD%\plugins\obs-sl-browser\grpc_dist\lib\cmake\utf8_range

9. Add the plugin in the OBS-Studio CMake script (do it manually if the example commands below do not work)

    powershell -command "(Get-Content plugins\CMakeLists.txt).Replace('add_obs_plugin(win-capture PLATFORMS WINDOWS)', \\"add_obs_plugin(win-capture PLATFORMS WINDOWS)\`r\`n  add_obs_plugin(obs-sl-browser PLATFORMS WINDOWS)\\") | Set-Content plugins\CMakeLists.txt"

    powershell -command "(Get-Content plugins\CMakeLists.txt).Replace('add_subdirectory(win-capture)', \\"add_subdirectory(win-capture)\`r\`n  add_subdirectory(obs-sl-browser)\\") | Set-Content plugins\CMakeLists.txt"

10. Configure OBS-Studio the regular way (check https://github.com/obsproject/obs-studio/wiki/build-instructions-for-windows)

    cmake --preset windows-x64

11. Start building

    cmake --build --preset windows-x64

