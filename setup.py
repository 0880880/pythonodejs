# setup.py
import glob
from setuptools import find_packages, setup
from wheel.bdist_wheel import bdist_wheel as _bdist_wheel

class bdist_wheel(_bdist_wheel):
    def finalize_options(self):
        super().finalize_options()
        # force platform tag
        self.root_is_pure = False

external_files = [
    f.replace("pythonode/", "", 1)
    for f in glob.glob("pythonode/external/**/*", recursive=True)
    if not f.endswith("/")
]

setup(
    name="pythonode",
    version="0.1.0",
    packages=find_packages(),
    include_package_data=True,
    package_data={"pythonode": ["lib/*", *external_files]},
    zip_safe=False,
    cmdclass={"bdist_wheel": bdist_wheel},
    options={
        "bdist_wheel": {
            "universal": True
        }
    }
)
