from colorama import Fore, Style
from pathlib import Path
import platform
import shutil
import pooch
import os
from SCons.Script import Environment


def print_colored(text, color=Fore.GREEN):
    print(color + text + Style.RESET_ALL)


def get_system_info():
    bits = platform.architecture()[0].lower()
    os_name = platform.system().lower()

    arch_map = {
        "x86_64": "amd64",
        "aarch64": "arm64",
        "amd64": "amd64",
        "arm64": "arm64"
    }

    return {"OS": os_name, "BITS": bits, "ARCH": arch_map.get(platform.machine().lower())}


def extract_archive(archive_path, extract_to):
    print_colored(f"Extracting {archive_path}...", Fore.YELLOW)
    if archive_path.endswith('.zip'):
        shutil.unpack_archive(archive_path, extract_to, 'zip')
    elif archive_path.endswith('.tar.xz'):
        shutil.unpack_archive(archive_path, extract_to, 'xztar')
    else:
        print_colored(f"Unsupported archive format: {archive_path}", Fore.RED)


# === system info ===
sys_info = get_system_info()
ARCH = sys_info["ARCH"]
OS = sys_info["OS"]
ARC_EXT = "zip" if OS == "windows" else "tar.xz"

# === libnode URL ===
libnode_url = f"https://github.com/metacall/libnode/releases/download/v23.11.0/libnode-{ARCH}-{OS}.{ARC_EXT}"
lib_dir = Path("lib")
lib_dir.mkdir(exist_ok=True)

# === download libnode ===
print(f"Downloading libnode from \"{libnode_url}\"")
libnode: Path = Path(pooch.retrieve(url=libnode_url, known_hash=None, progressbar=True))

# === extract if needed ===
if not any(lib_dir.iterdir()):
    extract_archive(str(libnode.resolve()), str(lib_dir.resolve()))
else:
    print("lib directory is not empty.")

# === compile flags ===
CXXFLAGS = ['-std=c++23']
if OS != 'windows':
    CXXFLAGS.append('-fPIC')

INCLUDES = [
    './external/node/src',
    './external/node/deps/v8/include',
    './external/node/deps/uv/include'
]

# === link flags ===
libnode_static = lib_dir / 'libnode.a'
if libnode_static.exists():
    print_colored("Using static linking for libnode", Fore.CYAN)
    LDFLAGS = ['-L./lib', '-Wl,-Bstatic', '-lnode', '-Wl,-Bdynamic']
else:
    print_colored("Static libnode.a not found; using dynamic linking", Fore.YELLOW)
    LDFLAGS = ['-L./lib', '-lnode']

# === output extension ===
if OS == 'windows':
    EXT = 'dll'
elif OS == 'darwin':
    EXT = 'dylib'
else:
    EXT = 'so'

# === setup build environment ===
env = Environment(
    TOOLS=['clang', 'clang++', 'gnulink'],
    ENV={'PATH': os.environ['PATH']},
    CXXFLAGS=CXXFLAGS,
    CPPPATH=INCLUDES,
    LINKFLAGS=LDFLAGS
)

# === build shared library ===
target_name = f'pythonode-{OS}-{ARCH}.{EXT}'
env.SharedLibrary(target=str((lib_dir / target_name).resolve()), source=['pythonode.cpp'])
