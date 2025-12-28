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

// Function declarations
static PyObject* js_func_handler(PyObject* self, PyObject* args);
void py_func_handler(const v8::FunctionCallbackInfo<v8::Value>& args);
static PyObject* js_promise_handler(PyObject* self, PyObject* future);
static void cleanup_py_promise(PyObject* capsule);
void cleanup_py_function(const v8::WeakCallbackInfo<PyFunctionData>& info);
static void cleanup_js_func(PyObject* capsule);
