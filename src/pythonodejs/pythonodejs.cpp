#include "common.h"

#include <Python.h>
#include <chrono>
#include <cmath>
#include <datetime.h>
#include <random>
#include <string>
#include <thread>
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
#include "pyport.h"
#include "pystate.h"
#include "unicodeobject.h"
#include "v8-external.h"
#include "v8-local-handle.h"
#include "v8-object.h"
#include "v8-persistent-handle.h"
#include "v8-primitive.h"
#include "v8-promise.h"
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

#define V8_SCOPE(node)                                          \
    v8::Locker locker_##__LINE__(node->isolate);                \
    v8::Isolate::Scope isolate_scope_##__LINE__(node->isolate); \
    v8::HandleScope handle_scope_##__LINE__(node->isolate);     \
    v8::Context::Scope context_scope_##__LINE__(node->setup->context());

Local<Value> GetValueByKey(Local<Context> context, Isolate* isolate,
    Local<Object> obj,
    const string& key)
{
    Local<String> v8Key = String::NewFromUtf8(isolate, key.c_str()).ToLocalChecked();
    return obj->Get(context, v8Key).ToLocalChecked();
}

string encode_base62(uint64_t value)
{
    static const char chars[] = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789";
    string result;
    do {
        result = chars[value % 62] + result;
        value /= 62;
    } while (value > 0);
    return result;
}

string random_string()
{
    static std::atomic<uint64_t> counter { 0 };

    uint64_t timestamp = std::chrono::steady_clock::now().time_since_epoch().count();
    uint64_t thread_id = std::hash<std::thread::id> {}(std::this_thread::get_id());
    uint64_t c = counter.fetch_add(1, std::memory_order_relaxed);

    std::string ts_str = encode_base62(timestamp);
    std::string tid_str = encode_base62(thread_id);
    std::string counter_str = encode_base62(c);

    char first_char = 'A';
    if (!isalpha(ts_str[0]) && ts_str[0] != '_') {
        first_char = 'A';
    } else {
        first_char = ts_str[0];
    }

    return first_char + ts_str.substr(1) + "_" + tid_str + "_" + counter_str;
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
    auto setup = CommonEnvironmentSetup::Create(platform.get(), &errors, args, exec_args,
        static_cast<node::EnvironmentFlags::Flags>(node::EnvironmentFlags::kNoCreateInspector));
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
        string import_name = "import_" + random_string();
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
        Local<v8::Map> visited_map = v8::Map::New(isolate);
        node->import.Reset(isolate, import_func);
        node->require.Reset(isolate, require_func);
        node->runInThisContext.Reset(isolate, runInThisContext_func);
        node->visited.Reset(isolate, visited_map);
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

        Local<String> js_error_msg = String::NewFromUtf8(node->isolate, error_msg).ToLocalChecked();
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
            Local<Function> catch_handler = Function::New(
                node->setup->context(),
                [](const FunctionCallbackInfo<Value>& args) {
                },
                Local<Value>())
                                                .ToLocalChecked();
            (void)promise->Catch(node->setup->context(), catch_handler);
        }

        Py_RETURN_NONE;
    }

    Py_XDECREF(exception);

    PyObject* result = PyObject_CallMethod(future, "result", NULL);
    if (!result) {
        // This shouldn't happend
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
        Local<Value> js_value = PyToJS(node, result);

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

        vector<Local<Value>> argv = {};
        argv.resize(nargs);

        Local<Function> func;

        Py_BEGIN_ALLOW_THREADS;

        func = data->js_func.Get(node->isolate);

        Py_END_ALLOW_THREADS;

        for (int i = 0; i < nargs; i++) {
            argv[i] = PyToJS(node, PyTuple_GetItem(args, i));
        }

        PyThreadState* _save = PyEval_SaveThread();

        Local<Value> recv = node->setup->context()->Global(); // TODO Fix recv for objects
        v8::TryCatch try_catch(node->isolate);
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

void py_func_handler(const FunctionCallbackInfo<Value>& args)
{
    Isolate* isolate = args.GetIsolate();

    Local<External> data = Local<External>::Cast(args.Data());
    PyFunctionData* func_data = (PyFunctionData*)data->Value();

    PyObject* func = func_data->py_func;

    PyGILState_STATE gstate = PyGILState_Ensure();

    int nargs = args.Length();
    PyObject* py_args = PyTuple_New(nargs);
    if (!py_args) {
        PyGILState_Release(gstate);
        isolate->ThrowException(
            v8::Exception::Error(
                String::NewFromUtf8(isolate, "Failed to create Python tuple").ToLocalChecked()));
        return;
    }

    for (int i = 0; i < nargs; i++) {
        PyObject* py_arg = JSToPy(func_data->node, args[i]);
        if (!py_arg) {
            PyGILState_Release(gstate);
            Py_DECREF(py_args);
            isolate->ThrowException(
                v8::Exception::Error(
                    String::NewFromUtf8(isolate, "Failed to convert JS argument to Python").ToLocalChecked()));
            return;
        }
        PyTuple_SET_ITEM(py_args, i, py_arg); // Steals reference
    }

    PyObject* res = PyObject_CallObject(func, py_args);
    Py_DECREF(py_args);

    if (!res) {
        PyErr_Print();
        PyGILState_Release(gstate);
        isolate->ThrowException(
            v8::Exception::Error(
                String::NewFromUtf8(isolate, "Python function raised an exception").ToLocalChecked()));
        return;
    }

    Local<Value> js_result = PyToJS(func_data->node, res);
    Py_DECREF(res);

    PyGILState_Release(gstate);

    args.GetReturnValue().Set(js_result);
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
    Local<Context> context = node->setup->context();
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
            uint64_t* words = (uint64_t*)malloc(nwords * sizeof(uint64_t));
            if (_PyLong_AsByteArray((PyLongObject*)PyNumber_Absolute(value), (unsigned char*)words, nwords * 8, 1, 0) < 0) {
                return Null(node->isolate); // Maybe error
            }
            int sign = _PyLong_Sign(value) == -1 ? 1 : 0;
            using v8::BigInt;
            return BigInt::NewFromWords(context, sign, nwords, words).ToLocalChecked();
        } else if (num < -2147483648LL || num > 2147483647LL) {
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
            arr->Set(context, i, PyToJS(node, PyList_GetItem(value, i))).Check();
        }
        return arr;
    } else if (PyTuple_Check(value)) // Array
    {
        int len = PyTuple_Size(value);
        Local<Array> arr = Array::New(node->isolate, len);
        for (int i = 0; i < len; i++) {
            arr->Set(context, i, PyToJS(node, PyTuple_GetItem(value, i))).Check();
        }
        return arr;
    } else if (PyDict_Check(value)) // Object TODO use PyMapping
    {
        using v8::Name;
        Py_ssize_t len = PyObject_Size(value);
        if (len < 0) {
            PyErr_Clear();
            return v8::Null(node->isolate);
        }
        Local<Object> obj = Object::New(node->isolate);
        Py_ssize_t pos = 0;
        PyObject *py_key, *py_val;
        printf("pythonodejs: PyToJS:  len=%d\n", (int)len);
        printf("    ");
        while (PyDict_Next(value, &pos, &py_key, &py_val)) {
            obj->Set(context, String::NewFromUtf8(node->isolate, PyUnicode_AsUTF8(py_key)).ToLocalChecked(), PyToJS(node, py_val)).Check();
            printf("setting %s, ", PyUnicode_AsUTF8(py_key));
        }
        int obj_len = obj->GetOwnPropertyNames(context).ToLocalChecked()->Length();
        printf("\npythonodejs: PyToJS:  obj_len=%d\n", obj_len);
        return obj;
    } else if (is_coroutine_like(value)) // Coroutine
    {
        PyObject* asyncio = PyImport_ImportModule("asyncio");
        using v8::Null;
        if (!asyncio)
            return Null(node->isolate);

        v8::Local<v8::Context> context = node->setup->context();
        v8::MaybeLocal<v8::Promise::Resolver> maybe_resolver = v8::Promise::Resolver::New(context);
        if (maybe_resolver.IsEmpty()) {
            Py_DECREF(asyncio);
            return v8::Null(node->isolate);
        }

        v8::Local<v8::Promise::Resolver> resolver = maybe_resolver.ToLocalChecked();
        v8::Local<v8::Promise> promise = resolver->GetPromise();

        Local<Function> catch_handler = Function::New(
            context,
            [](const FunctionCallbackInfo<Value>& args) {
                // This catch handler prevents unhandled rejection warnings
                // The actual error is still available through the promise chain
            },
            Local<Value>())
                                            .ToLocalChecked();

        (void)promise->Catch(context, catch_handler);

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

        std::string name_str = "callback_" + random_string();
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
    } else { // TODO Use PyMapping
        PyObject* dict = PyObject_GetAttrString(value, "__dict__");
        if (!dict || !PyDict_Check(dict)) {
            Py_XDECREF(dict);
            using v8::Null;
            return Null(node->isolate);
        }
        using v8::Name;
        Py_ssize_t len = PyObject_Size(dict);
        if (len < 0) {
            PyErr_Clear();
            return v8::Null(node->isolate);
        }
        Local<Object> obj = Object::New(node->isolate);
        Py_ssize_t pos;
        PyObject *py_key, *py_val;
        while (PyDict_Next(dict, &pos, &py_key, &py_val)) {
            obj->Set(context, String::NewFromUtf8(node->isolate, PyUnicode_AsUTF8(py_key)).ToLocalChecked(), PyToJS(node, py_val)).Check();
        }

        Py_DECREF(dict);

        return obj;
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
    Local<Context> context = node->setup->context();
    Local<v8::Map> visited = node->visited.Get(node->isolate);
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
        v8::Local<v8::Promise> promise = value.As<v8::Promise>();
        // TODO add to visited
        // Add a catch handler to prevent unhandled rejection warnings
        Local<Function> catch_handler = Function::New(
            context,
            [](const FunctionCallbackInfo<Value>& args) {
                // This catch handler prevents unhandled rejection warnings
            },
            Local<Value>())
                                            .ToLocalChecked();
        (void)promise->Catch(context, catch_handler);
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
        if (
#ifdef _WIN32
            gmtime_s(&timeinfo, &seconds) != 0
#else
            gmtime_r(&seconds, &timeinfo) == NULL
#endif
        ) {
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

        visited->Set(context, js_func, v8::External::New(node->isolate, func)).ToLocalChecked(); // TODO Catch errors

        return func;
    } else if (value->IsArray() || value->IsSet()) { // Array
        Local<Array> arr;
        if (value->IsSet()) {
            Local<v8::Set> set = value.As<Set>();
            arr = set->AsArray();
        } else {
            arr = value.As<Array>();
        }
        PyObject* list = PyList_New(arr->Length());
        visited->Set(context, arr, v8::External::New(node->isolate, list)).ToLocalChecked(); // TODO Catch errors
        for (int i = 0; i < arr->Length(); i++) {
            Local<Value> item = arr->Get(context, i).ToLocalChecked();
            if (!visited->Has(context, item).ToChecked())
                PyList_SET_ITEM(list, i, JSToPy(node, item));
        }
        visited->Delete(context, arr).ToChecked(); // Catch errors
        return list;
    } else { // Any Object
        Local<Object> obj = value.As<Object>();
        Local<Array> keys = obj->GetOwnPropertyNames(context).ToLocalChecked();
        size_t length = keys->Length();
        PyObject* dict = _PyDict_NewPresized(length);
        printf("pythonodejs: JSToPy:  len=%d\n", (int)length);
        printf("pythonodejs: JSToPy:  len=%d\n", (int)length);
        printf("                      ");
        visited->Set(context, obj, v8::External::New(node->isolate, dict)).ToLocalChecked(); // TODO Catch errors
        for (int i = 0; i < length; i++) {
            Local<Value> key = keys->Get(node->setup->context(), i).ToLocalChecked();
            Local<String> str = key->ToString(context).ToLocalChecked();
            String::Utf8Value utf8(node->isolate, str);

            printf("Setting %s, ", *utf8);

            Local<Value> val = obj->Get(node->setup->context(), key).ToLocalChecked();

            if (!visited->Has(context, val).ToChecked())
                PyDict_SetItemString(dict, *utf8, JSToPy(node, val));
        }
        printf("\npythonodejs: JSToPy:  py_len=%d\n", (int)PyDict_Size(dict));
        visited->Delete(context, obj).ToChecked(); // Catch errors
        return dict;
    }
}

void PollSync(NodeEnv* node, bool blocking)
{
    {
        V8_SCOPE(node);
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
        V8_SCOPE(node);

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
        node->visited.Reset();
        node->promises.clear();
    }
}

typedef struct {
    PyObject_HEAD;
    NodeEnv* node;
} NodeJSObject;

static bool node_initialized = false;

// 2. __init__ method
int NodeJS_init(NodeJSObject* self, PyObject* args, PyObject* kwds)
{
    if (self->node) {
        NodeEnvFree(self->node);
        delete self->node;
    }
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
    NodeEnvFree(self->node);
    delete self->node;
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
        Local<Function> eval = self->node->runInThisContext.Get(self->node->isolate);
        vector<Local<Value>> argv = { String::NewFromUtf8(self->node->isolate, source).ToLocalChecked() };
        v8::TryCatch try_catch(self->node->isolate);

        Local<Value> res;
        if (!eval->CallAsFunction(context, context->Global(), 1, argv.data()).ToLocal(&res)) {
            v8::String::Utf8Value error(self->node->isolate, try_catch.Exception());
            PyErr_SetString(PyExc_RuntimeError, *error);
            return NULL;
        }
        // If result is a promise, attach a catch handler to prevent unhandled rejection warnings
        if (res->IsPromise()) {
            Local<Promise> promise = res.As<Promise>();
            Local<Function> catch_handler = Function::New(
                context,
                [](const FunctionCallbackInfo<Value>& args) {
                    // Catch handler to prevent unhandled rejection warnings
                },
                Local<Value>())
                                                .ToLocalChecked();
            (void)promise->Catch(context, catch_handler);
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
        V8_SCOPE(self->node);
        Local<Context> context = self->node->setup->context();
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

static void pythonodejs_free(void* m)
{
    if (node_initialized)
        NodeFree();
}

// Module definition
static struct PyModuleDef node_mod = {
    PyModuleDef_HEAD_INIT,
    "_pythonodejs", // Module name
    "Pythonodejs NodeJS Interop", // Module doc
    -1,
    LibMethods, // module methods
    NULL, // m_slots
    NULL, // m_traverse
    NULL, // m_clear
    pythonodejs_free // m_free
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