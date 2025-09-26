#!/usr/bin/env bash
set -euo pipefail

echo "DEBUG: start build_node.sh"

PROJECT_ROOT="${GITHUB_WORKSPACE:-$(pwd)}"
if [ -d "/io" ]; then
  PROJECT_ROOT="/io"
fi
PLATFORM="${1:-linux}"
BUILD_DIR="${PROJECT_ROOT}/build/node-src"
INSTALL_DIR="${PROJECT_ROOT}"

export BUILD_DIR INSTALL_DIR

echo "DEBUG: PROJECT_ROOT=${PROJECT_ROOT}"
echo "DEBUG: PLATFORM=${PLATFORM}"
echo "DEBUG: BUILD_DIR=${BUILD_DIR}"
echo "DEBUG: INSTALL_DIR=${INSTALL_DIR}"

mkdir -p "${BUILD_DIR}" "${INSTALL_DIR}"
echo "DEBUG: created directories"

cd "${BUILD_DIR}"
echo "DEBUG: changed to build dir: $(pwd)"

if [ -d "${PROJECT_ROOT}/include/node" ]; then
  echo "DEBUG: copying ${PROJECT_ROOT}/include/node into ${BUILD_DIR}"
  cp -rP "${PROJECT_ROOT}/include/node" ./ 2>/dev/null || echo "DEBUG: cp returned non-zero (ignored)"
else
  echo "DEBUG: no ${PROJECT_ROOT}/include/node to copy"
fi

echo "DEBUG: attempting to enter node directory"
cd "node"
echo "DEBUG: in node directory: $(pwd)"

echo "DEBUG: running perl patch on ares_rand.c"
perl -0777 -pi -e 's|#  include <sys/random.h>|#  if defined(__linux__)\n#    include <unistd.h>\n#    include <sys/types.h>\n#  else\n#    include <sys/random.h>\n#  endif|' deps/cares/src/lib/util/ares_rand.c
echo "DEBUG: perl patch complete"

export CFLAGS="${CFLAGS:-} -U HAVE_GETRANDOM -U HAVE_SYS_RANDOM_H"
export CXXFLAGS="${CXXFLAGS:-} -U HAVE_GETRANDOM -U HAVE_SYS_RANDOM_H"
echo "DEBUG: CFLAGS='${CFLAGS}'"
echo "DEBUG: CXXFLAGS='${CXXFLAGS}'"

CARES_CFG="deps/cares/config/linux/ares_config.h"
echo "DEBUG: CARES_CFG=${CARES_CFG}"
if [ -f "${CARES_CFG}" ]; then
  echo "DEBUG: appending undefs to ${CARES_CFG} if missing"
  grep -q '#undef HAVE_GETRANDOM' "${CARES_CFG}" || printf '\n#undef HAVE_GETRANDOM\n#undef HAVE_SYS_RANDOM_H\n' >> "${CARES_CFG}"
  echo "DEBUG: CARES_CFG updated"
else
  echo "DEBUG: CARES_CFG not present"
fi

if [ "${PLATFORM}" = "windows" ]; then
  echo "DEBUG: windows build path"
  ./vcbuild.bat static dll x64 release
  echo "DEBUG: copying Release/node.dll to ${INSTALL_DIR}"
  cp Release/node.lib "${INSTALL_DIR}/lib"
  echo "DEBUG: windows build finished"
else
  echo "DEBUG: unix build path (configure & make)"
  ./configure --prefix="${INSTALL_DIR}" --partly-static --enable-static
  echo "DEBUG: configure finished"

  if command -v nproc >/dev/null 2>&1; then
    CORES=$(nproc)
  elif command -v sysctl >/dev/null 2>&1; then
    CORES=$(sysctl -n hw.ncpu)
  else
    CORES=2
  fi
  echo "DEBUG: using CORES=${CORES}"

  echo "DEBUG: running make -j${CORES}"
  make -j"${CORES}" || { echo "DEBUG: make -j${CORES} failed, retrying make -j2"; make -j2; }
  echo "DEBUG: make completed"

  echo "DEBUG: post-processing libs in ${INSTALL_DIR}/lib"
  if [ -d "${INSTALL_DIR}/lib" ]; then
    cd "${INSTALL_DIR}/lib"
    echo "DEBUG: in lib dir: $(pwd)"
    if [ "${PLATFORM}" = "linux" ]; then
      LIB_FILE=$(ls libnode.so.* 2>/dev/null | head -n1 || true)
      echo "DEBUG: linux lib candidate=${LIB_FILE}"
      if [ -n "${LIB_FILE}" ]; then
        patchelf --set-soname libnode.so "${LIB_FILE}" || echo "DEBUG: patchelf failed (ignored)"
        mv "${LIB_FILE}" libnode.so || echo "DEBUG: mv failed (ignored)"
        echo "DEBUG: linux lib post-processing done"
      else
        echo "DEBUG: no libnode.so.* found"
      fi
    elif [ "${PLATFORM}" = "macos" ]; then
      LIB_FILE=$(ls libnode.*.dylib 2>/dev/null | head -n1 || true)
      echo "DEBUG: macos lib candidate=${LIB_FILE}"
      if [ -n "${LIB_FILE}" ]; then
        mv "${LIB_FILE}" libnode.dylib || echo "DEBUG: mv failed (ignored)"
        install_name_tool -id libnode.dylib libnode.dylib || echo "DEBUG: install_name_tool failed (ignored)"
        echo "DEBUG: macos lib post-processing done"
      else
        echo "DEBUG: no libnode.*.dylib found"
      fi
    else
      echo "DEBUG: unknown unix PLATFORM=${PLATFORM}, skipping lib post-processing"
    fi
  else
    echo "DEBUG: ${INSTALL_DIR}/lib does not exist"
  fi
fi

echo "DEBUG: Node build finished, install dir=${INSTALL_DIR}"
ls -la "${INSTALL_DIR}" || echo "DEBUG: ls failed (ignored)"

echo "DEBUG: staging include dir in project"
mkdir -p "${PROJECT_ROOT}/include"
echo "DEBUG: running Python sync to copy includes/libs"

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

src = os.path.join(INSTALL_DIR, 'include')
dst = os.path.join(PROJECT_ROOT, 'include')
if os.path.isdir(src):
    copy_if_changed(src, dst)
else:
    print("No include/ to copy (OK).")

lib_src = os.path.join(INSTALL_DIR, 'lib')
lib_dst = os.path.join(PROJECT_ROOT, 'libs', 'libnode', 'lib')
if os.path.isdir(lib_src):
    copy_if_changed(lib_src, lib_dst)

bin_src = os.path.join(INSTALL_DIR, 'bin')
bin_dst = os.path.join(PROJECT_ROOT, 'libs', 'libnode', 'bin')
if os.path.isdir(bin_src):
    try:
        copy_if_changed(bin_src, bin_dst)
    except Exception as e:
        print("WARN copying bin (ignored):", e, file=sys.stderr)
PY

echo "DEBUG: finished staging includes/libs"
echo "DEBUG: listing staged lib dir"
ls -la "${INSTALL_DIR}/lib" || echo "DEBUG: staged lib listing failed (maybe missing)"
echo "DEBUG: end build_node.sh"
