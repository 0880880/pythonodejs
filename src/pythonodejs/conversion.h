#pragma once
#include "common.h"

PyObject* JSToPy(NodeEnv* node, Local<Value> value);
MaybeLocal<Value> PyToJS(NodeEnv* node, PyObject* value);
int is_coroutine_like(PyObject* obj);
