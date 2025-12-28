#include "common.h"
#include "conversion.h"
#include "datetime.h"
#include "handlers.h"
#include "node_env.h"
#include "symbol.h"
#include "utils.h"

static bool node_initialized = false;
PyObject* re_compile_func = NULL;

static void pythonodejs_free(void* m)
{
    if (node_initialized)
        NodeFree();
    Py_XDECREF(re_compile_func);
}

int NodeJS_init(NodeJSObject* self, PyObject* args, PyObject* kwds)
{
    if (self->node) {
        NodeEnvFree(self->node);
        delete self->node;
    }
    const char* path = nullptr;
    int thread_pool_size = 4;

    static const char* kwlist[] = { "path", "thread_pool_size", nullptr };

    if (!PyArg_ParseTupleAndKeywords(args, kwds, "|s|i", const_cast<char**>(kwlist), &path, &thread_pool_size))
        return -1;

    if (!node_initialized) {
        NodeInit(thread_pool_size);
        node_initialized = true;
    }
    NodeEnv* node = NodeEnvCreate(path);
    if (!node) {
        PyErr_SetString(PyExc_RuntimeError, "Failed to create Node environment");
        return -1;
    }
    self->node = node;
    return 0;
}

PyObject* NodeJS_repr(NodeJSObject* self)
{
    return PyUnicode_FromString("NodeJS");
}

static void NodeJS_dealloc(NodeJSObject* self)
{
    if (self->node) {
        NodeEnvFree(self->node);
        delete self->node;
        self->node = nullptr;
    }
    Py_TYPE(self)->tp_free((PyObject*)self);
}

PyObject* NodeJS_eval(NodeJSObject* self, PyObject* code)
{
    if (!PyUnicode_Check(code)) {
        PyErr_SetString(PyExc_TypeError, "Expected a string");
        return nullptr;
    }

    const char* source = PyUnicode_AsUTF8(code);
    {
        V8_SCOPE(self->node);
        Local<Context> context = self->node->setup->context();
        Local<Function> eval_func = self->node->runInThisContext.Get(self->node->isolate);
        Local<Value> argv[] = { v8::String::NewFromUtf8(self->node->isolate, source).ToLocalChecked() };
        v8::TryCatch try_catch(self->node->isolate);

        Local<Value> res;
        if (!eval_func->Call(context, context->Global(), 1, argv).ToLocal(&res)) {
            v8::String::Utf8Value error(self->node->isolate, try_catch.Exception());
            PyErr_SetString(PyExc_RuntimeError, *error);
            return NULL;
        }
        if (res->IsPromise()) {
            Local<Promise> promise = res.As<Promise>();
            Local<Function> catch_handler = v8::Function::New(context, [](const v8::FunctionCallbackInfo<v8::Value>& args) {}, v8::Local<v8::Value>()).ToLocalChecked();
            promise->Catch(context, catch_handler);
        }
        return JSToPy(self->node, res);
    }
}

PyObject* NodeJS_require(NodeJSObject* self, PyObject* arg)
{
    if (!PyUnicode_Check(arg)) {
        PyErr_SetString(PyExc_TypeError, "Expected a string");
        return nullptr;
    }

    const char* url = PyUnicode_AsUTF8(arg);
    {
        V8_SCOPE(self->node);
        Local<Context> context = self->node->setup->context();
        Local<Function> require_func = self->node->require.Get(self->node->isolate);
        Local<Value> argv[] = { v8::String::NewFromUtf8(self->node->isolate, url).ToLocalChecked() };
        v8::TryCatch try_catch(self->node->isolate);

        Local<Value> res;
        if (!require_func->Call(context, context->Global(), 1, argv).ToLocal(&res)) {
            v8::String::Utf8Value error(self->node->isolate, try_catch.Exception());
            PyErr_SetString(PyExc_RuntimeError, *error);
            return NULL;
        }
        return JSToPy(self->node, res);
    }
}

PyObject* NodeJS_import(NodeJSObject* self, PyObject* arg)
{
    if (!PyUnicode_Check(arg)) {
        PyErr_SetString(PyExc_TypeError, "Expected a string");
        return nullptr;
    }

    const char* url = PyUnicode_AsUTF8(arg);
    {
        V8_SCOPE(self->node);
        Local<Context> context = self->node->setup->context();
        Local<Function> import_func = self->node->import.Get(self->node->isolate);
        Local<Value> argv[] = { v8::String::NewFromUtf8(self->node->isolate, url).ToLocalChecked() };
        v8::TryCatch try_catch(self->node->isolate);

        Local<Value> res;
        if (!import_func->Call(context, context->Global(), 1, argv).ToLocal(&res)) {
            v8::String::Utf8Value error(self->node->isolate, try_catch.Exception());
            PyErr_SetString(PyExc_RuntimeError, *error);
            return NULL;
        }
        return JSToPy(self->node, res);
    }
}

PyObject* NodeJS_poll(NodeJSObject* self)
{
    if (!self->node) {
        Py_RETURN_FALSE;
    }

    V8_SCOPE(self->node);

    self->node->setup->context()->GetMicrotaskQueue()->PerformCheckpoint(self->node->isolate);

    PollAsync(self->node);

    if (uv_loop_alive(self->node->loop)) {
        Py_RETURN_TRUE;
    } else {
        Py_RETURN_FALSE;
    }
}

static PyMethodDef NodeJS_methods[] = {
    { "eval_cjs", (PyCFunction)NodeJS_eval, METH_O, "Evaluates a piece of CJS code." },
    { "require_cjs", (PyCFunction)NodeJS_require, METH_O, "Imports a CJS file." },
    { "import_esm", (PyCFunction)NodeJS_import, METH_O, "Imports an ES module." },
    { "poll", (PyCFunction)NodeJS_poll, METH_NOARGS, "Runs one tick of the Node.js event loop." },
    { NULL } /* Sentinel */
};

static PyTypeObject NodeJSType = {
    PyVarObject_HEAD_INIT(NULL, 0) "pythonodejs.NodeJS", /* tp_name */
    sizeof(NodeJSObject), /* tp_basicsize */
    0, /* tp_itemsize */
    (destructor)NodeJS_dealloc, /* tp_dealloc */
    0, /* tp_vectorcall_offset */
    0, /* tp_getattr */
    0, /* tp_setattr */
    0, /* tp_as_async */
    (reprfunc)NodeJS_repr, /* tp_repr */
    0, /* tp_as_number */
    0, /* tp_as_sequence */
    0, /* tp_as_mapping */
    0, /* tp_hash */
    0, /* tp_call */
    0, /* tp_str */
    0, /* tp_getattro */
    0, /* tp_setattro */
    0, /* tp_as_buffer */
    Py_TPFLAGS_DEFAULT, /* tp_flags */
    0, /* tp_doc */
    0, /* tp_traverse */
    0, /* tp_clear */
    0, /* tp_richcompare */
    0, /* tp_weaklistoffset */
    0, /* tp_iter */
    0, /* tp_iternext */
    NodeJS_methods, /* tp_methods */
    0, /* tp_members */
    0, /* tp_getset */
    0, /* tp_base */
    0, /* tp_dict */
    0, /* tp_descr_get */
    0, /* tp_descr_set */
    0, /* tp_dictoffset */
    (initproc)NodeJS_init, /* tp_init */
    0, /* tp_alloc */
    PyType_GenericNew, /* tp_new */
};

static PyMethodDef module_methods[] = {
    { NULL }
};

static struct PyModuleDef module_def = {
    PyModuleDef_HEAD_INIT,
    "_pythonodejs",
    "Pythonodejs NodeJS Interop",
    -1,
    module_methods,
    NULL, NULL, NULL, pythonodejs_free
};

PyMODINIT_FUNC
PyInit__pythonodejs(void)
{
    if (PyType_Ready(&NodeJSType) < 0)
        return NULL;
    if (PyType_Ready(&JSSymbolType) < 0)
        return NULL;

    PyObject* m = PyModule_Create(&module_def);
    if (m == NULL)
        return NULL;

    PyModule_AddObject(m, "NodeJS", (PyObject*)&NodeJSType);
    PyDateTime_IMPORT;

    Py_INCREF(&NodeJSType);
    PyModule_AddObject(m, "NodeJS", (PyObject*)&NodeJSType);

    Py_INCREF(&JSSymbolType);
    PyModule_AddObject(m, "JSSymbol", (PyObject*)&JSSymbolType);

    PyObject* re_module = PyImport_ImportModule("re");
    if (re_module == NULL) {
        PyErr_Print();
        Py_DECREF(m);
        return NULL;
    }
    re_compile_func = PyObject_GetAttrString(re_module, "compile");
    Py_DECREF(re_module);
    if (re_compile_func == NULL) {
        PyErr_Print();
        Py_DECREF(m);
        return NULL;
    }

    return m;
}
