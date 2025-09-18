#pragma once

#include "node.h"
#include "uv.h"
#include <assert.h>
#include <map>

using std::unique_ptr;

using node::Environment;
using std::map;
using v8::Context;
using v8::Function;
using v8::Global;
using v8::Isolate;
using v8::Local;
using v8::Promise;

typedef struct NodeEnv {
    Isolate* isolate;
    Environment* env;
    uv_loop_t* loop;
    Global<Function> import;
    Global<Function> require;
    Global<Function> runInThisContext;
    map<int, Global<Promise>> promises;
} NodeEnv;
