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
export BUILD_DIR="${PROJECT_ROOT}/build/node-src"
export INSTALL_DIR="${PROJECT_ROOT}/libs/libnode"

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

if [ "$PLATFORM" = "windows" ]; then
    echo "Windows build"
    winget configure ./.configurations/configuration.dsc.yaml
    ./vcbuild dll x64 release
    xcopy /E /I Release/node.dll ${INSTALL_DIR}
    xcopy /E /I Release/node.dll ${INSTALL_DIR}
else
    # Linux / macOS: normal configure & make
    echo "Configuring Node..."
    ./configure --prefix="${INSTALL_DIR}" --shared
    echo "Running make -j"
    make -j$(nproc) || make -j2
    make install
fi

echo "Node built and installed to ${INSTALL_DIR}"
ls -la "${INSTALL_DIR}" || true

# Copy headers into project-level include/ tree so setup.py can find them
mkdir -p "${PROJECT_ROOT}/include"

# Cross-platform, rsync-like copy implemented in Python (works on GHA runners)
INSTALL_DIR="${INSTALL_DIR}" PROJECT_ROOT="${PROJECT_ROOT}" python3 - <<'PY'
import os, shutil, filecmp, sys

INSTALL_DIR = os.environ.get('INSTALL_DIR')
PROJECT_ROOT = os.environ.get('PROJECT_ROOT')
if not INSTALL_DIR or not PROJECT_ROOT:
    print("INSTALL_DIR and PROJECT_ROOT must be set", file=sys.stderr)
    sys.exit(2)

def copy_if_changed(src, dst):
    if os.path.islink(src):
        target = os.readlink(src)
        try:
            if os.path.lexists(dst) and os.path.islink(dst) and os.readlink(dst) == target:
                return
            if os.path.lexists(dst):
                if os.path.isdir(dst) and not os.path.islink(dst):
                    shutil.rmtree(dst)
                else:
                    os.remove(dst)
            os.symlink(target, dst)
        except Exception:
            try:
                tgt = target if os.path.isabs(target) else os.path.join(os.path.dirname(src), target)
                shutil.copy2(tgt, dst)
            except Exception:
                shutil.copy2(src, dst)
    elif os.path.isdir(src):
        os.makedirs(dst, exist_ok=True)
        try:
            shutil.copystat(src, dst, follow_symlinks=False)
        except Exception:
            pass
        for name in os.listdir(src):
            copy_if_changed(os.path.join(src, name), os.path.join(dst, name))
    else:
        if os.path.exists(dst):
            try:
                if filecmp.cmp(src, dst, shallow=False):
                    return
            except Exception:
                pass
        shutil.copy2(src, dst)

# include
src = os.path.join(INSTALL_DIR, 'include')
dst = os.path.join(PROJECT_ROOT, 'include')
if os.path.isdir(src):
    copy_if_changed(src, dst)
else:
    print("No include/ to copy (OK).")

# libs
lib_src = os.path.join(INSTALL_DIR, 'lib')
lib_dst = os.path.join(PROJECT_ROOT, 'libs', 'libnode', 'lib')
if os.path.isdir(lib_src):
    copy_if_changed(lib_src, lib_dst)

# bin (ignore errors)
bin_src = os.path.join(INSTALL_DIR, 'bin')
bin_dst = os.path.join(PROJECT_ROOT, 'libs', 'libnode', 'bin')
if os.path.isdir(bin_src):
    try:
        copy_if_changed(bin_src, bin_dst)
    except Exception as e:
        print("WARN copying bin (ignored):", e, file=sys.stderr)
PY

echo "Finished building Node and staging includes/libs into project."

echo "Test built libs"
ls ${PROJECT_ROOT}/libs/libnode/lib
