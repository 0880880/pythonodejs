import glob
from setuptools import find_packages, setup

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
    package_data={
        "pythonode": ["lib/*", *external_files],
    },
    zip_safe=False,  
)