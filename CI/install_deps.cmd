set GRPC_DIST=grpc_dist

if "%~1"=="arm64" (
    set GRPC_FILE=grpc-release-static-v1.71.0-arm64.7z
    set GRPC_HOST_FILE=grpc-release-static-%GRPC_VERSION%.7z
) else (
    set GRPC_FILE=grpc-release-static-%GRPC_VERSION%.7z
)

set GRPC_URL=https://obs-studio-deployment.s3-us-west-2.amazonaws.com/%GRPC_FILE%

if exist grpc_dist (
    echo "grpc dependency already installed"
) else (
    if exist %GRPC_FILE% (curl -kLO %GRPC_URL% -f --retry 5 -z GRPC_FILE) else (curl -kLO %GRPC_URL% -f --retry 5 -C -)
    7z x %GRPC_FILE% -aoa -ogrpc_dist
)

if "%~1"=="arm64" (
    if not exist x64_for_arm (
        set GRPC_HOST_URL=https://obs-studio-deployment.s3-us-west-2.amazonaws.com/%GRPC_HOST_FILE%
        if exist %GRPC_HOST_FILE% (curl -kLO %GRPC_HOST_URL% -f --retry 5 -z GRPC_HOST_FILE) else (curl -kLO %GRPC_HOST_URL% -f --retry 5 -C -)
        7z x %GRPC_HOST_FILE% -aoa -ox64_for_arm
        copy /Y x64_for_arm\bin\protoc.exe grpc_dist\bin\protoc.exe
        copy /Y x64_for_arm\bin\grpc_cpp_plugin.exe grpc_dist\bin\grpc_cpp_plugin.exe
    )
)

dir %GRPC_DIST%