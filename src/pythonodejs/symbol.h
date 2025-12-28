#pragma once
#include "common.h"

// JSSymbolData struct
typedef struct {
    NodeEnv* node;
    Global<v8::Symbol>* symbol;
    char* name;
    Py_hash_t hash;
} JSSymbolData;

// JSSymbol type definition
typedef struct JSSymbol {
    PyObject_HEAD;
    PyObject* capsule;
} JSSymbol;

// JSSymbol methods
PyObject* JSSymbol_repr(JSSymbol* self);
void JSSymbol_dealloc(JSSymbol* self);
Py_hash_t JSSymbol_hash(JSSymbol* self);
PyObject* JSSymbol_richcompare(JSSymbol* self, PyObject* other, int op);
JSSymbol* JSSymbol_New(PyObject* capsule);

// JSSymbolType declaration
extern PyTypeObject JSSymbolType;
