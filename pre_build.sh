#!/bin/bash
set -e

if [[ "$(uname)" == "Linux" ]]; then
    echo "Running on Linux"

    echo "🧰 Installing Clang..."
    rpm -Uvh https://packages.llvm.org/apt/llvm-org.repo
    yum install clang

    export CC=clang
    export CXX=clang++

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
    echo "This script is only for Linux"
    exit 1
fi

