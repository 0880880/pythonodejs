#include "conversion.h"
#include "abstract.h"
#include "bytesobject.h"
#include "common.h"
#include "handlers.h"
#include "node_env.h"
#include "object.h"
#include "pyerrors.h"
#include "setobject.h"
#include "symbol.h"
#include "utils.h"
#include "v8-promise.h"
#include "v8-typed-array.h"
#include <Python.h>
#include <cmath>
#include <ctime>
#include <datetime.h>
#include <vector>

using std::string;
using std::vector;
using v8::Array;
using v8::Boolean;
using v8::Context;
using v8::Date;
using v8::External;
using v8::Function;
using v8::FunctionCallbackInfo;
using v8::FunctionTemplate;
using v8::Global;
using v8::HandleScope;
using v8::Int32;
using v8::Isolate;
using v8::Local;
using v8::MaybeLocal;
using v8::NewStringType;
using v8::Null;
using v8::Number;
using v8::NumberObject;
using v8::Object;
using v8::Promise;
using v8::Set;
using v8::String;
using v8::StringObject;
using v8::Symbol;
using v8::Value;

extern PyObject* re_compile_func; // From module.cpp

int is_coroutine_like(PyObject* obj)
{
    if (PyCoro_CheckExact(obj)) {
        return 1;
    }

    if (PyObject_HasAttrString(obj, "__await__")) {
        return 1;
    }

    return 0;
}

MaybeLocal<Value> PyToJS(NodeEnv* node, PyObject* value)
{
    Local<Context> context = node->setup->context();

    if (value == Py_None) {
        return Null(node->isolate);
    } else if (PyBool_Check(value)) {
        return Boolean::New(node->isolate, value == Py_True);
    } else if (PyLong_Check(value)) {
        int overflow = 0;
        long long num = PyLong_AsLongLongAndOverflow(value, &overflow);
        if (overflow != 0) {
            Py_ssize_t nbytes = _PyLong_NumBits(value);
            if (nbytes < 0) {
                return Null(node->isolate);
            }
            size_t nwords = (nbytes + 63) / 64;
            uint64_t* words = (uint64_t*)malloc(nwords * sizeof(uint64_t));
            if (_PyLong_AsByteArray((PyLongObject*)PyNumber_Absolute(value), (unsigned char*)words, nwords * 8, 1, 0) < 0) {
                free(words);
                return Null(node->isolate);
            }
            int sign = _PyLong_Sign(value) == -1 ? 1 : 0;
            MaybeLocal<v8::BigInt> b = v8::BigInt::NewFromWords(context, sign, nwords, words);
            free(words);
            return b;
        } else if (num < INT32_MIN || num > INT32_MAX) {
            return v8::BigInt::New(node->isolate, num);
        }
        return Int32::New(node->isolate, static_cast<int32_t>(num));
    } else if (PyFloat_Check(value)) {
        return Number::New(node->isolate, PyFloat_AsDouble(value));
    } else if (PyUnicode_Check(value)) {
        return String::NewFromUtf8(node->isolate, PyUnicode_AsUTF8(value), NewStringType::kNormal);
    } else if (PyFunction_Check(value)) {
        PyFunctionData* data = new PyFunctionData { node, value };
        Local<External> ext = External::New(node->isolate, data);
        v8::Persistent<v8::External> persistent(node->isolate, ext);
        persistent.SetWeak(data, cleanup_py_function, v8::WeakCallbackType::kParameter);
        Local<FunctionTemplate> tpl = FunctionTemplate::New(node->isolate, py_func_handler, ext);
        return tpl->GetFunction(node->isolate->GetCurrentContext());
    } else if (PyObject_TypeCheck(value, &JSSymbolType)) {
        JSSymbol* jsym = (JSSymbol*)value;
        JSSymbolData* data = (JSSymbolData*)PyCapsule_GetPointer(jsym->capsule, NULL);
        return data->symbol->Get(node->isolate);
    } else if (PyExceptionClass_Check(value)) {
        PyObject* exc = PyObject_CallObject(value, NULL);
        if (!exc) {
            return Null(node->isolate);
        }
        PyObject* args = PyObject_GetAttrString(exc, "args");
        if (args && PyTuple_Check(args)) {
            Py_ssize_t n = PyTuple_Size(args);
            if (n > 0) {
                PyObject* first_arg = PyTuple_GetItem(args, 0);
                if (PyUnicode_Check(first_arg)) {
                    const char* msg = PyUnicode_AsUTF8(first_arg);
                    Local<String> message = v8::String::NewFromUtf8(node->isolate, msg).ToLocalChecked();
                    Py_XDECREF(args);
                    Py_DECREF(exc);
                    return v8::Exception::Error(message);
                }
            }
        }
        Py_XDECREF(args);
        Py_DECREF(exc);
        return Null(node->isolate);
    } else if (PyDateTime_Check(value)) {
        int year = PyDateTime_GET_YEAR(value);
        int month = PyDateTime_GET_MONTH(value);
        int day = PyDateTime_GET_DAY(value);
        int hour = PyDateTime_DATE_GET_HOUR(value);
        int min = PyDateTime_DATE_GET_MINUTE(value);
        int sec = PyDateTime_DATE_GET_SECOND(value);
        struct tm t;
        t.tm_year = year - 1900;
        t.tm_mon = month - 1;
        t.tm_mday = day;
        t.tm_hour = hour;
        t.tm_min = min;
        t.tm_sec = sec;
        t.tm_isdst = -1;
        time_t unix_time = mktime(&t);
        double unix_ms = static_cast<double>(unix_time) * 1000;
        return v8::Date::New(context, unix_ms);
    } else if (PyBytes_Check(value)) {
        char* buf = PyBytes_AsString(value);
        Py_ssize_t len = PyBytes_Size(value);

        Local<v8::ArrayBuffer> ab = v8::ArrayBuffer::New(node->isolate, len);
        memcpy(ab->GetBackingStore()->Data(), buf, len);

        return v8::Uint8Array::New(ab, 0, len);
    } else if (PyList_Check(value)) {
        int len = PyList_Size(value);
        Local<Array> arr = Array::New(node->isolate, len);
        for (int i = 0; i < len; i++) {
            arr->Set(context, i, PyToJS(node, PyList_GetItem(value, i)).ToLocalChecked()).Check();
        }
        return arr;
    } else if (PyTuple_Check(value)) {
        int len = PyTuple_Size(value);
        Local<Array> arr = Array::New(node->isolate, len);
        for (int i = 0; i < len; i++) {
            arr->Set(context, i, PyToJS(node, PyTuple_GetItem(value, i)).ToLocalChecked()).Check();
        }
        return arr;
    } else if (PySet_Check(value)) {
        Local<v8::Set> js_set = v8::Set::New(node->isolate);
        PyObject* iterator = PyObject_GetIter(value);
        PyObject* item;

        if (iterator == NULL)
            return Null(node->isolate);

        while ((item = PyIter_Next(iterator))) {
            Local<Value> js_val = PyToJS(node, item).ToLocalChecked();
            js_set->Add(context, js_val).ToLocalChecked();
            Py_DECREF(item);
        }
        Py_DECREF(iterator);
        return js_set;
    } else if (PyMapping_Check(value)) {
        Py_ssize_t len = PyMapping_Length(value);
        if (len < 0) {
            PyErr_Clear();
            return Null(node->isolate);
        }
        Local<Object> obj = Object::New(node->isolate);
        PyObject* items = PyMapping_Items(value);
        for (Py_ssize_t i = 0; i < len; i++) {
            PyObject* tuple = PyList_GET_ITEM(items, i);
            PyObject* py_key = PyTuple_GET_ITEM(tuple, 0);
            PyObject* py_val = PyTuple_GET_ITEM(tuple, 1);
            obj->Set(context, v8::String::NewFromUtf8(node->isolate, PyUnicode_AsUTF8(py_key)).ToLocalChecked(), PyToJS(node, py_val).ToLocalChecked()).Check();
        }
        Py_DECREF(items);
        return obj;
    } else if (is_coroutine_like(value)) {
        PyObject* asyncio = PyImport_ImportModule("asyncio");
        if (!asyncio)
            return Null(node->isolate);
        MaybeLocal<Promise::Resolver> maybe_resolver = Promise::Resolver::New(context);
        if (maybe_resolver.IsEmpty()) {
            Py_DECREF(asyncio);
            return Null(node->isolate);
        }
        Local<Promise::Resolver> resolver = maybe_resolver.ToLocalChecked();
        Local<Promise> promise = resolver->GetPromise();
        v8::Local<v8::Function> catch_handler = v8::Function::New(context, [](const v8::FunctionCallbackInfo<v8::Value>& args) {}, v8::Local<v8::Value>()).ToLocalChecked();
        (void)promise->Catch(context, catch_handler);
        JSPromiseData* data = (JSPromiseData*)PyMem_Malloc(sizeof(JSPromiseData));
        if (!data) {
            Py_DECREF(asyncio);
            return Null(node->isolate);
        }
        data->node = node;
        new (&data->js_resolver) Global<Promise::Resolver>();
        data->js_resolver.Reset(node->isolate, resolver);
        PyObject* capsule = PyCapsule_New(data, "promise_data", cleanup_py_promise);
        if (!capsule) {
            data->js_resolver.Reset();
            PyMem_Free(data);
            Py_DECREF(asyncio);
            return Null(node->isolate);
        }
        PyMethodDef* def = (PyMethodDef*)PyMem_Malloc(sizeof(PyMethodDef));
        if (!def) {
            Py_DECREF(capsule);
            Py_DECREF(asyncio);
            return Null(node->isolate);
        }
        std::string name_str = "callback_" + random_string();
        char* name_copy = (char*)PyMem_Malloc(name_str.length() + 1);
        if (!name_copy) {
            PyMem_Free(def);
            Py_DECREF(capsule);
            Py_DECREF(asyncio);
            return Null(node->isolate);
        }
        strcpy(name_copy, name_str.c_str());
        def->ml_name = name_copy;
        def->ml_meth = js_promise_handler;
        def->ml_flags = METH_VARARGS;
        def->ml_doc = "";
        data->method_def = def;
        data->method_name = name_copy;
        PyObject* callback = PyCFunction_NewEx(def, capsule, NULL);
        if (!callback) {
            PyMem_Free(name_copy);
            PyMem_Free(def);
            Py_DECREF(asyncio);
            return Null(node->isolate);
        }
        PyObject* create_task = PyObject_GetAttrString(asyncio, "create_task");
        if (!create_task) {
            PyErr_Print();
            Py_DECREF(callback);
            PyMem_Free(name_copy);
            PyMem_Free(def);
            Py_DECREF(asyncio);
            return Null(node->isolate);
        }
        PyObject* task = PyObject_CallFunctionObjArgs(create_task, value, NULL);
        Py_DECREF(create_task);
        if (!task) {
            PyErr_Print();
            Py_DECREF(callback);
            PyMem_Free(name_copy);
            PyMem_Free(def);
            Py_DECREF(asyncio);
            return Null(node->isolate);
        }
        PyObject* result = PyObject_CallMethod(task, "add_done_callback", "O", callback);
        Py_XDECREF(result);
        Py_DECREF(task);
        Py_DECREF(callback);
        Py_DECREF(asyncio);
        return promise;
    } else if (PyObject_HasAttrString(value, "__dict__")) {
        PyObject* dict = PyObject_GetAttrString(value, "__dict__");
        if (!dict || !PyMapping_Check(dict)) {
            Py_XDECREF(dict);
            return Null(node->isolate);
        }
        Py_ssize_t len = PyMapping_Length(dict);
        if (len < 0) {
            PyErr_Clear();
            Py_DECREF(dict);
            return Null(node->isolate);
        }
        Local<Object> obj = Object::New(node->isolate);
        PyObject* items = PyMapping_Items(dict);
        for (Py_ssize_t i = 0; i < len; i++) {
            PyObject* tuple = PyList_GET_ITEM(items, i);
            PyObject* py_key = PyTuple_GET_ITEM(tuple, 0);
            PyObject* py_val = PyTuple_GET_ITEM(tuple, 1);
            obj->Set(context, v8::String::NewFromUtf8(node->isolate, PyUnicode_AsUTF8(py_key)).ToLocalChecked(), PyToJS(node, py_val).ToLocalChecked()).Check();
        }
        Py_DECREF(items);
        Py_DECREF(dict);
        return obj;
    } else {
        node->isolate->ThrowException(v8::Exception::Error(v8::String::NewFromUtf8Literal(node->isolate, "Cannot convert object to JS type.")));
        return MaybeLocal<Value>();
    }
}

PyObject* JSToPy(NodeEnv* node, Local<Value> value)
{
    Local<Context> context = node->setup->context();
    Local<v8::Map> visited = node->visited.Get(node->isolate);
    if (value.IsEmpty() || value->IsNullOrUndefined()) {
        Py_RETURN_NONE;
    } else if (value->IsBoolean()) {
        return value.As<Boolean>()->Value() ? Py_True : Py_False;
    } else if (value->IsBigInt()) {
        Local<String> str = value.As<v8::BigInt>()->ToString(context).ToLocalChecked();
        v8::String::Utf8Value utf8(node->isolate, str);
        return PyLong_FromString(*utf8, NULL, 10);
    } else if (value->IsInt32()) {
        return PyLong_FromLong(value->Int32Value(context).FromJust());
    } else if (value->IsNumber()) {
        return PyFloat_FromDouble(value->NumberValue(context).FromJust());
    } else if (value->IsNumberObject()) {
        return PyFloat_FromDouble(value.As<NumberObject>()->ValueOf());
    } else if (value->IsString()) {
        Local<String> str = value.As<String>();
        v8::String::Utf8Value utf8(node->isolate, str);
        return PyUnicode_FromString(*utf8);
    } else if (value->IsStringObject()) {
        Local<StringObject> obj = value.As<StringObject>();
        v8::String::Utf8Value utf8(node->isolate, obj->ValueOf());
        return PyUnicode_FromString(*utf8);
    } else if (value->IsArrayBufferView()) {
        Local<v8::ArrayBufferView> view = value.As<v8::ArrayBufferView>();
        size_t len = view->ByteLength();
        size_t offset = view->ByteOffset();

        Local<v8::ArrayBuffer> buffer = view->Buffer();
        std::shared_ptr<v8::BackingStore> store = buffer->GetBackingStore();
        char* data = static_cast<char*>(store->Data());

        return PyBytes_FromStringAndSize(data + offset, len);
    } else if (value->IsNativeError()) {
        Local<Object> err = value.As<Object>();
        Local<String> msg_key = v8::String::NewFromUtf8(node->isolate, "message").ToLocalChecked();
        Local<Value> message;
        if (err->Get(context, msg_key).ToLocal(&message)) {
            v8::String::Utf8Value msg_str(node->isolate, message);
            PyErr_SetString(PyExc_RuntimeError, *msg_str);
            return NULL;
        }
        PyErr_SetString(PyExc_RuntimeError, "JS Error");
        return NULL;
    } else if (value->IsDate()) {
        Local<Date> pd = value.As<Date>();
        double ms = pd->ValueOf();
        time_t seconds = ms / 1000;
        int microseconds = static_cast<int>(fmod(ms, 1000.0)) * 1000;
        struct tm timeinfo;
#ifdef _WIN32
        if (gmtime_s(&timeinfo, &seconds) != 0) {
#else
        if (gmtime_r(&seconds, &timeinfo) == NULL) {
#endif
            PyErr_SetString(PyExc_ValueError, "Invalid timestamp");
            return NULL;
        }
        return PyDateTime_FromDateAndTime(timeinfo.tm_year + 1900, timeinfo.tm_mon + 1, timeinfo.tm_mday, timeinfo.tm_hour, timeinfo.tm_min, timeinfo.tm_sec, microseconds);
    } else if (value->IsFunction()) {
        Local<Function> js_func = value.As<Function>();
        JSFunctionData* data = (JSFunctionData*)PyMem_Malloc(sizeof(JSFunctionData));
        if (!data)
            return NULL;
        data->node = node;
        new (&data->js_func) Global<Function>();
        data->js_func.Reset(node->isolate, js_func);
        Local<String> name_str = js_func->GetName()->ToString(context).ToLocalChecked();
        v8::String::Utf8Value name_utf8(node->isolate, name_str);
        char* name_copy = (char*)PyMem_Malloc(name_utf8.length() + 1);
        strcpy(name_copy, *name_utf8);
        PyMethodDef* def = (PyMethodDef*)PyMem_Malloc(sizeof(PyMethodDef));
        def->ml_name = name_copy;
        def->ml_meth = js_func_handler;
        def->ml_flags = METH_VARARGS;
        def->ml_doc = NULL;
        data->method_def = def;
        PyObject* capsule = PyCapsule_New(data, "func_data", cleanup_js_func);
        PyObject* func_obj = PyCFunction_NewEx(def, capsule, NULL);
        visited->Set(context, js_func, External::New(node->isolate, func_obj)).ToLocalChecked();
        return func_obj;
    } else if (value->IsArray()) {
        Local<Array> arr = value.As<Array>();
        PyObject* list = PyList_New(arr->Length());
        visited->Set(context, arr, External::New(node->isolate, list)).ToLocalChecked();
        for (uint32_t i = 0; i < arr->Length(); i++) {
            Local<Value> item = arr->Get(context, i).ToLocalChecked();
            if (!visited->Has(context, item).ToChecked()) {
                PyList_SET_ITEM(list, i, JSToPy(node, item));
            }
        }
        visited->Delete(context, arr).ToChecked();
        return list;
    } else if (value->IsSet()) {
        Local<Set> set = value.As<Set>();
        PyObject* py_set = PySet_New(NULL);
        Local<Array> arr = set->AsArray();
        visited->Set(context, set, External::New(node->isolate, py_set)).ToLocalChecked();
        for (uint32_t i = 0; i < arr->Length(); i++) {
            Local<Value> item = arr->Get(context, i).ToLocalChecked();
            if (!visited->Has(context, item).ToChecked()) {
                PySet_Add(py_set, JSToPy(node, item));
            }
        }
        visited->Delete(context, set).ToChecked();
        return py_set;
    } else if (value->IsMap()) {
        Local<v8::Map> js_map = value.As<v8::Map>();
        Local<Array> as_array = js_map->AsArray(); // [key, val, ...]
        PyObject* py_dict = PyDict_New();

        visited->Set(context, js_map, External::New(node->isolate, py_dict)).ToLocalChecked();

        for (uint32_t i = 0; i < as_array->Length(); i += 2) {
            Local<Value> k = as_array->Get(context, i).ToLocalChecked();
            Local<Value> v = as_array->Get(context, i + 1).ToLocalChecked();

            PyObject* py_key = JSToPy(node, k);
            PyObject* py_val = JSToPy(node, v);

            if (py_key && py_val) {
                PyDict_SetItem(py_dict, py_key, py_val);
            }
            Py_XDECREF(py_key);
            Py_XDECREF(py_val);
        }
        visited->Delete(context, js_map).ToChecked();
        return py_dict;
    } else if (value->IsSymbol()) {
        Local<Symbol> symbol = value.As<Symbol>();
        Global<Symbol>* global_symbol = new Global<Symbol>();
        global_symbol->Reset(node->isolate, symbol);
        Local<Value> description = symbol->Description(node->isolate);
        v8::String::Utf8Value utf8(node->isolate, description);
        char* name = strdup(*utf8 ? *utf8 : "");
        JSSymbolData* data = new JSSymbolData();
        data->node = node;
        data->symbol = global_symbol;
        data->name = name;
        PyObject* s = PyUnicode_FromString("JS");
        PyObject* i = PyLong_FromLong(symbol->GetIdentityHash());
        PyObject* t = PyTuple_New(2);
        PyTuple_SET_ITEM(t, 0, s);
        PyTuple_SET_ITEM(t, 1, i);
        Py_hash_t h = PyObject_Hash(t);
        Py_DECREF(t);
        data->hash = h;
        PyObject* capsule = PyCapsule_New(data, name, NULL);
        return (PyObject*)JSSymbol_New(capsule);
    } else if (value->IsRegExp()) {
        Local<v8::RegExp> regex = value.As<v8::RegExp>();
        v8::String::Utf8Value source_utf8(node->isolate, regex->GetSource());
        PyObject* pattern_obj = PyUnicode_FromString(*source_utf8);
        if (!pattern_obj)
            return NULL;
        PyObject* compiled = PyObject_CallFunctionObjArgs(re_compile_func, pattern_obj, NULL);
        Py_DECREF(pattern_obj);
        if (!compiled) {
            PyErr_Print();
            return NULL;
        }
        return compiled;
    } else if (value->IsPromise()) {
        Local<v8::Promise> promise = value.As<v8::Promise>();
        PyObject* loop = NULL;
        PyObject* future = NULL;

        PyObject* asyncio_mod = PyImport_ImportModule("asyncio");
        if (!asyncio_mod)
            return NULL;

        loop = PyObject_CallMethod(asyncio_mod, "get_running_loop", NULL);
        Py_DECREF(asyncio_mod);

        if (!loop) {
            return NULL; // Not inside async code
        }

        future = PyObject_CallMethod(loop, "create_future", NULL);
        if (!future) {
            Py_DECREF(loop);
            return NULL;
        }

        PyAwaitableData* data = new PyAwaitableData { .node = node, .future = future, .loop = loop };

        // We must keep 'future' and 'loop' alive.
        Py_INCREF(future);
        Py_INCREF(loop);

        Local<External> ext = External::New(node->isolate, data);
        v8::Persistent<v8::External> persistent(node->isolate, ext);
        persistent.SetWeak(data, cleanup_py_awaitable, v8::WeakCallbackType::kParameter);
        Local<FunctionTemplate> tpl = FunctionTemplate::New(node->isolate, py_awaitable_handler, ext);

        (void)promise->Then(node->isolate->GetCurrentContext(), tpl->GetFunction(node->isolate->GetCurrentContext()).ToLocalChecked());
    } else {
        Local<Object> obj = value.As<Object>();
        Local<Array> keys = obj->GetOwnPropertyNames(context).ToLocalChecked();
        PyObject* dict = PyDict_New();
        visited->Set(context, obj, External::New(node->isolate, dict)).ToLocalChecked();
        for (uint32_t i = 0; i < keys->Length(); i++) {
            Local<Value> key = keys->Get(context, i).ToLocalChecked();
            Local<String> str_key = key.As<String>();
            v8::String::Utf8Value utf8(node->isolate, str_key);
            Local<Value> val = obj->Get(context, key).ToLocalChecked();
            if (!visited->Has(context, val).ToChecked()) {
                PyDict_SetItemString(dict, *utf8, JSToPy(node, val));
            }
        }
        visited->Delete(context, obj).ToChecked();
        return dict;
    }
    PyErr_SetString(PyExc_RuntimeError, "Unable to convert Javascript type to Python type");
    return NULL;
}
