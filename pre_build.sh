#!/bin/bash
set -e

if [[ "$(uname)" == "Linux" ]]; then
    echo "Running on Linux"

    echo "🧰 Installing Clang and build tools..."
    yum install -y clang

    # tell clang to use libc++ (both compile-time and link-time)
    export CXXFLAGS="-stdlib=libc++"
    export LDFLAGS="-stdlib=libc++"

    clang++ --version

    echo "⬆️  Updating pip, setuptools, and wheel..."
    python -m pip install --upgrade pip setuptools wheel patchelf

    echo "📦 Installing build dependencies..."
    pip install -r requirements.txt

    echo "🛠️  Installing SCons..."
    pip install scons

    echo "🔧 Building native binary..."
    scons

    echo "📂 Moving libnode files..."
    mv pythonodejs/externals/libnode/* pythonodejs/lib/

    echo "🔧 Running Linux-specific library fixes..."
    python ./fix_linux_libs.py

    echo "📁 Tree view (ignoring .git, v8, node)..."
    python tree.py . --max-files 20000 --ignore .git v8 node

    echo "✅ Validating build environment..."
    python setup.py check

else
    echo "Skipping (Linux only)"
fi
