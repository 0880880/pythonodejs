# setup.py
import glob
from setuptools import find_packages, setup
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

setup(
    name="pythonodejs",
    version="0.1.3",
    packages=find_packages(),
    include_package_data=True,
    package_data={"pythonodejs": ["lib/*", *external_files]},
    zip_safe=False,
    cmdclass={"bdist_wheel": bdist_wheel},
)
