from setuptools import setup, Extension
from setuptools.command.build_ext import build_ext
import os
import sys
import glob

ext_name = "pythonodejs"  # adjust if you want a specific name

include_dirs = [
    os.path.join(os.getcwd(), "include", "node", "src"),
    os.path.join(os.getcwd(), "include", "node", "deps", "v8", "include"),
    os.path.join(os.getcwd(), "include", "node", "deps", "uv", "include"),
]

library_dirs = [
    os.path.join(os.getcwd(), "libs", "libnode", "lib"),
]

extra_compile_args = ["-std=c++17", "-DNODE_WANT_INTERNALS=1", "-fPIC"]
extra_link_args = []

# On Linux attempt to add rpath so the built wheel will find libnode at runtime if bundled
if sys.platform.startswith("linux"):
    extra_link_args += ["-Wl,-rpath,$ORIGIN/libs/libnode/lib"]

sources = ["pythonodejs/pythonodejs.cpp"]

ext_modules = [
    Extension(
        ext_name,
        sources=sources,
        include_dirs=include_dirs,
        library_dirs=library_dirs,
        libraries=["node"],
        extra_compile_args=extra_compile_args,
        extra_link_args=extra_link_args,
        language="c++",
    )
]

setup(
    name="pythonodejs",
    version="1.0.0",
    description="Pythonodejs NodeJS Interop",
    ext_modules=ext_modules,
)
