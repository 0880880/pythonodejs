import os
import platform
from pathlib import Path
from setuptools import setup, Extension

system = platform.system()

libnode_path = os.environ.get("LIBNODE_PATH")

if not libnode_path:
    libs_dir = Path(__file__).parent / "libs"
    if system == "Linux":
        libnode_path = str(libs_dir / "linux")
    elif system == "Darwin":
        libnode_path = str(libs_dir / "macos")
    elif system == "Windows":
        libnode_path = str(libs_dir / "windows")

node_include = str(Path(__file__).parent / "include" / "node" / "src")
node_deps_v8 = str(
    Path(__file__).parent / "include" / "node" / "deps" / "v8" / "include"
)
node_deps_uv = str(
    Path(__file__).parent / "include" / "node" / "deps" / "uv" / "include"
)

include_dirs = [
    node_include,
    node_deps_v8,
    node_deps_uv,
]

extra_compile_args = []
extra_link_args = []
libraries = []
library_dirs = []

if system == "Linux":
    extra_compile_args = ["-std=c++20", "-fPIC"]
    extra_link_args = ["-Wl,-rpath,$ORIGIN"]
    libraries = ["node"]
    if libnode_path:
        library_dirs = [libnode_path]
        extra_link_args.append(f"-Wl,-rpath,{libnode_path}")

elif system == "Darwin":
    extra_compile_args = ["-std=c++20", "-stdlib=libc++", "-mmacosx-version-min=10.15"]
    extra_link_args = ["-Wl,-rpath,@loader_path"]
    libraries = ["node"]
    if libnode_path:
        library_dirs = [libnode_path]
        extra_link_args.append(f"-Wl,-rpath,{libnode_path}")

elif system == "Windows":
    extra_compile_args = ["/std:c++20", "/Zc:__cplusplus", "/EHsc", "/MD"]
    libraries = ["libnode"]
    if libnode_path:
        library_dirs = [libnode_path]

pythonodejs_extension = Extension(
    "pythonodejs",
    sources=["pythonodejs/pythonodejs.cpp"],
    include_dirs=include_dirs,
    library_dirs=library_dirs,
    libraries=libraries,
    extra_compile_args=extra_compile_args,
    extra_link_args=extra_link_args,
    language="c++",
)

setup(
    name="pythonodejs",
    version="0.1.0",
    description="Python-NodeJS interop library",
    author="0880",
    ext_modules=[pythonodejs_extension],
    packages=["pythonodejs"],
    package_data={
        "pythonodejs": ["*.pyi"],
    },
    python_requires=">=3.8",
)
