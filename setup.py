from setuptools import setup, Extension
import os
import sys

ext_name = "pythonodejs"

include_dirs = [
    os.path.join(os.getcwd(), "include", "node", "src"),
    os.path.join(os.getcwd(), "include", "node", "deps", "v8", "include"),
    os.path.join(os.getcwd(), "include", "node", "deps", "uv", "include"),
]

library_dirs = [
    os.path.join(os.getcwd(), "lib"),
]

extra_compile_args = ["-DNODE_WANT_INTERNALS=1", "-DNODE_STATIC=1", "-static"]
if sys.platform.startswith("win"):
    extra_compile_args += ["/std:c++20", "/Zc:__cplusplus"]
else:
    extra_compile_args += ["-std=c++20", "-fPIC"]

extra_link_args = ["-static"]

if sys.platform.startswith("linux") or sys.platform.startswith("darwin"):
    libraries += ["dl", "pthread"]
# if sys.platform.startswith("linux"):
#     extra_link_args += ["-Wl,-rpath,$ORIGIN/lib"]

sources = ["pythonodejs/pythonodejs.cpp"]

if sys.platform == "win32":
    extra_objects = [os.path.join(library_dirs[0], "node.lib")]
else:
    extra_objects = [os.path.join(library_dirs[0], "libnode.a")]

ext_modules = [
    Extension(
        ext_name,
        sources=sources,
        include_dirs=include_dirs,
        extra_compile_args=extra_compile_args,
        extra_link_args=extra_link_args,
        extra_objects=extra_objects,
        language="c++",
    )
]

setup(
    name="pythonodejs",
    version="1.0.0",
    description="Python–NodeJS Interop",
    ext_modules=ext_modules,
)
