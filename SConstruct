from colorama import Fore, Style
from pathlib import Path
from tqdm import tqdm
import platform
import shutil
import pooch
import os


def print_colored(text, color=Fore.GREEN):
    print(color + text + Style.RESET_ALL)


def print_progress_bar(iteration, total, prefix='', length=40):
    percent = ("{0:.1f}").format(100 * (iteration / float(total)))
    filled_length = int(length * iteration // total)
    bar = '█' * filled_length + '-' * (length - filled_length)
    sys.stdout.write(f'\r{prefix} |{bar}| {percent}% Complete')
    sys.stdout.flush()


def get_system_info():
    bits = platform.architecture()[0].lower()
    os_name = platform.system().lower()
    return {"OS": os_name, "BITS": bits, "ARCH": platform.machine().lower()}


def extract_archive(archive_path, extract_to):
    print_colored(f"Extracting {archive_path}...", Fore.YELLOW)
    if archive_path.endswith('.zip'):
        shutil.unpack_archive(archive_path, extract_to, 'zip')
    elif archive_path.endswith('.tar.xz'):
        shutil.unpack_archive(archive_path, extract_to, 'xztar')
    else:
        print_colored(f"Unsupported archive format: {archive_path}", Fore.RED)


get_arch = lambda: {"x86_64": "amd64","aarch64": "arm64","arm64": "arm64","amd64": "amd64"}.get(machine := platform.machine().lower()) or (_ for _ in ()).throw(RuntimeError(f"Unsupported architecture: {machine}"))


sys_info = get_system_info()
ARCH = get_arch()
OS = sys_info["OS"]
LIBNODE_OS = OS
if OS == "darwin":
    LIBNODE_OS = "macos"
ARC_EXT = "zip" if OS == "windows" else "tar.xz"


libnode_url = f"https://github.com/metacall/libnode/releases/download/v23.11.0/libnode-{ARCH}-{LIBNODE_OS}.{ARC_EXT}"

pythonode_path = Path("./pythonodejs")

lib_dir = Path("./pythonodejs/externals/libnode")
lib_dir.mkdir(exist_ok=True, parents=True)

print(f"Downloading libnode from \"{libnode_url}\"")

libnode: Path = Path(pooch.retrieve(url=libnode_url, known_hash=None, progressbar=True))

if not any(lib_dir.iterdir()):
    extract_archive(str(libnode.resolve()), str(lib_dir.resolve()))
else:
    print("lib directory is not empty.")

CXXFLAGS = ['-std=c++20']
if not OS == 'windows':
    CXXFLAGS.append('-fPIC')
INCLUDES = ['./pythonodejs/externals/node', './pythonodejs/externals/v8/include', './pythonodejs/externals/uv/include']
LDFLAGS = [f'-L{lib_dir.resolve()}', '-lnode', '-shared']

if OS == 'windows':
    EXT = 'dll'
elif OS == 'darwin':
    EXT = 'dylib'
else:
    EXT = 'so'

dst_lib = pythonode_path / "lib"

for item in lib_dir.iterdir():
    shutil.move(str(item), dst_lib / item.name)

env = Environment(
    TOOLS=['clang', 'clang++', 'gnulink'],
    ENV={'PATH': os.environ['PATH']},
    CXXFLAGS=CXXFLAGS,
    CPPPATH=INCLUDES,
    LINKFLAGS=LDFLAGS
)

env.Program(target=str((pythonode_path / "lib" / f'pythonodejs-{OS}-{ARCH}.{EXT}').resolve()), source=['pythonodejs.cpp'])
