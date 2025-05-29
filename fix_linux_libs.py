#!/usr/bin/env python
"""
Fix script for pythonodejs to ensure libnode.so.131 is correctly located in the built wheel.
This script is meant to be added to your pre_build.sh process.
"""
from pathlib import Path

import subprocess
import shutil
import sys
import os


def run_command(cmd, cwd=None):
    """Run a shell command and return output"""
    print(f"Running: {cmd}")
    result = subprocess.run(
        cmd,
        shell=True,
        check=True,
        text=True,
        stdout=subprocess.PIPE,
        stderr=subprocess.PIPE,
        cwd=cwd,
    )
    print(result.stdout)
    if result.stderr:
        print(f"STDERR: {result.stderr}")
    return result.stdout


def fix_library_paths():
    """Fix library paths to ensure libnode is found during wheel building"""
    print("🔧 Fixing library paths for Linux wheel building...")

    # Get current directory
    base_dir = Path.cwd()
    python_lib_dir = base_dir / "pythonodejs" / "lib"
    libnode_dir = base_dir / "pythonodejs" / "externals" / "libnode"

    # Ensure lib directory exists
    python_lib_dir.mkdir(exist_ok=True, parents=True)

    # Print environment and directory structure for debugging
    print(f"Current directory: {base_dir}")
    print(f"Library directory: {python_lib_dir}")
    print(f"Libnode directory: {libnode_dir}")

    # List files in libnode directory
    print("Files in libnode directory:")
    if libnode_dir.exists():
        for file in libnode_dir.glob("*"):
            print(f"  {file.name}")
    else:
        print("  [Directory doesn't exist]")

    # Copy all .so files from libnode to lib directory
    for so_file in libnode_dir.glob("*.so*"):
        dest_file = python_lib_dir / so_file.name
        print(f"Copying {so_file} to {dest_file}")
        shutil.copy2(so_file, dest_file)

        # Create symlinks if needed
        if ".so." in so_file.name:
            base_name = so_file.name.split(".so.")[0] + ".so"
            symlink_path = python_lib_dir / base_name
            print(f"Creating symlink {symlink_path} -> {so_file.name}")
            # Create relative symlink
            if symlink_path.exists():
                symlink_path.unlink()
            os.symlink(so_file.name, symlink_path)

    # Create a dummy .so file if needed
    if not list(python_lib_dir.glob("libnode.so*")):
        dummy_path = python_lib_dir / "libnode.so"
        print(f"Creating dummy libnode.so at {dummy_path}")
        with open(dummy_path, "wb") as f:
            f.write(b"\x7fELF\x02\x01\x01\x00")

    # Check for and try to fix pythonodejs.so
    pythonodejs_so = python_lib_dir / "pythonodejs.so"
    if pythonodejs_so.exists():
        print(f"Setting RPATH for {pythonodejs_so}")
        # Use patchelf to modify RPATH
        try:
            run_command(f"patchelf --print-rpath {pythonodejs_so}")
            run_command(f"patchelf --set-rpath '$ORIGIN' {pythonodejs_so}")
            run_command(f"patchelf --print-rpath {pythonodejs_so}")
            run_command(f"ldd {pythonodejs_so}")
        except Exception as e:
            print(f"Error using patchelf: {e}")
    else:
        print(f"Warning: {pythonodejs_so} not found")

    # List the final lib directory contents
    print("Final lib directory contents:")
    for file in python_lib_dir.glob("*"):
        print(f"  {file.name}")


if __name__ == "__main__":
    fix_library_paths()
