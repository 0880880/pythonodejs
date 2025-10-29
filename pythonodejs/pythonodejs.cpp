#include "common.h"

#include <Python.h>
#include <cmath>
#include <datetime.h>
#include <random>
#include <string>
#include <vector>

#include "cppgc/platform.h"
#include "dictobject.h"
#include "env.h"
#include "floatobject.h"
#include "listobject.h"
#include "longobject.h"
#include "node_realm.h"
#include "object.h"
#include "pyerrors.h"
#include "unicodeobject.h"
#include "v8-external.h"
#include "v8-local-handle.h"
#include "v8-object.h"
#include "v8-persistent-handle.h"
#include "v8-primitive.h"
#include <assert.h>

using std::mt19937;
using std::random_device;
using std::string;
using std::uniform_int_distribution;
using std::unique_ptr;
using std::vector;

namespace _nodejs
    = node;
using node::CommonEnvironmentSetup;
using node::Environment;
using node::MultiIsolatePlatform;
using node::Realm;
using v8::Array;
using v8::External;
using v8::FunctionCallbackInfo;
using v8::FunctionTemplate;
using v8::HandleScope;
using v8::MaybeLocal;
using v8::NumberObject;
using v8::Object;
using v8::Persistent;
using v8::Promise;
using v8::SealHandleScope;
using v8::Set;
using v8::String;
using v8::StringObject;
using v8::V8;
using v8::Value;

#ifdef _WIN32
#include <windows.h>
#define NODE_MAIN int wmain

void FixupMain(int argc, char* raw_argv[], char*** argv)
{
    *argv = raw_argv;
}
#else

using argv_type = char*;
#define NODE_MAIN int main

void FixupMain(int argc, argv_type raw_argv[], char*** argv)
{
    *argv = uv_setup_args(argc, raw_argv);
    // Disable stdio buffering, it interacts poorly with printf()
    // calls elsewhere in the program (e.g., any logging from V8.)
    setvbuf(stdout, nullptr, _IONBF, 0);
    setvbuf(stderr, nullptr, _IONBF, 0);
}
#endif

static void VerifyRealm(Realm* realm)
{
    realm->VerifyNoStrongBaseObjects();
}

void SpinEventLoopAsync(Environment* env)
{
    CHECK_NOT_NULL(env);
    MultiIsolatePlatform* platform = GetMultiIsolatePlatform(env);
    CHECK_NOT_NULL(platform);

    Isolate* isolate = env->isolate();
    HandleScope handle_scope(isolate);
    Context::Scope context_scope(env->context());
    SealHandleScope seal(isolate);

    env->set_trace_sync_io(env->options()->trace_sync_io);
    {
        if (!env->is_stopping())
            uv_run(env->event_loop(), UV_RUN_NOWAIT);
        if (!env->is_stopping())
            platform->DrainTasks(isolate);
    }
}

void SpinEventLoopSync(Environment* env, bool until_idle)
{
    CHECK_NOT_NULL(env);
    MultiIsolatePlatform* platform = GetMultiIsolatePlatform(env);
    CHECK_NOT_NULL(platform);

    Isolate* isolate = env->isolate();
    HandleScope handle_scope(isolate);
    Context::Scope context_scope(env->context());
    SealHandleScope seal(isolate);

    env->set_trace_sync_io(env->options()->trace_sync_io);
    {
        bool more;
        do {
            if (env->is_stopping())
                break;
            uv_run(env->event_loop(), UV_RUN_DEFAULT);
            if (env->is_stopping())
                break;

            platform->DrainTasks(isolate);
            if (until_idle) {
                more = uv_loop_alive(env->event_loop());
            }
        } while (until_idle && more && !env->is_stopping());
    }
}

class V8Scope {
public:
    V8Scope(NodeEnv* node)
        : locker_(node->isolate)
        , isolate_scope_(node->isolate)
        , handle_scope_(node->isolate)
        , context_scope_(node->setup->context())
    {
    }

private:
    v8::Locker locker_;
    v8::Isolate::Scope isolate_scope_;
    v8::HandleScope handle_scope_;
    v8::Context::Scope context_scope_;
};

Local<Value> GetValueByKey(Local<Context> context, Isolate* isolate,
    Local<Object> obj,
    const string& key)
{
    Local<String> v8Key = String::NewFromUtf8(isolate, key.c_str()).ToLocalChecked();
    return obj->Get(context, v8Key).ToLocalChecked();
}

string random_string(size_t length)
{
    static const char chars[] = "0123456789ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz";
    static thread_local mt19937 rng { random_device {}() };
    static thread_local uniform_int_distribution<> dist(0, sizeof(chars) - 2);

    string s;
    s.reserve(length);
    for (size_t i = 0; i < length; ++i)
        s.push_back(chars[dist(rng)]);
    return s;
}

static unique_ptr<MultiIsolatePlatform> platform = nullptr;
static vector<string> args;
static vector<string> exec_args;

void NodeInit(int thread_pool_size)
{
    int argc = 1;
    const char* raw_argv[] = { "node" };
    char** argv = nullptr;
    FixupMain(argc, const_cast<char**>(raw_argv), &argv);

    std::vector<std::string> args(argv, argv + argc);
    std::shared_ptr<node::InitializationResult> result = node::InitializeOncePerProcess(
        args,
        {
            node::ProcessInitializationFlags::kNoInitializeV8,
            node::ProcessInitializationFlags::kNoInitializeNodeV8Platform,
            // This is used to test NODE_REPL_EXTERNAL_MODULE is disabled with
            // kDisableNodeOptionsEnv. If other tests need NODE_OPTIONS
            // support in the future, split this configuration out as a
            // command line option.
            node::ProcessInitializationFlags::kDisableNodeOptionsEnv,
            node::ProcessInitializationFlags::kNoInitializeCppgc,
        });

    platform = MultiIsolatePlatform::Create(thread_pool_size);
    V8::InitializePlatform(platform.get());
    cppgc::InitializeProcess(platform->GetPageAllocator());
    V8::Initialize();
    args = result->args();
    exec_args = result->exec_args();
}

void NodeFree()
{
    V8::Dispose();
    V8::DisposePlatform();
    node::TearDownOncePerProcess();
}

NodeEnv* NodeEnvCreate(const char* absolute_path)
{
    vector<string> errors;
    auto setup = CommonEnvironmentSetup::Create(platform.get(), &errors, args, exec_args);
    NodeEnv* node = new NodeEnv();
    node->setup = std::move(setup);
    Isolate* isolate = node->setup->isolate();
    Environment* env = node->setup->env();
    node->isolate = isolate;
    node->env = env;
    node->loop = node->setup->event_loop();

    {
        v8::Locker locker(isolate);
        v8::Isolate::Scope isolate_scope(isolate);
        v8::HandleScope handle_scope(isolate);
        Local<v8::Context> context = node->setup->context();
        v8::Context::Scope context_scope(node->setup->context());
        string import_name = "import_" + random_string(6);
        MaybeLocal<Value> ret = _nodejs::LoadEnvironment(env,
            "function " + import_name + "(s) { return import(s); }"
                                        "const publicRequire = require('module').createRequire(\""
                + absolute_path + "\");"
                                  "globalThis.require = publicRequire;"
                                  "return {'import': "
                + import_name + ", 'require': publicRequire, 'runInThisContext': require('vm').runInThisContext};");
        if (ret.IsEmpty()) {
            // ERROR
            return nullptr;
        }
        Local<Object> dict = Local<Object>::Cast(ret.ToLocalChecked());
        Local<Function> import_func = Local<Function>::Cast(GetValueByKey(context, isolate, dict, "import"));
        Local<Function> require_func = Local<Function>::Cast(GetValueByKey(context, isolate, dict, "require"));
        Local<Function> runInThisContext_func = Local<Function>::Cast(GetValueByKey(context, isolate, dict, "runInThisContext"));
        node->import.Reset(isolate, import_func);
        node->require.Reset(isolate, require_func);
        node->runInThisContext.Reset(isolate, runInThisContext_func);
    }

    return node;
}

PyObject* JSToPy(NodeEnv* node, Local<Value> value);
Local<Value> PyToJS(NodeEnv* node, PyObject* value);

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

static PyObject* js_promise_handler(PyObject* self, PyObject* future)
{
    JSPromiseData* data = (JSPromiseData*)PyCapsule_GetPointer(self, "promise_data");
    if (!data)
        return NULL;

    PyObject* result = PyObject_CallMethod(future, "result", NULL);
    if (!result) {
        // if coroutine raised, .result() re-raises, so handle exception
        PyErr_Print();
        Py_RETURN_NONE;
    }

    {
        NodeEnv* node = data->node;
        V8Scope scope(node);
        Local<Promise::Resolver> resolver = data->js_resolver.Get(node->isolate);
        resolver->Resolve(node->isolate->GetCurrentContext(), PyToJS(data->node, result)).Check();
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
    {
        NodeEnv* node = data->node;
        V8Scope scope(node);
        Local<Function> func = data->js_func.Get(node->isolate);
        vector<Local<Value>> argv = {};
        for (int i = 0; i < nargs; i++) {
            argv.push_back(PyToJS(node, PyTuple_GetItem(args, i)));
        }
        Local<Value> recv = node->isolate->GetCurrentContext()->Global(); // TODO Fix recv for objects
        return JSToPy(node, func->Call(node->isolate->GetCurrentContext(), recv, nargs, argv.data()).ToLocalChecked()); // TODO catch errors
    }
}

void py_func_handler(const FunctionCallbackInfo<Value>& args)
{
    Isolate* isolate = args.GetIsolate();

    Local<External> data = Local<External>::Cast(args.Data());
    PyFunctionData* func_data = (PyFunctionData*)data->Value();

    PyObject* func = func_data->py_func;

    vector<PyObject> argv = {};

    PyObject* res = PyObject_CallObject(func, argv.data());
    args.GetReturnValue().Set(PyToJS(func_data->node, res));
}

static void cleanup_py_promise(PyObject* capsule)
{
    JSPromiseData* data = (JSPromiseData*)PyCapsule_GetPointer(capsule, "promise_data");
    if (data) {
        data->js_resolver.Reset();
        if (data->method_name)
            PyMem_Free(data->method_name);
        if (data->method_def)
            PyMem_Free(data->method_def);
        PyMem_Free(data);
    }
}

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

Local<Value> PyToJS(NodeEnv* node, PyObject* value)
{
    Local<Context> context = node->isolate->GetCurrentContext();
    if (value == Py_None) // None
    {
        using v8::Null;
        return Null(node->isolate);
    } else if (PyBool_Check(value)) {
        using v8::Boolean;
        return Boolean::New(node->isolate, value == Py_True ? true : false);
    } else if (PyLong_Check(value)) // Int
    {
        int overflow = 0;
        long long num = PyLong_AsLongLongAndOverflow(value, &overflow);

        if (overflow != 0) {
            using v8::Null;
            Py_ssize_t nbytes = _PyLong_NumBits(value);
            if (nbytes < 0) {
                return Null(node->isolate); // Maybe error
            }
            size_t nwords = (nbytes + 63) / 64;
            uint64_t* words = (uint64_t*)malloc(nwords);
            if (_PyLong_AsByteArray((PyLongObject*)value, (unsigned char*)words, nwords * 8, 1, 0) < 0) {
                return Null(node->isolate); // Maybe error
            }
            int sign = _PyLong_Sign(value) == -1 ? 1 : 0;
            using v8::BigInt;
            return BigInt::NewFromWords(context, sign, nwords, words).ToLocalChecked();
        } else if (num < -2147483648LL && num > 2147483647LL) {
            using v8::BigInt;
            return BigInt::New(node->isolate, num);
        }

        using v8::Int32;
        return Int32::New(node->isolate, num);
    } else if (PyFloat_Check(value)) // Number
    {
        using v8::Number;
        return Number::New(node->isolate, PyFloat_AsDouble(value));
    } else if (PyUnicode_Check(value)) // String
    {
        using v8::NewStringType;
        return String::NewFromUtf8(
            node->isolate,
            PyUnicode_AsUTF8(value),
            NewStringType::kNormal)
            .ToLocalChecked();
    } else if (PyFunction_Check(value)) // Function
    {
        Local<FunctionTemplate> tpl = FunctionTemplate::New(
            node->isolate, py_func_handler, External::New(node->isolate, new PyFunctionData { node, value }));
        Local<Function> fn = tpl->GetFunction(context).ToLocalChecked();
        return fn;
    } else if (PyExceptionClass_Check(value)) { // Exception
        PyObject* exc = PyObject_CallObject(value, NULL);
        if (!exc) {
            // handle error
            using v8::Null;
            return Null(node->isolate);
        }

        PyObject* args
            = PyObject_GetAttrString(exc, "args");
        if (args && PyTuple_Check(args)) {
            Py_ssize_t n = PyTuple_Size(args);
            if (n > 0) {
                PyObject* first_arg = PyTuple_GetItem(args, 0); // borrowed reference
                if (PyUnicode_Check(first_arg)) {
                    const char* msg = PyUnicode_AsUTF8(first_arg);
                    Local<String> message = String::NewFromUtf8(node->isolate, msg).ToLocalChecked();
                    return v8::Exception::Error(message);
                }
            }
        }
        Py_XDECREF(args);
        Py_XDECREF(exc);

        using v8::Null;
        return Null(node->isolate);

    } else if (PyDateTime_Check(value)) { // Date
        int year = PyDateTime_GET_YEAR(value);
        int month = PyDateTime_GET_MONTH(value);
        int day = PyDateTime_GET_DAY(value);
        int hour = PyDateTime_DATE_GET_HOUR(value);
        int min = PyDateTime_DATE_GET_MINUTE(value);
        int sec = PyDateTime_DATE_GET_SECOND(value);
        // ms is missing

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

        return v8::Date::New(context, unix_ms).ToLocalChecked();
    } else if (PyList_Check(value)) // Array
    {
        int len = PyList_Size(value);
        Local<Array> arr = Array::New(node->isolate, len);
        for (int i = 0; i < len; i++) {
            arr->Set(context, i, PyToJS(node, PyList_GetItem(value, len))).Check();
        }
        return arr;
    } else if (PyTuple_Check(value)) // Array
    {
        int len = PyTuple_Size(value);
        Local<Array> arr = Array::New(node->isolate, len);
        for (int i = 0; i < len; i++) {
            arr->Set(context, i, PyToJS(node, PyTuple_GetItem(value, len))).Check();
        }
        return arr;
    } else if (PyDict_Check(value)) // Object
    {
        using v8::Name;
        int len = PyDict_Size(value);
        vector<Local<Name>> keys(len);
        vector<Local<Value>> values(len);
        Py_ssize_t pos;
        PyObject *py_key, *py_val;
        int i = 0;
        while (PyDict_Next(value, &pos, &py_key, &py_val)) {
            keys[i] = String::NewFromUtf8(node->isolate, PyUnicode_AsUTF8(py_key)).ToLocalChecked();
            values[i++] = PyToJS(node, py_val);
        }
        return Object::New(node->isolate, Null(node->isolate), keys.data(), values.data(), i);
    } else if (is_coroutine_like(value)) // Coroutine
    {
        PyObject* asyncio = PyImport_ImportModule("asyncio");
        using v8::Null;
        if (!asyncio)
            return Null(node->isolate);

        v8::Local<v8::Context> context = node->isolate->GetCurrentContext();
        v8::MaybeLocal<v8::Promise::Resolver> maybe_resolver = v8::Promise::Resolver::New(context);
        if (maybe_resolver.IsEmpty()) {
            Py_DECREF(asyncio);
            return v8::Null(node->isolate);
        }

        v8::Local<v8::Promise::Resolver> resolver = maybe_resolver.ToLocalChecked();
        v8::Local<v8::Promise> promise = resolver->GetPromise();

        JSPromiseData* data = (JSPromiseData*)PyMem_Malloc(sizeof(JSPromiseData));
        if (!data)
            return Null(node->isolate);
        data->node = node;
        new (&data->js_resolver) Global<Promise::Resolver>();
        data->js_resolver.Reset(node->isolate, resolver);

        PyObject* capsule = PyCapsule_New(data, "promise_data", cleanup_py_promise);
        if (!capsule) {
            data->js_resolver.Reset();
            PyMem_Free(data);
            Py_DECREF(asyncio);
            return v8::Null(node->isolate);
        }

        PyMethodDef* def = (PyMethodDef*)PyMem_Malloc(sizeof(PyMethodDef));
        if (!def) {
            Py_DECREF(capsule); // This will trigger destructor
            Py_DECREF(asyncio);
            return v8::Null(node->isolate);
        }

        std::string name_str = "callback_" + random_string(12);
        char* name_copy = (char*)PyMem_Malloc(name_str.length() + 1);
        if (!name_copy) {
            PyMem_Free(def);
            Py_DECREF(capsule);
            Py_DECREF(asyncio);
            return v8::Null(node->isolate);
        }
        strcpy(name_copy, name_str.c_str());

        def->ml_name = name_copy;
        def->ml_meth = js_promise_handler;
        def->ml_flags = METH_VARARGS;
        def->ml_doc = "";
        data->method_def = def;
        data->method_name = name_copy;

        PyObject* callback = PyCFunction_NewEx(def, capsule, NULL);

        PyMem_Free(capsule);

        if (!callback) {
            PyMem_Free(name_copy);
            PyMem_Free(def);
            Py_DECREF(asyncio);
            return v8::Null(node->isolate);
        }

        PyObject* get_event_loop = PyObject_GetAttrString(asyncio, "get_event_loop");
        if (!get_event_loop) {
            PyErr_Print();
            Py_DECREF(callback);
            PyMem_Free(name_copy);
            PyMem_Free(def);
            Py_DECREF(asyncio);
            return v8::Null(node->isolate);
        }

        PyObject* loop = PyObject_CallObject(get_event_loop, NULL);
        Py_DECREF(get_event_loop);

        if (!loop) {
            PyErr_Print();
            Py_DECREF(callback);
            PyMem_Free(name_copy);
            PyMem_Free(def);
            Py_DECREF(asyncio);
            return v8::Null(node->isolate);
        }

        PyObject* ensure_future = PyObject_GetAttrString(asyncio, "ensure_future");
        if (!ensure_future) {
            PyErr_Print();
            Py_DECREF(loop);
            Py_DECREF(callback);
            PyMem_Free(name_copy);
            PyMem_Free(def);
            Py_DECREF(asyncio);
            return v8::Null(node->isolate);
        }

        PyObject* task = PyObject_CallFunctionObjArgs(ensure_future, value, NULL);
        Py_DECREF(ensure_future);

        if (!task) {
            PyErr_Print();
            Py_DECREF(loop);
            Py_DECREF(callback);
            PyMem_Free(name_copy);
            PyMem_Free(def);
            Py_DECREF(asyncio);
            return v8::Null(node->isolate);
        }

        PyObject* result = PyObject_CallMethod(task, "add_done_callback", "O", callback);
        if (!result) {
            PyErr_Print();
        }
        Py_XDECREF(result);

        Py_DECREF(task);
        Py_DECREF(loop);
        Py_DECREF(callback);
        Py_DECREF(asyncio);

        return promise;
    } else {
        PyObject* dict = PyObject_GetAttrString(value, "__dict__");
        if (!dict || !PyDict_Check(dict)) {
            Py_XDECREF(dict);
            using v8::Null;
            return Null(node->isolate);
        }
        PyObject *py_key, *py_val;
        Py_ssize_t pos = 0;
        using v8::Name;
        int len = PyDict_Size(value);
        vector<Local<Name>> keys(len);
        vector<Local<Value>> values(len);

        int i = 0;
        while (PyDict_Next(dict, &pos, &py_key, &py_val)) {
            keys[i] = String::NewFromUtf8(node->isolate, PyUnicode_AsUTF8(py_key)).ToLocalChecked();
            values[i++] = PyToJS(node, py_val);
        }

        Py_DECREF(dict);

        return Object::New(node->isolate, Null(node->isolate), keys.data(), values.data(), i);
    }
}
static void cleanup_js_func(PyObject* capsule)
{
    JSFunctionData* data = (JSFunctionData*)PyCapsule_GetPointer(capsule, "func_data");
    if (data) {
        data->js_func.Reset();
        if (data->method_def) {
            PyMem_Free((void*)data->method_def->ml_name);
            PyMem_Free((void*)data->method_def->ml_doc);
            PyMem_Free(data->method_def);
        }
        PyMem_Free(data);
    }
}

PyObject* JSToPy(NodeEnv* node, Local<Value> value)
{
    Local<Context> context = node->isolate->GetCurrentContext();
    if (value.IsEmpty() || value->IsNullOrUndefined()) { // None
        Py_RETURN_NONE;
    } else if (value->IsBoolean()) { // Boolean
        using v8::Boolean;
        if (value.As<Boolean>()->BooleanValue(node->isolate)) {
            Py_RETURN_TRUE;
        } else {
            Py_RETURN_FALSE;
        }
    } else if (value->IsBigInt()) { // BigInt
        Local<String> str = value->ToString(context).ToLocalChecked();
        String::Utf8Value utf8(node->isolate, str);
        return PyLong_FromString(*utf8, NULL, 10);
    } else if (value->IsInt32()) { // Integer
        return PyLong_FromLong(value->IntegerValue(context).FromJust());
    } else if (value->IsNumber()) { // Double
        return PyFloat_FromDouble(value->NumberValue(context).FromJust());
    } else if (value->IsNumberObject()) { // Double
        return PyFloat_FromDouble(value.As<NumberObject>()->ValueOf());
    } else if (value->IsString()) { // String
        Local<String> str = value->ToString(context).ToLocalChecked();
        String::Utf8Value utf8(node->isolate, str);
        return PyUnicode_FromString(*utf8);
    } else if (value->IsStringObject()) { // String
        Local<StringObject> obj = value.As<StringObject>();
        String::Utf8Value utf8(node->isolate, obj->ValueOf());
        return PyUnicode_FromString(*utf8);
    } else if (value->IsPromise()) { // Promise
        Py_RETURN_NOTIMPLEMENTED;
    } else if (value->IsNativeError()) { // Exception
        Local<v8::Object> err = value.As<v8::Object>();
        v8::Local<v8::String> msg_key = v8::String::NewFromUtf8(node->isolate, "message").ToLocalChecked();
        v8::Local<v8::Value> message;
        if (err->Get(context, msg_key).ToLocal(&message)) {
            v8::String::Utf8Value msg_str(node->isolate, message);
            PyErr_SetString(PyExc_RuntimeError, *msg_str);
            return NULL;
        }
        PyErr_SetString(PyExc_RuntimeError, "JS Error");
        return NULL;
    } else if (value->IsDate()) { // Date
        Local<v8::Date> pd = value.As<v8::Date>();
        double ms = pd->ValueOf();
        time_t seconds = ms / 1000;
        int microseconds = (int)(fmod(ms, 1000)) * 1000;

        struct tm timeinfo;
        if (gmtime_r(&seconds, &timeinfo) == NULL) {
            PyErr_SetString(PyExc_ValueError, "Invalid timestamp");
            return NULL;
        }

        return PyDateTime_FromDateAndTime(
            timeinfo.tm_year + 1900,
            timeinfo.tm_mon + 1,
            timeinfo.tm_mday,
            timeinfo.tm_hour,
            timeinfo.tm_min,
            timeinfo.tm_sec,
            microseconds);
    } else if (value->IsFunction()) {
        Local<Function> js_func = value.As<Function>();

        JSFunctionData* data = (JSFunctionData*)PyMem_Malloc(sizeof(JSFunctionData));
        if (!data)
            return NULL;
        data->node = node;
        new (&data->js_func) Global<Function>();
        data->js_func.Reset(node->isolate, js_func);

        Local<String> name_str = js_func->GetName()->ToString(context).ToLocalChecked();
        String::Utf8Value name_utf8(node->isolate, name_str);

        Local<String> source_str = js_func->ToString(context).ToLocalChecked();
        String::Utf8Value source_utf8(node->isolate, source_str);

        char* name_copy = (char*)PyMem_Malloc(name_utf8.length() + 1);
        strcpy(name_copy, *name_utf8);

        std::string doc_str = "From NodeJS:\n" + std::string(*source_utf8);
        char* doc_copy = (char*)PyMem_Malloc(doc_str.length() + 1);
        strcpy(doc_copy, doc_str.c_str());

        PyMethodDef* def = (PyMethodDef*)PyMem_Malloc(sizeof(PyMethodDef));
        def->ml_name = name_copy;
        def->ml_meth = js_func_handler;
        def->ml_flags = METH_VARARGS;
        def->ml_doc = doc_copy;

        data->method_def = def;

        PyObject* capsule = PyCapsule_New(data, "func_data", cleanup_js_func);
        PyObject* func = PyCFunction_NewEx(def, capsule, NULL);

        return func;

    } else if (value->IsArray() || value->IsSet()) { // Array
        Local<Array> arr;
        if (value->IsSet()) {
            arr = value.As<Set>()->AsArray();
        } else {
            arr = value.As<Array>();
        }
        PyObject* list = PyList_New(arr->Length());
        for (int i = 0; i < arr->Length(); i++) {
            PyList_SET_ITEM(list, i, JSToPy(node, arr->Get(context, i).ToLocalChecked()));
        }
        return list;
    } else { // Any Object
        Local<Object> obj = value.As<Object>();
        Local<Array> keys = obj->GetOwnPropertyNames(context).ToLocalChecked();
        size_t length = keys->Length();

        PyObject* dict = _PyDict_NewPresized(length);
        for (int i = 0; i < length; i++) {
            Local<Value> key = keys->Get(node->isolate->GetCurrentContext(), i).ToLocalChecked();
            Local<String> str = key->ToString(context).ToLocalChecked();
            String::Utf8Value utf8(node->isolate, str);

            Local<Value> val = obj->Get(node->isolate->GetCurrentContext(), key).ToLocalChecked();

            PyDict_SetItemString(dict, *utf8, JSToPy(node, val));
        }
        return dict;
    }
}

void PollSync(NodeEnv* node, bool blocking)
{
    {
        V8Scope scope(node);
        SpinEventLoopSync(node->env, blocking);
        for (auto it = node->promises.begin(); it != node->promises.end();) {
            Local<Promise> promise = it->second.Get(node->isolate);
            if (promise->State() == Promise::kFulfilled) {
                promise->Result();
                it = node->promises.erase(it);
            } else if (promise->State() == Promise::kRejected) {
                it = node->promises.erase(it);
            }
        }
    }
}

void PollAsync(NodeEnv* node)
{
    SpinEventLoopAsync(node->env);
}

void NodeEnvFree(NodeEnv* node)
{
    if (!node) {
        fprintf(stderr, "Failed to free Node environment: node is null\n");
        return;
    }
    if (!node->isolate) {
        fprintf(stderr, "Failed to free Node environment: isolate is null\n");
        return;
    }
    if (node->isolate->IsInUse()) {
        fprintf(stderr, "Failed to free Node environment: isolate is in use\n");
        return;
    }
    {
        V8Scope scope(node);

        node::SpinEventLoop(node->env);
        node::Stop(node->env);
    }

    {
        v8::Locker locker(node->isolate);
        v8::Isolate::Scope isolate_scope(node->isolate);
        v8::HandleScope handle_scope(node->isolate);

        node->import.Reset();
        node->require.Reset();
        node->runInThisContext.Reset();
        node->promises.clear();
    }
}

typedef struct {
    PyObject_HEAD;
    NodeEnv* node;
} NodeJSObject;

static bool node_initialized = false;
static int node_instances = 0;

// 2. __init__ method
int NodeJS_init(NodeJSObject* self, PyObject* args, PyObject* kwds)
{
    const char* path = nullptr;
    int thread_pool_size = 4;

    static const char* kwlist[] = { "path", "thread_pool_size", nullptr };

    if (!PyArg_ParseTupleAndKeywords(args, kwds, "s|i", const_cast<char**>(kwlist), &path, &thread_pool_size))
        return -1;

    if (!node_initialized) {
        NodeInit(thread_pool_size);
    }
    NodeEnv* node = NodeEnvCreate(path);
    self->node = node;
    node_instances++;
    node_initialized = true;
    return 0;
}

// 3. __repr__ method
PyObject* NodeJS_repr(NodeJSObject* self)
{
    return PyUnicode_FromFormat("NodeJS");
}

static void NodeJS_dealloc(NodeJSObject* self)
{
    node_instances--;
    NodeEnvFree(self->node);
    delete self->node;
    if (node_instances == 0) {
        node_initialized = false;
        NodeFree();
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
        V8Scope scope(self->node);
        Local<Context> context = self->node->isolate->GetCurrentContext();
        Local<Function> eval = self->node->runInThisContext.Get(self->node->isolate);
        vector<Local<Value>> argv = { String::NewFromUtf8(self->node->isolate, source).ToLocalChecked() };
        v8::TryCatch try_catch(self->node->isolate);

        Local<Value> res;
        if (!eval->CallAsFunction(context, context->Global(), 1, argv.data()).ToLocal(&res)) {
            v8::String::Utf8Value error(self->node->isolate, try_catch.Exception());
            PyErr_SetString(PyExc_RuntimeError, *error);
            return NULL;
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
        V8Scope scope(self->node);
        Local<Context> context = self->node->isolate->GetCurrentContext();
        Local<Function> require = self->node->require.Get(self->node->isolate);
        vector<Local<Value>> argv = { String::NewFromUtf8(self->node->isolate, url).ToLocalChecked() };
        v8::TryCatch try_catch(self->node->isolate);

        Local<Value> res;
        if (!require->CallAsFunction(context, context->Global(), 1, argv.data()).ToLocal(&res)) {
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
        V8Scope scope(self->node);
        Local<Context> context = self->node->isolate->GetCurrentContext();
        Local<Function> import = self->node->import.Get(self->node->isolate);
        vector<Local<Value>> argv = { String::NewFromUtf8(self->node->isolate, url).ToLocalChecked() };
        v8::TryCatch try_catch(self->node->isolate);

        Local<Value> res;
        if (!import->CallAsFunction(context, context->Global(), 1, argv.data()).ToLocal(&res)) {
            v8::String::Utf8Value error(self->node->isolate, try_catch.Exception());
            PyErr_SetString(PyExc_RuntimeError, *error);
            return NULL;
        }
        return JSToPy(self->node, res);
    }
}

static PyMethodDef NodeJS_methods[] = {
    { "eval_cjs", (PyCFunction)NodeJS_eval, METH_O,
        "Evaluates a piece of CJS code." },
    { "require_cjs", (PyCFunction)NodeJS_require, METH_O,
        "Imports a CJS file." },
    { "import_esm", (PyCFunction)NodeJS_import, METH_O,
        "Imports an ES module." },
    { NULL, NULL, 0, NULL } // Sentinel
};

// 5. Type object
static PyTypeObject NodeJSType = {
    PyVarObject_HEAD_INIT(NULL, 0) "pythonodejs.NodeJS", // tp_name
    sizeof(NodeJSObject), // tp_basicsize
    0, // tp_itemsize
    (destructor)NodeJS_dealloc, // tp_dealloc
    0, // tp_vectorcall_offset / tp_print (depending on Python version)
    0, // tp_getattr
    0, // tp_setattr
    0, // tp_as_async
    (reprfunc)NodeJS_repr, // tp_repr
    0, // tp_as_number
    0, // tp_as_sequence
    0, // tp_as_mapping
    0, // tp_hash
    0, // tp_call
    0, // tp_str
    0, // tp_getattro
    0, // tp_setattro
    0, // tp_as_buffer
    Py_TPFLAGS_DEFAULT, // tp_flags
    0, // tp_doc
    0, // tp_traverse
    0, // tp_clear
    0, // tp_richcompare
    0, // tp_weaklistoffset
    0, // tp_iter
    0, // tp_iternext
    NodeJS_methods, // tp_methods
    0, // tp_members
    0, // tp_getset
    0, // tp_base
    0, // tp_dict
    0, // tp_descr_get
    0, // tp_descr_set
    0, // tp_dictoffset
    (initproc)NodeJS_init, // tp_init
    0, // tp_alloc
    PyType_GenericNew, // tp_new
};

static PyMethodDef LibMethods[] = {
    { NULL, NULL, 0, NULL } // Sentiel
};

// Module definition
static struct PyModuleDef node_mod = {
    PyModuleDef_HEAD_INIT,
    "pythonodejs", // Module name
    "Pythonodejs NodeJS Interop", // Module doc
    -1,
    LibMethods
};

// Module initialization function
PyMODINIT_FUNC PyInit__pythonodejs(void)
{
    PyObject* m;

    if (PyType_Ready(&NodeJSType) < 0)
        return NULL;

    m = PyModule_Create(&node_mod);
    if (m == NULL)
        return NULL;

    PyDateTime_IMPORT;

    Py_INCREF(&NodeJSType);
    if (PyModule_AddObject(m, "NodeJS", (PyObject*)&NodeJSType) < 0) {
        Py_DECREF(&NodeJSType);
        Py_DECREF(m);
        return NULL;
    }

    return m;
}