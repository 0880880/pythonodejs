#!/bin/bash
set -e

if [[ "$(uname)" == "Linux" ]]; then
    echo "Running on Linux"

    echo "🧰 Installing Clang..."
    yum install -y curl xz
    curl -SL https://github.com/llvm/llvm-project/releases/download/llvmorg-10.0.0/clang+llvm-10.0.0-x86_64-linux-gnu-ubuntu-18.04.tar.xz | tar -xJ
    mv clang+llvm-10.0.0-x86_64-linux-gnu-ubuntu-18.04 /opt/clang_10
    export PATH=/opt/clang_10/bin:$PATH
    export LD_LIBRARY_PATH=/opt/clang_10/lib:$LD_LIBRARY_PATH

    export CC=clang
    export CXX=clang++

    yum install -y devtoolset-9
    source /opt/rh/devtoolset-9/enable

    echo "⬆️  Updating pip, setuptools, and wheel..."
    python -m pip install --upgrade pip setuptools wheel

    echo "📦 Installing build dependencies..."
    pip install -r requirements.txt

    echo "🛠️  Installing SCons..."
    pip install scons

    echo "🔧 Building native binary..."
    scons

    echo "📂 Moving libnode files..."
    mv pythonodejs/externals/libnode/* pythonodejs/lib/

    #echo "📁 Tree view (ignoring .git, v8, node)..."
    #python tree.py . --max-files 20000 --ignore .git v8 node

    echo "✅ Validating build environment..."
    python setup.py check

else
    echo "Skipping (Linux only)"
fi

