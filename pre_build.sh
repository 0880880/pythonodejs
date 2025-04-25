#!/bin/bash
set -e

echo "🔧 Building native binary..."
scons

echo "📦 Moving libnode files..."
mv pythonodejs/externals/libnode/* pythonodejs/lib/

echo "📁 Tree view (ignoring .git, v8, node)..."
python tree.py . --max-files 20000 --ignore .git v8 node

if [[ "$(uname)" == "Darwin" ]]; then
  echo "🛠️  Adding rpath for macOS..."
  install_name_tool -add_rpath pythonodejs/lib/ pythonodejs/lib/pythonodejs.dylib
fi

echo "✅ Validating build environment..."
python setup.py check
