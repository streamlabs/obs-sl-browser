set GRPC_DIST=grpc_dist
set GRPC_FILE=grpc-release-static-%GRPC_VERSION%.7z
set GRPC_URL=https://obs-studio-deployment.s3-us-west-2.amazonaws.com/%GRPC_FILE%

if exist grpc_dist (
    echo "grpc dependency already installed"
) else (
    if exist %GRPC_FILE% (curl -kLO %GRPC_URL% -f --retry 5 -z GRPC_FILE) else (curl -kLO %GRPC_URL% -f --retry 5 -C -)
    7z x %GRPC_FILE% -aoa -ogrpc_dist
)

dir %GRPC_DIST%