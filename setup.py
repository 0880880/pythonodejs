# setup.py
import glob
import os
from setuptools import setup, Extension
from wheel.bdist_wheel import bdist_wheel as _bdist_wheel


class bdist_wheel(_bdist_wheel):
    def finalize_options(self):
        super().finalize_options()
        self.root_is_pure = False


external_files = [
    f.replace("pythonodejs/", "", 1)
    for f in glob.glob("pythonodejs/external/**/*", recursive=True)
    if not f.endswith("/")
]

ext = Extension(
    "pythonodejs",
    sources=["src/binding.cpp"],
    language="c++",
)

setup(
    name="pythonodejs",
    version="1.4.0",
    packages=["node"],
    ext_modules=[ext],
    include_package_data=True,
    package_data={
        "pythonodejs": [
            *external_files,
            *[
                f"lib/{f}"
                for f in os.listdir("pythonodejs/lib")
                if not (f.endswith(".exp") or f.endswith(".pdb") or f.endswith(".ilk"))
            ],
        ]
    },
    zip_safe=False,
    cmdclass={"bdist_wheel": bdist_wheel},
)
