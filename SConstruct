from colorama import Fore, Style
from pathlib import Path
import scons_compiledb
import platform
import shutil
import pooch
import os


def print_colored(text, color=Fore.GREEN):
    print(color + text + Style.RESET_ALL)


def get_system_info():
    bits = platform.architecture()[0].lower()
    os_name = platform.system().lower()
    return {"OS": os_name, "BITS": bits, "ARCH": platform.machine().lower()}


def extract_archive(archive_path, extract_to):
    print_colored(f"Extracting {archive_path}...", Fore.YELLOW)
    if archive_path.endswith(".zip"):
        shutil.unpack_archive(archive_path, extract_to, "zip")
    elif archive_path.endswith(".tar.xz"):
        shutil.unpack_archive(archive_path, extract_to, "xztar")
    else:
        print_colored(f"Unsupported archive format: {archive_path}", Fore.RED)


get_arch = lambda: {
    "x86_64": "amd64",
    "aarch64": "arm64",
    "arm64": "arm64",
    "amd64": "amd64",
    "i686": "x86",
    "x86": "x86",
}.get(machine := platform.machine().lower()) or (_ for _ in ()).throw(
    RuntimeError(f"Unsupported architecture: {machine}")
)


sys_info = get_system_info()
ARCH = get_arch()
OS = sys_info["OS"]
LIBNODE_OS = OS
if OS == "darwin":
    LIBNODE_OS = "macos"
ARC_EXT = "zip" if OS == "windows" else "tar.xz"


libnode_url = f"https://github.com/0880880/libnode/releases/download/v23.11.0/libnode-{ARCH}-{LIBNODE_OS}.{ARC_EXT}"

pythonode_path = Path("./pythonodejs")

lib_dir = Path("./pythonodejs/externals/libnode")
lib_dir.mkdir(exist_ok=True, parents=True)

print(f'Downloading libnode from "{libnode_url}"')

libnode: Path = Path(pooch.retrieve(url=libnode_url, known_hash=None, progressbar=True))

if not any(lib_dir.iterdir()):
    extract_archive(str(libnode.resolve()), str(lib_dir.resolve()))
else:
    print("lib directory is not empty.")

is_debug = not os.getenv("CI") == "true" and os.getenv("DEBUG") == "true"

if is_debug:
    print("DEBUG=TRUE CI=FALSE")

LDFLAGS = [
    f"-L{lib_dir.resolve()}",
    "-lnode",
    "-shared",
]

if not is_debug:
    CXXFLAGS = [
        "-std=c++20",
        "-DNODE_WANT_INTERNALS=1",
        "-O3",
        "-fPIC",
    ]
else:
    print(
        "Fast build enabled (debug symbols included). If this is unintended, set NO_DEBUG=true."
    )
    CXXFLAGS = [
        "-std=c++20",
        "-DNODE_WANT_INTERNALS=1",
        "-g",
        "-O0",
        "-flto=thin",
        "-fno-rtti",
        "-fPIC",
    ]
    LDFLAGS.append("-flto=thin")

if OS == "windows":
    CXXFLAGS.remove("-fPIC")

INCLUDES = [
    "./pythonodejs/externals/node",
    "./pythonodejs/externals/v8/include",
    "./pythonodejs/externals/uv/include",
]


if not OS == "windows":
    LDFLAGS.append("-Wl,-rpath,./lib")

EXT = "so"

if OS == "windows":
    EXT = "dll"
elif OS == "darwin":
    EXT = "dylib"

env = Environment(
    TOOLS=["clang", "clang++", "gnulink"],
    ENV={"PATH": os.environ["PATH"]},
    CXXFLAGS=CXXFLAGS,
    CPPPATH=INCLUDES,
    LINKFLAGS=LDFLAGS,
)

scons_compiledb.enable(env)
env.CompileDb()
env.Program(
    target=str((pythonode_path / "lib" / f"pythonodejs.{EXT}").resolve()),
    source=["pythonodejs.cpp"],
)
