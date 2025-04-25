#!/bin/bash
set -e

echo "🧰 Installing Clang..."
yum install -y clang llvm

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

unameOut="$(uname -s)"
case "${unameOut}" in
    Darwin)
        echo "🔗 Adding rpath for macOS..."
        install_name_tool -add_rpath pythonodejs/lib/ pythonodejs/lib/pythonodejs.dylib
        ;;
    MINGW*|MSYS*|CYGWIN*)
        echo "ℹ️ Windows detected — no rpath changes needed."
        ;;
    *)
        echo "🐧 Linux detected — no rpath changes needed."
        ;;
esac

echo "✅ Validating build environment..."
python setup.py check
