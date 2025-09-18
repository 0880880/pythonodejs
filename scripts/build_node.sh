#!/usr/bin/env bash
set -euo pipefail

# Usage:
#   bash scripts/build_node.sh linux
#   bash scripts/build_node.sh macos
#   bash scripts/build_node.sh windows
#
# When run inside manylinux container (cibuildwheel) the project root is mounted at /io
PROJECT_ROOT="${GITHUB_WORKSPACE:-$(pwd)}"
if [ -d "/io" ]; then
  # inside manylinux container cibuildwheel mounts project at /io
  PROJECT_ROOT="/io"
fi

PLATFORM="${1:-linux}"
NODE_VERSION="${NODE_VERSION:-20.19.5}"
BUILD_DIR="${PROJECT_ROOT}/build/node-src"
INSTALL_DIR="${PROJECT_ROOT}/libs/libnode"

mkdir -p "${BUILD_DIR}" "${INSTALL_DIR}"

echo "Building Node ${NODE_VERSION} for platform=${PLATFORM}"
cd "${BUILD_DIR}"

TARBALL="node-v${NODE_VERSION}.tar.gz"
if [ ! -d "node-v${NODE_VERSION}" ]; then
  echo "Downloading Node ${NODE_VERSION}..."
  curl -fsSL "https://nodejs.org/dist/v${NODE_VERSION}/${TARBALL}" -o "${TARBALL}"
  tar xf "${TARBALL}"
fi
cd "node-v${NODE_VERSION}"
# Before building, patch cares
perl -0777 -pi -e 's|#  include <sys/random.h>|#  if defined(__linux__)\n#    include <unistd.h>\n#    include <sys/types.h>\n#  else\n#    include <sys/random.h>\n#  endif|' deps/cares/src/lib/util/ares_rand.c


# Prepare configure flags (platform-specific tweaks)
CONFIGURE_OPTS=(--prefix="${INSTALL_DIR}" --fully-static)
# On Windows we will use MSVC/build tools; use python to generate correct build if needed
if [ "$PLATFORM" = "windows" ]; then
  echo "Windows build: using msbuild / Visual Studio toolchain (requires preinstalled tools)"
  # Node's Windows build flow uses vcbuild.bat; run it via bash wrapper if provided
  # We'll use a generic approach: let Node's build system detect env
  python3 ./configure --prefix="${INSTALL_DIR}" --openssl-no-asm
  # use built-in msbuild script (vcbuild). Fallback to `python tools\msvs\...` if needed.
  # For simplicity attempt `make` (MSYS2) or `vcbuild` if present.
  if [ -f "vcbuild.bat" ]; then
    ./vcbuild.bat release
    # install step: copy out build artifacts
    mkdir -p "${INSTALL_DIR}/bin" "${INSTALL_DIR}/lib"
    cp -v out/Release/node.exe "${INSTALL_DIR}/bin/" || true
    cp -v out/Release/node.lib "${INSTALL_DIR}/lib/" || true
  else
    echo "vcbuild.bat not found; Windows builds typically need MSVC environment; ensure Visual Studio build tools are present."
    exit 1
  fi
else
  # Linux / macOS: normal configure & make
  echo "Configuring Node..."
  ./configure --prefix="${INSTALL_DIR}"
  echo "Running make -j"
  make -j$(nproc) || make -j2
  make install
fi

# After install, copy headers to expected include paths for your extension
# Node's installed layout puts headers in ${INSTALL_DIR}/include
echo "Node built and installed to ${INSTALL_DIR}"
ls -la "${INSTALL_DIR}" || true

# Copy headers into project-level include/ tree so setup.py can find them
mkdir -p "${PROJECT_ROOT}/include"
rsync -a "${INSTALL_DIR}/include/" "${PROJECT_ROOT}/include/"

# Copy libs (shared/static) into project libs so wheel can bundle them
mkdir -p "${PROJECT_ROOT}/libs/libnode/lib"
if [ -d "${INSTALL_DIR}/lib" ]; then
  rsync -a "${INSTALL_DIR}/lib/" "${PROJECT_ROOT}/libs/libnode/lib/"
fi
if [ -d "${INSTALL_DIR}/bin" ]; then
  rsync -a "${INSTALL_DIR}/bin/" "${PROJECT_ROOT}/libs/libnode/bin/" || true
fi

echo "Finished building Node and staging includes/libs into project."
