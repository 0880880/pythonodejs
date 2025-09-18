#!/bin/bash
bear -- clang++ -c -std=c++20 -DNODE_WANT_INTERNALS=1 -shared -fPIC pythonodejs/pythonodejs.cpp \
    -Iinclude/node/src     -Iinclude/node/deps/v8/include -Iinclude/node/deps/uv/include \
    -o node$(python3-config --extension-suffix) \
    -Llibs/libnode -Wl,-rpath=$(pwd)/libs/libnode -lnode \
    $(python3-config --includes --ldflags)