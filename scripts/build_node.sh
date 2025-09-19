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
export CFLAGS="${CFLAGS:-} -U HAVE_GETRANDOM -U HAVE_SYS_RANDOM_H"
export CXXFLAGS="${CXXFLAGS:-} -U HAVE_GETRANDOM -U HAVE_SYS_RANDOM_H"

# Also defensively patch the generated config header if it exists
CARES_CFG="deps/cares/config/linux/ares_config.h"
if [ -f "$CARES_CFG" ]; then
  grep -q '#undef HAVE_GETRANDOM' "$CARES_CFG" || printf '\n#undef HAVE_GETRANDOM\n#undef HAVE_SYS_RANDOM_H\n' >> "$CARES_CFG"
fi

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

# Copy libs (shared/static) into project libs so wheel can bundle them
mkdir -p "${PROJECT_ROOT}/libs/libnode/lib"

# Helper: convert Windows MSYS/Cygwin paths to Unix-style for rsync if possible
convert_path_for_rsync() {
  local p="$1"
  if command -v cygpath >/dev/null 2>&1; then
    cygpath -u "$p"
  else
    # If no cygpath, try a naive conversion of "C:\..." to "/c/..."
    # Works in Git Bash / MSYS when /c/ exists; safe fallback.
    if [[ "$p" =~ ^([A-Za-z]):\\(.*) ]]; then
      local drive="$(echo "${BASH_REMATCH[1]}" | tr '[:upper:]' '[:lower:]')"
      local rest="$(echo "${BASH_REMATCH[2]}" | sed 's#\\#/#g')"
      echo "/${drive}/${rest}"
    else
      printf '%s\n' "$p"
    fi
  fi
}

if [[ "${PLATFORM}" = "windows" ]] || [[ "$(uname -s 2>/dev/null || true)" =~ (MINGW|MSYS|CYGWIN) ]]; then
  echo "Staging files on Windows/MinGW environment"

  # Prefer rsync if available (but convert paths first)
  if command -v rsync >/dev/null 2>&1; then
    SRC="$(convert_path_for_rsync "${INSTALL_DIR}/include/")"
    DST="$(convert_path_for_rsync "${PROJECT_ROOT}/include/")"
    mkdir -p "${PROJECT_ROOT}/include"
    echo "rsync $SRC -> $DST"
    rsync -a "$SRC" "$DST"
  elif command -v robocopy >/dev/null 2>&1; then
    echo "Using robocopy fallback"
    # robocopy takes source_dir target_dir [file ...] /E to mirror dirs
    robocopy "${INSTALL_DIR}/include" "${PROJECT_ROOT}/include" /E || true
  else
    echo "Using cp fallback"
    cp -r "${INSTALL_DIR}/include/." "${PROJECT_ROOT}/include/" || true
  fi

  # libs/bin
  if [ -d "${INSTALL_DIR}/lib" ]; then
    if command -v rsync >/dev/null 2>&1; then
      SRC="$(convert_path_for_rsync "${INSTALL_DIR}/lib/")"
      DST="$(convert_path_for_rsync "${PROJECT_ROOT}/libs/libnode/lib/")"
      mkdir -p "${PROJECT_ROOT}/libs/libnode/lib"
      rsync -a "$SRC" "$DST"
    else
      mkdir -p "${PROJECT_ROOT}/libs/libnode/lib"
      cp -r "${INSTALL_DIR}/lib/." "${PROJECT_ROOT}/libs/libnode/lib/" || true
    fi
  fi

  if [ -d "${INSTALL_DIR}/bin" ]; then
    if command -v rsync >/dev/null 2>&1; then
      SRC="$(convert_path_for_rsync "${INSTALL_DIR}/bin/")"
      DST="$(convert_path_for_rsync "${PROJECT_ROOT}/libs/libnode/bin/")"
      mkdir -p "${PROJECT_ROOT}/libs/libnode/bin"
      rsync -a "$SRC" "$DST" || true
    else
      mkdir -p "${PROJECT_ROOT}/libs/libnode/bin"
      cp -r "${INSTALL_DIR}/bin/." "${PROJECT_ROOT}/libs/libnode/bin/" || true
    fi
  fi

else
  # POSIX (Linux / macOS) — original rsync usage
  rsync -a "${INSTALL_DIR}/include/" "${PROJECT_ROOT}/include/"

  mkdir -p "${PROJECT_ROOT}/libs/libnode/lib"
  if [ -d "${INSTALL_DIR}/lib" ]; then
    rsync -a "${INSTALL_DIR}/lib/" "${PROJECT_ROOT}/libs/libnode/lib/"
  fi
  if [ -d "${INSTALL_DIR}/bin" ]; then
    rsync -a "${INSTALL_DIR}/bin/" "${PROJECT_ROOT}/libs/libnode/bin/" || true
  fi
fi

echo "Finished building Node and staging includes/libs into project."
