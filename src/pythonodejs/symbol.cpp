#include "symbol.h"
#include "common.h"
#include "conversion.h"
#include <Python.h>

PyObject* JSSymbol_repr(JSSymbol* self)
{
    if (self->capsule && PyCapsule_CheckExact(self->capsule)) {
        const char* capsule_name = PyCapsule_GetName(self->capsule);
        if (capsule_name != NULL) {
            return PyUnicode_FromFormat("<JSSymbol: Symbol(%s)>", capsule_name);
        }
    }
    return PyUnicode_FromString("<JSSymbol: Symbol>");
}

void JSSymbol_dealloc(JSSymbol* self)
{
    JSSymbolData* data = (JSSymbolData*)PyCapsule_GetPointer(self->capsule, NULL);
    if (data) {
        data->symbol->Reset();
        delete data->symbol;
        if (data->name) {
            free(data->name);
        }
        delete data;
    }
    Py_XDECREF(self->capsule);
    Py_TYPE(self)->tp_free((PyObject*)self);
}

Py_hash_t JSSymbol_hash(JSSymbol* self)
{
    JSSymbolData* data = (JSSymbolData*)PyCapsule_GetPointer(self->capsule, NULL);
    return data ? data->hash : -1;
}

PyObject* JSSymbol_richcompare(JSSymbol* self, PyObject* other, int op)
{
    if (!PyObject_TypeCheck(other, &JSSymbolType) || op == Py_LT || op == Py_GT) {
        Py_RETURN_FALSE;
    }

    JSSymbol* o = (JSSymbol*)other;
    JSSymbolData* data = (JSSymbolData*)PyCapsule_GetPointer(self->capsule, NULL);
    JSSymbolData* other_data = (JSSymbolData*)PyCapsule_GetPointer(o->capsule, NULL);
    if (!data || !other_data || data->node != other_data->node) {
        PyErr_SetString(PyExc_RuntimeError, "Invalid NodeJS environment");
        return NULL;
    }
    V8_SCOPE(data->node);
    Local<v8::Symbol> symbol = data->symbol->Get(data->node->isolate);
    Local<v8::Symbol> other_symbol = other_data->symbol->Get(data->node->isolate);
    bool equals = symbol->StrictEquals(other_symbol);
    if (op == Py_EQ) {
        return equals ? Py_True : Py_False;
    } else if (op == Py_NE) {
        return equals ? Py_False : Py_True;
    }
    Py_RETURN_FALSE;
}

JSSymbol* JSSymbol_New(PyObject* capsule)
{
    if (!PyCapsule_CheckExact(capsule)) {
        PyErr_SetString(PyExc_TypeError, "Expected a PyCapsule object");
        return NULL;
    }

    JSSymbol* self = PyObject_New(JSSymbol, &JSSymbolType);
    if (!self) {
        return NULL;
    }

    Py_INCREF(capsule);
    self->capsule = capsule;
    return self;
}

static PyMethodDef JSSymbol_methods[] = {
    { NULL } /* Sentinel */
};

PyTypeObject JSSymbolType = {
    .ob_base = { { { 1 }, (&PyType_Type) }, (0) },
    .tp_name = "pythonodejs.JSSymbol",
    .tp_basicsize = sizeof(JSSymbol),
    .tp_itemsize = 0,
    .tp_dealloc = (destructor)JSSymbol_dealloc,
    .tp_repr = (reprfunc)JSSymbol_repr,
    .tp_hash = (hashfunc)JSSymbol_hash,
    .tp_flags = Py_TPFLAGS_DEFAULT,
    .tp_richcompare = (richcmpfunc)JSSymbol_richcompare,
    .tp_methods = JSSymbol_methods,
};
