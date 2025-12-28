#pragma once

#include "v8-container.h"
#ifndef NODE_WANT_INTERNALS
#define NODE_WANT_INTERNALS 1
#endif
#include "env.h"
#include "node.h"
#include "uv.h"
#include "v8-local-handle.h"
#include <Python.h>
#include <assert.h>
#include <env-inl.h>
#include <map>
#include <memory>
#include <node.h>
#include <node_internals.h>
#include <uv.h>
#include <v8-platform.h>
#include <v8.h>

// Platform specific
#ifdef _WIN32
#include <windows.h>
#endif

// Namespaces
using node::CommonEnvironmentSetup;
using node::Environment;
using node::MultiIsolatePlatform;
using v8::Context;
using v8::Function;
using v8::Global;
using v8::Isolate;
using v8::Local;
using v8::MaybeLocal;
using v8::Object;
using v8::Promise;
using v8::Value;

// The Core Environment Structure
struct NodeEnv {
    Isolate* isolate = nullptr;
    Environment* env = nullptr;
    uv_loop_t* loop = nullptr;
    std::unique_ptr<CommonEnvironmentSetup> setup;

    // JS Global Functions references
    Global<Function> import;
    Global<Function> require;
    Global<Function> runInThisContext;

    // State management
    std::map<int, Global<Promise>> promises;
    Global<v8::Map> visited;
};

// Forward declarations for module functions
void NodeInit(int thread_pool_size);
void NodeFree();
NodeEnv* NodeEnvCreate(const char* absolute_path);
void NodeEnvFree(NodeEnv* node);

// Type declarations
typedef struct NodeJSObject {
    PyObject_HEAD NodeEnv* node;
} NodeJSObject;

typedef struct JSSymbol JSSymbol;

extern PyObject* re_compile_func;

// Macro for V8 Scoping (standardized)
#define V8_SCOPE(node_env)                                          \
    v8::Locker locker_##__LINE__(node_env->isolate);                \
    v8::Isolate::Scope isolate_scope_##__LINE__(node_env->isolate); \
    v8::HandleScope handle_scope_##__LINE__(node_env->isolate);     \
    v8::Context::Scope context_scope_##__LINE__(node_env->setup->context());

// Declaration for Python module initialization
PyMODINIT_FUNC PyInit__pythonodejs(void);
