#pragma once
#include "common.h"

// Struct definitions
typedef struct {
    NodeEnv* node;
    Global<Function> js_func;
    PyMethodDef* method_def;
} JSFunctionData;

typedef struct {
    NodeEnv* node;
    Global<Promise::Resolver> js_resolver;
    PyMethodDef* method_def;
    char* method_name;
} JSPromiseData;

typedef struct {
    NodeEnv* node;
    PyObject* py_func;
} PyFunctionData;

typedef struct {
    NodeEnv* node;
    PyObject* future;
    PyObject* loop;
} PyAwaitableData;

// Function declarations
PyObject* js_func_handler(PyObject* self, PyObject* args);
void py_func_handler(const v8::FunctionCallbackInfo<v8::Value>& args);
PyObject* js_promise_handler(PyObject* self, PyObject* future);
void py_awaitable_handler(const v8::FunctionCallbackInfo<v8::Value>& args);
void cleanup_py_promise(PyObject* capsule);
void cleanup_py_awaitable(const v8::WeakCallbackInfo<PyAwaitableData>& info);
void cleanup_py_function(const v8::WeakCallbackInfo<PyFunctionData>& info);
void cleanup_js_func(PyObject* capsule);
