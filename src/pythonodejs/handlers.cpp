#include "handlers.h"
#include "common.h"
#include "conversion.h"
#include "symbol.h"
#include <Python.h>
#include <vector>

using namespace std;

// Handlers
static PyObject* js_promise_handler(PyObject* self, PyObject* future)
{
    JSPromiseData* data = (JSPromiseData*)PyCapsule_GetPointer(self, "promise_data");
    if (!data)
        return NULL;

    PyObject* exception = PyObject_CallMethod(future, "exception", NULL);
    if (exception && exception != Py_None) {
        NodeEnv* node = data->node;
        if (!node || !node->isolate) {
            Py_DECREF(exception);
            PyErr_SetString(PyExc_RuntimeError, "Node environment no longer valid");
            return NULL;
        }

        V8_SCOPE(node);
        Local<Promise::Resolver> resolver = data->js_resolver.Get(node->isolate);

        PyObject* exc_str = PyObject_Str(exception);
        const char* error_msg = exc_str ? PyUnicode_AsUTF8(exc_str) : "Unknown Python error";

        v8::Local<v8::String> js_error_msg = v8::String::NewFromUtf8(node->isolate, error_msg).ToLocalChecked();
        Local<Value> js_error = v8::Exception::Error(js_error_msg);

        Py_XDECREF(exc_str);
        Py_DECREF(exception);

        v8::TryCatch try_catch(node->isolate);
        v8::Maybe<bool> maybe_result = resolver->Reject(node->setup->context(), js_error);

        if (maybe_result.IsNothing()) {
            if (try_catch.HasCaught()) {
                v8::String::Utf8Value error(node->isolate, try_catch.Exception());
                fprintf(stderr, "Failed to reject promise: %s\n", *error);
            }
        } else {
            Local<Promise> promise = resolver->GetPromise();
            v8::Local<v8::Function> catch_handler = v8::Function::New(
                node->setup->context(),
                [](const v8::FunctionCallbackInfo<v8::Value>& args) {},
                v8::Local<v8::Value>())
                                                        .ToLocalChecked();
            (void)promise->Catch(node->setup->context(), catch_handler);
        }

        Py_RETURN_NONE;
    }

    Py_XDECREF(exception);

    PyObject* result = PyObject_CallMethod(future, "result", NULL);
    if (!result) {
        PyErr_Print();
        Py_RETURN_NONE;
    }

    {
        NodeEnv* node = data->node;
        if (!node || !node->isolate) {
            Py_DECREF(result);
            PyErr_SetString(PyExc_RuntimeError, "Node environment no longer valid");
            return NULL;
        }

        V8_SCOPE(node);
        Local<Promise::Resolver> resolver = data->js_resolver.Get(node->isolate);
        MaybeLocal<Value> maybe_js_value = PyToJS(node, result);
        Local<Value> js_value;
        if (!maybe_js_value.ToLocal(&js_value)) {
            // TODO handle error
        }

        v8::TryCatch try_catch(node->isolate);
        v8::Maybe<bool> maybe_result = resolver->Resolve(node->setup->context(), js_value);

        if (maybe_result.IsNothing()) {
            if (try_catch.HasCaught()) {
                v8::String::Utf8Value error(node->isolate, try_catch.Exception());
                fprintf(stderr, "Failed to resolve promise: %s\n", *error);
            }
        }
    }
    Py_DECREF(result);
    Py_RETURN_NONE;
}

static PyObject* js_func_handler(PyObject* self, PyObject* args)
{
    JSFunctionData* data = (JSFunctionData*)PyCapsule_GetPointer(self, "func_data");
    if (!data)
        return NULL;

    long nargs = PyTuple_Size(args);
    PyObject* result = NULL;
    {
        NodeEnv* node = data->node;
        V8_SCOPE(node);

        vector<Local<Value>> argv(nargs);

        Local<Function> func = data->js_func.Get(node->isolate);

        v8::TryCatch try_catch(node->isolate);
        for (int i = 0; i < nargs; i++) {
            MaybeLocal<Value> maybe_arg = PyToJS(node, PyTuple_GetItem(args, i));
            Local<Value> arg;
            if (!maybe_arg.ToLocal(&arg)) {
                v8::String::Utf8Value error(node->isolate, try_catch.Exception());
                const char* msg = *error ? *error : "Unknown V8 exception";
                PyErr_Format(PyExc_RuntimeError, "V8 error at function \"%s\": %s", data->method_def->ml_name, msg);
                return NULL;
            }
            argv[i] = arg;
        }

        PyThreadState* _save = PyEval_SaveThread();

        Local<Value> recv = node->setup->context()->Global();

        MaybeLocal<Value> maybe_result = func->Call(node->setup->context(), recv, nargs, argv.data());

        PyEval_RestoreThread(_save);

        if (maybe_result.IsEmpty()) {
            if (try_catch.HasCaught()) {
                v8::String::Utf8Value error(node->isolate, try_catch.Exception());
                PyErr_SetString(PyExc_RuntimeError, *error);
            } else {
                PyErr_SetString(PyExc_RuntimeError, "JS function call failed");
            }
            return NULL;
        }

        result = JSToPy(node, maybe_result.ToLocalChecked());
    }

    return result;
}

void py_func_handler(const v8::FunctionCallbackInfo<v8::Value>& args)
{
    Isolate* isolate = args.GetIsolate();

    v8::Local<v8::External> data = v8::Local<v8::External>::Cast(args.Data());
    PyFunctionData* func_data = (PyFunctionData*)data->Value();

    PyObject* func = func_data->py_func;

    PyGILState_STATE gstate = PyGILState_Ensure();

    int nargs = args.Length();
    PyObject* py_args = PyTuple_New(nargs);
    if (!py_args) {
        PyGILState_Release(gstate);
        isolate->ThrowException(v8::Exception::Error(v8::String::NewFromUtf8(isolate, "Failed to create Python tuple").ToLocalChecked()));
        return;
    }

    for (int i = 0; i < nargs; i++) {
        PyObject* py_arg = JSToPy(func_data->node, args[i]);
        if (!py_arg) {
            PyGILState_Release(gstate);
            Py_DECREF(py_args);
            isolate->ThrowException(v8::Exception::Error(v8::String::NewFromUtf8(isolate, "Failed to convert JS argument to Python").ToLocalChecked()));
            return;
        }
        PyTuple_SET_ITEM(py_args, i, py_arg);
    }

    PyObject* res = PyObject_CallObject(func, py_args);
    Py_DECREF(py_args);

    if (!res) {
        PyErr_Print();
        PyGILState_Release(gstate);
        isolate->ThrowException(v8::Exception::Error(v8::String::NewFromUtf8(isolate, "Python function raised an exception").ToLocalChecked()));
        return;
    }

    v8::TryCatch try_catch(isolate);

    MaybeLocal<Value> maybe_js_result = PyToJS(func_data->node, res);
    Py_DECREF(res);

    Local<Value> js_result;
    if (!maybe_js_result.ToLocal(&js_result)) {
        try_catch.ReThrow();
        PyGILState_Release(gstate);
        return;
    }
    args.GetReturnValue().Set(js_result);

    PyGILState_Release(gstate);
}

static void cleanup_py_promise(PyObject* capsule)
{
    JSPromiseData* data = (JSPromiseData*)PyCapsule_GetPointer(capsule, "promise_data");
    if (data) {
        data->js_resolver.Reset();
        if (data->method_def) {
            PyMem_Free((void*)data->method_def->ml_name);
            PyMem_Free(data->method_def);
            data->method_def = nullptr;
        }
        PyMem_Free(data);
    }
}

void cleanup_py_function(const v8::WeakCallbackInfo<PyFunctionData>& info)
{
    delete info.GetParameter();
}

static void cleanup_js_func(PyObject* capsule)
{
    JSFunctionData* data = (JSFunctionData*)PyCapsule_GetPointer(capsule, "func_data");
    if (data) {
        data->js_func.Reset();
        if (data->method_def) {
            PyMem_Free((char*)data->method_def->ml_name);
            PyMem_Free(data->method_def);
        }
        PyMem_Free(data);
    }
}
