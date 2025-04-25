#!/bin/bash
set -e

if [[ "$(uname)" == "Linux" ]]; then
    echo "Running on Linux"

    echo "🧰 Installing Clang..."
    yum install -y curl xz
    # download & run the Miniforge3 installer for Linux x86_64
    curl -LO https://github.com/conda-forge/miniforge/releases/latest/download/Miniforge3-Linux-x86_64.sh
    bash Miniforge3-Linux-x86_64.sh -b -p $HOME/miniforge
    export PATH=$HOME/miniforge/bin:$PATH

    conda create -n clang10 \
      -c conda-forge/label/llvm_dev clangdev=10.* llvmdev=10.* \
      -y
    conda init bash
    source ~/.bashrc
    conda activate clang10

    clang++ --version

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

