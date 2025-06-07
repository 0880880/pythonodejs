#include "pythonodejs.h"

#include <cstdint>
#include <cstring>
#include <iostream>
#include <random>
#include <string>
#include <vector>

#include "cppgc/platform.h"
#include "env.h"
#include "node.h"
#include "node_internals.h"
#include "uv.h"

using node::CommonEnvironmentSetup;
using node::Environment;
using node::MultiIsolatePlatform;
using v8::Context;
using v8::HandleScope;
using v8::Isolate;
using v8::Locker;
using v8::MaybeLocal;
using v8::V8;
using v8::Value;

struct NodeContext {
    std::unique_ptr<MultiIsolatePlatform> platform;
    std::vector<std::string> args;
    std::vector<std::string> exec_args;
    std::unique_ptr<CommonEnvironmentSetup> setup;
    node::Environment *env;
    Isolate *isolate;
    v8::Global<Context> global_ctx;
    v8::Global<v8::Function> runInThisContext;
    Callback py_callback;
    FutureCallback future_callback;
    uv_loop_t *loop;
    std::unordered_map<int64_t, v8::Global<v8::Promise::Resolver>>
        resolvers_from_python;
    std::unordered_map<int64_t, v8::Global<v8::Promise::Resolver>>
        resolvers_to_python;
};

struct FuncInfo {
    const char *name;
    NodeContext *context;
};

struct FutureInfo {
    NodeContext *context;
    int64_t id;
    bool rejected;
};

struct Func {
    v8::Global<v8::Function> function;
};

struct Val {
    v8::Global<v8::Value> value;
};

int64_t randomInt64() {
    static std::random_device rd;
    static std::mt19937_64 gen(rd());
    static std::uniform_int_distribution<int64_t> dist(INT64_MIN, INT64_MAX);
    return dist(gen);
}

void debugValue(v8::Local<v8::Value> value, v8::Isolate *isolate,
                v8::Local<Context> context) {

    v8::String::Utf8Value typeStr(
        isolate, value->TypeOf(isolate)->ToString(context).ToLocalChecked());
    std::cout << "Type: " << *typeStr << "\n";

    if (value->IsBoolean() || value->IsNumber() || value->IsString() ||
        value->IsNull() || value->IsUndefined()) {
        v8::String::Utf8Value valStr(isolate,
                                     value->ToString(context).ToLocalChecked());
        std::cout << "Value: " << *valStr << "\n";
    }

    if (value->IsObject()) {
        auto obj = value.As<v8::Object>();
        v8::Local<v8::Array> props;
        std::cout << "OBJECT" << "GETTING PROPS" << std::endl;
        if (obj->GetOwnPropertyNames(context).ToLocal(&props)) {
            uint32_t len = props->Length();
            std::cout << "Object has " << len << " own properties:\n";
            for (uint32_t i = 0; i < len; ++i) {
                v8::Local<v8::Value> key, val;
                if (!props->Get(context, i).ToLocal(&key))
                    continue;
                if (!obj->Get(context, key).ToLocal(&val))
                    continue;

                v8::String::Utf8Value keyStr(isolate, key);
                v8::String::Utf8Value valStr(
                    isolate, val->ToString(context).ToLocalChecked());
                std::cout << "  [" << *keyStr << "] = " << *valStr << "\n";
            }
        }
    }
}

void run_loop_blocking(NodeContext *context) {
    while (uv_loop_alive(context->loop)) {
        uv_run(context->loop, UV_RUN_DEFAULT);
    }
    /* uv_walk(
        context->loop,
        [](uv_handle_t *handle, void *) {
            if (!uv_is_closing(handle))
                uv_close(handle, nullptr);
        },
        nullptr); */
    node::EmitBeforeExit(context->env);
    node::EmitExit(context->env);
}

NodeContext *NodeContext_Create() { return new NodeContext(); }

void NodeContext_Destroy(NodeContext *context) { delete context; }

int NodeContext_Setup(NodeContext *context, int argc, char **argv) {
    argv = uv_setup_args(argc, argv);
    std::vector<std::string> args(argv, argv + argc);

    std::shared_ptr<node::InitializationResult> result =
        node::InitializeOncePerProcess(
            args,
            {
                node::ProcessInitializationFlags::kNoInitializeV8,
                node::ProcessInitializationFlags::kNoInitializeNodeV8Platform,
                node::ProcessInitializationFlags::kDisableNodeOptionsEnv,
                node::ProcessInitializationFlags::kNoInitializeCppgc,
            });

    for (const std::string &error : result->errors())
        fprintf(stderr, "%s: %s\n", args[0].c_str(), error.c_str());

    if (result->early_return() != 0)
        return result->exit_code();

    std::unique_ptr<MultiIsolatePlatform> platform =
        MultiIsolatePlatform::Create(4);
    V8::InitializePlatform(platform.get());
    cppgc::InitializeProcess(platform->GetPageAllocator());
    V8::Initialize();

    context->platform = std::move(platform);

    context->args = {result->args()[0]};
    context->exec_args = result->exec_args();

    return result->exit_code();
}

void NodeContext_SetCallback(NodeContext *context, Callback cb) {
    if (!context) {
        std::cerr
            << "PYTHONODEJS: NodeContext_SetCallback called with NULL context!"
            << std::endl;
        return;
    }
    context->py_callback = cb;
}

void NodeContext_SetFutureCallback(NodeContext *context, FutureCallback cb) {
    if (!context) {
        std::cerr
            << "PYTHONODEJS: NodeContext_SetFutureCallback called with NULL "
               "context!"
            << std::endl;
        return;
    }
    context->future_callback = cb;
}

NodeValue to_node_value(NodeContext *context, v8::Local<Context> local_ctx,
                        v8::Local<Value> value);

void promise_callback(const v8::FunctionCallbackInfo<v8::Value> &args) {

    v8::Local<v8::External> data = v8::Local<v8::External>::Cast(args.Data());
    FutureInfo *info = static_cast<FutureInfo *>(data->Value());

    NodeContext *context = info->context;

    v8::Isolate *isolate = args.GetIsolate();
    Locker locker(isolate);
    Isolate::Scope isolate_scope(isolate);
    v8::HandleScope handle_scope(isolate);
    v8::Local<Context> local_ctx = context->global_ctx.Get(isolate);
    NodeValue result = to_node_value(context, local_ctx, args[0]);
    if (context->future_callback) {
        context->future_callback(info->id, result, info->rejected);
    }
}

NodeValue to_node_value(NodeContext *context, v8::Local<Context> local_ctx,
                        v8::Local<Value> value) {
    v8::Global<v8::Value> *self_heap =
        new v8::Global<v8::Value>(context->isolate, value);
    if (value->IsUndefined()) {
        self_heap->Reset();
        return {.type = UNDEFINED};
    } else if (value->IsNull()) {
        self_heap->Reset();
        return {.type = NULL_T};
    } else if (value->IsNumber()) {
        self_heap->Reset();
        return {.type = NUMBER, .val_num = value.As<v8::Number>()->Value()};
    } else if (value->IsBoolean()) {
        self_heap->Reset();
        return {.type = BOOLEAN_T,
                .val_bool = value.As<v8::Boolean>()->Value()};
    } else if (value->IsString()) {
        self_heap->Reset();
        v8::String::Utf8Value utf8(context->isolate, value.As<v8::String>());
        return {.type = STRING, .val_string = strdup(*utf8)};
    } else if (value->IsSymbol()) {
        v8::Local<v8::Symbol> symbol = value.As<v8::Symbol>();
        v8::String::Utf8Value utf8(context->isolate,
                                   symbol->Description(context->isolate));
        return {.type = SYMBOL,
                .val_string = strdup(*utf8),
                .val_external_ptr = static_cast<void *>(self_heap)};
    } else if (value->IsBigInt()) {
        self_heap->Reset();
        v8::String::Utf8Value utf8(
            context->isolate,
            value.As<v8::BigInt>()->ToString(local_ctx).ToLocalChecked());
        return {.type = BIGINT, .val_big = strdup(*utf8)};
    } else if (value->IsFunction()) {
        NodeValue ret = {};
        v8::Local<v8::Function> func = value.As<v8::Function>();
        v8::String::Utf8Value utf8(context->isolate, func->GetName());
        Func *f = new Func();
        f->function.Reset(context->isolate, func);
        ret.type = FUNCTION;
        ret.self_ptr = self_heap;
        ret.val_string = strdup(*utf8);
        ret.function = f;
        return ret;
    } else if (value->IsArray()) {
        v8::Local<v8::Array> array = value.As<v8::Array>();
        int length = array->Length();
        NodeValue *arr = (NodeValue *)malloc(length * sizeof(NodeValue));
        NodeValue nv = {.type = ARRAY,
                        .self_ptr = self_heap,
                        .val_array = arr,
                        .val_array_len = length};
        for (int i = 0; i < length; i++) {
            arr[i] = to_node_value(context, local_ctx,
                                   array->Get(local_ctx, i).ToLocalChecked());
            Val *parent = new Val();
            parent->value.Reset(context->isolate, value);
            arr[i].parent = parent;
        }
        return nv;
    } else if (value->IsDate()) {
        self_heap->Reset();
        v8::Local<v8::Date> date = value.As<v8::Date>();
        return {.type = DATE_T, .val_date_unix = date->ValueOf()};
    } else if (value->IsNativeError()) {
        self_heap->Reset();
        v8::Local<v8::Object> error_obj = value.As<v8::Object>();

        char *message;
        char *name;
        char *stack;

        v8::Local<v8::Value> message_val;
        if (error_obj
                ->Get(local_ctx,
                      v8::String::NewFromUtf8(context->isolate, "message")
                          .ToLocalChecked())
                .ToLocal(&message_val)) {
            v8::String::Utf8Value utf8(context->isolate, message_val);
            message = *utf8;
        }

        v8::Local<v8::Value> name_val;
        if (error_obj
                ->Get(local_ctx,
                      v8::String::NewFromUtf8(context->isolate, "name")
                          .ToLocalChecked())
                .ToLocal(&name_val)) {
            v8::String::Utf8Value utf8(context->isolate, name_val);
            name = *utf8;
        }

        v8::Local<v8::Value> stack_val;
        if (error_obj
                ->Get(local_ctx,
                      v8::String::NewFromUtf8(context->isolate, "stack")
                          .ToLocalChecked())
                .ToLocal(&stack_val)) {
            v8::String::Utf8Value utf8(context->isolate, stack_val);
            stack = *utf8;
        }

        return {.type = ERROR_T,
                .error_message = strdup(message),
                .error_name = strdup(name),
                .error_stack = strdup(stack)};
    } else if (value->IsRegExp()) {
        self_heap->Reset();
        v8::Local<v8::RegExp> regex = value.As<v8::RegExp>();
        v8::Local<v8::String> pattern = regex->GetSource();
        v8::RegExp::Flags flags = regex->GetFlags();
        v8::String::Utf8Value utf8(context->isolate, pattern);
        return {.type = REGEXP,
                .val_string = strdup(*utf8),
                .val_regex_flags = static_cast<int>(flags)};
    } else if (value->IsPromise()) {
        v8::Local<v8::Promise> promise = value.As<v8::Promise>();
        int64_t id = randomInt64();

        Locker locker(context->isolate);
        Isolate::Scope isolate_scope(context->isolate);
        HandleScope handle_scope(context->isolate);
        v8::Local<Context> local_ctx =
            context->global_ctx.Get(context->isolate);

        FutureInfo *thenInfo = new FutureInfo;
        thenInfo->id = id;
        thenInfo->context = context;
        thenInfo->rejected = false;
        v8::Local<v8::External> then_external =
            v8::External::New(context->isolate, thenInfo);

        v8::Local<v8::FunctionTemplate> then_tpl = v8::FunctionTemplate::New(
            context->isolate, promise_callback, then_external);
        v8::Local<v8::Function> then_fn =
            then_tpl->GetFunction(local_ctx).ToLocalChecked();
        promise->Then(local_ctx, then_fn).ToLocalChecked();

        FutureInfo *catchInfo = new FutureInfo;
        catchInfo->id = id;
        catchInfo->context = context;
        catchInfo->rejected = true;
        v8::Local<v8::External> catch_external =
            v8::External::New(context->isolate, catchInfo);

        v8::Local<v8::FunctionTemplate> catch_tpl = v8::FunctionTemplate::New(
            context->isolate, promise_callback, catch_external);
        v8::Local<v8::Function> catch_fn =
            catch_tpl->GetFunction(local_ctx).ToLocalChecked();
        promise->Catch(local_ctx, catch_fn).ToLocalChecked();
        return {.type = PROMISE, .self_ptr = self_heap, .future_id = id};
    } else if (value->IsMap()) {
        v8::Local<v8::Map> map = value.As<v8::Map>();
        v8::Local<v8::Array> array =
            map->AsArray(); // [key1, val1, key2, val2, ...]

        int len = array->Length() / 2;

        NodeValue *keys = (NodeValue *)malloc(len * sizeof(NodeValue));
        NodeValue *values = (NodeValue *)malloc(len * sizeof(NodeValue));
        NodeValue nv = {.type = MAP,
                        .self_ptr = self_heap,
                        .map_keys = keys,
                        .object_values = values,
                        .object_len = len};
        for (uint32_t i = 0; i < len; i++) {
            v8::Local<v8::Value> key =
                array->Get(local_ctx, i * 2).ToLocalChecked();
            v8::Local<v8::Value> val =
                array->Get(local_ctx, i * 2 + 1).ToLocalChecked();
            keys[i / 2] = to_node_value(context, local_ctx, key);
            values[i / 2] = to_node_value(context, local_ctx, value);
        }
        return nv;
    } else if (value->IsSet()) {
        v8::Local<v8::Set> set = value.As<v8::Set>();
        v8::Local<v8::Array> entries = set->AsArray();

        int len = entries->Length();

        NodeValue *arr = (NodeValue *)malloc(len * sizeof(NodeValue));
        for (uint32_t i = 0; i < len; ++i) {
            v8::Local<v8::Value> val =
                entries->Get(local_ctx, i).ToLocalChecked();
            arr[i] = to_node_value(context, local_ctx, val);
        }
        return {.type = SET,
                .self_ptr = self_heap,
                .val_array = arr,
                .val_array_len = len};
    } else if (value->IsArrayBuffer()) {
        self_heap->Reset();
        v8::Local<v8::ArrayBuffer> buffer = value.As<v8::ArrayBuffer>();
        std::shared_ptr<v8::BackingStore> backing = buffer->GetBackingStore();

        void *src = backing->Data();
        size_t size = backing->ByteLength();

        void *dest = malloc(size);
        memcpy(dest, src, size);
        return {.type = ARRAY_BUFFER,
                .val_tarray = dest,
                .val_array_len = static_cast<int>(size)};
    } else if (value->IsDataView()) {
        self_heap->Reset();
        auto arr = value.As<v8::DataView>();
        v8::Local<v8::ArrayBuffer> buffer = arr->Buffer();
        uint8_t *data =
            static_cast<uint8_t *>(buffer->GetBackingStore()->Data()) +
            arr->ByteOffset();
        size_t length = arr->ByteLength();
        return {.type = ARRAY_BUFFER,
                .val_tarray = data,
                .val_array_len = static_cast<int>(length)};
    } else if (value->IsSharedArrayBuffer()) {
        self_heap->Reset();
        // TODO
    } else if (value->IsTypedArray()) {
        self_heap->Reset();
        if (value->IsInt8Array()) {
            auto arr = value.As<v8::Int8Array>();
            v8::Local<v8::ArrayBuffer> buffer = arr->Buffer();
            int8_t *data =
                static_cast<int8_t *>(buffer->GetBackingStore()->Data()) +
                arr->ByteOffset();
            size_t length = arr->ByteLength();
            return {.type = TYPED_ARRAY,
                    .val_tarray = data,
                    .val_tarray_type = INT8_T,
                    .val_array_len = static_cast<int>(length)};
        } else if (value->IsUint8Array()) {
            auto arr = value.As<v8::Int8Array>();
            v8::Local<v8::ArrayBuffer> buffer = arr->Buffer();
            uint8_t *data =
                static_cast<uint8_t *>(buffer->GetBackingStore()->Data()) +
                arr->ByteOffset();
            size_t length = arr->ByteLength();
            return {.type = TYPED_ARRAY,
                    .self_ptr = self_heap,
                    .val_tarray = data,
                    .val_tarray_type = UINT8_T,
                    .val_array_len = static_cast<int>(length)};
        } else if (value->IsUint8ClampedArray()) {
            auto arr = value.As<v8::Uint8ClampedArray>();
            v8::Local<v8::ArrayBuffer> buffer = arr->Buffer();
            int8_t *data =
                static_cast<int8_t *>(buffer->GetBackingStore()->Data()) +
                arr->ByteOffset();
            size_t length = arr->ByteLength();
            return {.type = TYPED_ARRAY,
                    .self_ptr = self_heap,
                    .val_tarray = data,
                    .val_tarray_type = UINT8_T,
                    .val_array_len = static_cast<int>(length)};
        } else if (value->IsInt16Array()) {
            auto arr = value.As<v8::Int16Array>();
            v8::Local<v8::ArrayBuffer> buffer = arr->Buffer();
            int16_t *data =
                static_cast<int16_t *>(buffer->GetBackingStore()->Data()) +
                arr->ByteOffset();
            size_t length = arr->ByteLength();
            return {.type = TYPED_ARRAY,
                    .self_ptr = self_heap,
                    .val_tarray = data,
                    .val_tarray_type = INT16_T,
                    .val_array_len = static_cast<int>(length)};
        } else if (value->IsUint16Array()) {
            auto arr = value.As<v8::Uint16Array>();
            v8::Local<v8::ArrayBuffer> buffer = arr->Buffer();
            uint16_t *data =
                static_cast<uint16_t *>(buffer->GetBackingStore()->Data()) +
                arr->ByteOffset();
            size_t length = arr->ByteLength();
            return {.type = TYPED_ARRAY,
                    .self_ptr = self_heap,
                    .val_tarray = data,
                    .val_tarray_type = UINT16_T,
                    .val_array_len = static_cast<int>(length)};
        } else if (value->IsInt32Array()) {
            auto arr = value.As<v8::Int32Array>();
            v8::Local<v8::ArrayBuffer> buffer = arr->Buffer();
            int32_t *data =
                static_cast<int32_t *>(buffer->GetBackingStore()->Data()) +
                arr->ByteOffset();
            size_t length = arr->ByteLength();
            return {.type = TYPED_ARRAY,
                    .self_ptr = self_heap,
                    .val_tarray = data,
                    .val_tarray_type = INT32_T,
                    .val_array_len = static_cast<int>(length)};
        } else if (value->IsUint32Array()) {
            auto arr = value.As<v8::Uint32Array>();
            v8::Local<v8::ArrayBuffer> buffer = arr->Buffer();
            uint32_t *data =
                static_cast<uint32_t *>(buffer->GetBackingStore()->Data()) +
                arr->ByteOffset();
            size_t length = arr->ByteLength();
            return {.type = TYPED_ARRAY,
                    .self_ptr = self_heap,
                    .val_tarray = data,
                    .val_tarray_type = UINT32_T,
                    .val_array_len = static_cast<int>(length)};
        } else if (value->IsFloat32Array()) {
            auto arr = value.As<v8::Float32Array>();
            v8::Local<v8::ArrayBuffer> buffer = arr->Buffer();
            float *data =
                static_cast<float *>(buffer->GetBackingStore()->Data()) +
                arr->ByteOffset();
            size_t length = arr->ByteLength();
            return {.type = TYPED_ARRAY,
                    .self_ptr = self_heap,
                    .val_tarray = data,
                    .val_tarray_type = FLOAT32_T,
                    .val_array_len = static_cast<int>(length)};
        } else if (value->IsFloat64Array()) {
            auto arr = value.As<v8::Float64Array>();
            v8::Local<v8::ArrayBuffer> buffer = arr->Buffer();
            double *data =
                static_cast<double *>(buffer->GetBackingStore()->Data()) +
                arr->ByteOffset();
            size_t length = arr->ByteLength();
            return {.type = TYPED_ARRAY,
                    .self_ptr = self_heap,
                    .val_tarray = data,
                    .val_tarray_type = FLOAT64_T,
                    .val_array_len = static_cast<int>(length)};
        } else if (value->IsBigInt64Array()) {
            auto arr = value.As<v8::BigInt64Array>();
            v8::Local<v8::ArrayBuffer> buffer = arr->Buffer();
            long *data =
                static_cast<long *>(buffer->GetBackingStore()->Data()) +
                arr->ByteOffset();
            size_t length = arr->ByteLength();
            return {.type = TYPED_ARRAY,
                    .self_ptr = self_heap,
                    .val_tarray = data,
                    .val_tarray_type = BINT64_T,
                    .val_array_len = static_cast<int>(length)};
        } else if (value->IsBigUint64Array()) {
            auto arr = value.As<v8::BigUint64Array>();
            v8::Local<v8::ArrayBuffer> buffer = arr->Buffer();
            uint64_t *data =
                static_cast<uint64_t *>(buffer->GetBackingStore()->Data()) +
                arr->ByteOffset();
            size_t length = arr->ByteLength();
            return {.type = TYPED_ARRAY,
                    .self_ptr = self_heap,
                    .val_tarray = data,
                    .val_tarray_type = BUINT64_T,
                    .val_array_len = static_cast<int>(length)};
        }
    } else if (value->IsDataView()) {
        std::vector<int> v(10);
        for (int i = 0; i <= v.size();
             ++i) { // Off-by-one error (clang-tidy detects)
            v[i] = i;
        }
    } else if (value->IsExternal()) { // at the end to not override other
                                      // objects.
        v8::Local<v8::Object> obj = value.As<v8::Object>();
        v8::Local<v8::Array> keys =
            obj->GetOwnPropertyNames(local_ctx).ToLocalChecked();
        int length = keys->Length();
        char **key_arr = (char **)malloc(length * sizeof(char *));
        NodeValue *objects = (NodeValue *)malloc(length * sizeof(NodeValue));
        NodeValue nv = {.type = EXTERNAL,
                        .object_keys = key_arr,
                        .object_values = objects,
                        .object_len = length,
                        .val_external_ptr = static_cast<void *>(self_heap)};
        for (uint32_t i = 0; i < length; ++i) {
            v8::Local<v8::Value> key = keys->Get(local_ctx, i).ToLocalChecked();
            v8::String::Utf8Value utf8(context->isolate, key);
            size_t len = strlen(*utf8);
            key_arr[i] = (char *)malloc(len + 1); // +1 for null terminator
            strcpy(key_arr[i], strdup(*utf8));
            v8::Local<v8::Value> oval;
            if (obj->Get(local_ctx, key).ToLocal(&oval)) {
                objects[i] = to_node_value(context, local_ctx, oval);
                Val *parent = new Val();
                parent->value.Reset(context->isolate, value);
                objects[i].parent = parent;
            }
        }
        return nv;
    } else if (value->IsProxy()) {
        self_heap->Reset();
        v8::Local<v8::Proxy> proxy = value.As<v8::Proxy>();
        v8::Local<v8::Value> target = proxy->GetTarget();
        v8::Local<v8::Value> handler = proxy->GetHandler();
        NodeValue *node_target = (NodeValue *)malloc(sizeof(NodeValue));
        node_target[0] = to_node_value(context, local_ctx, target);
        NodeValue *node_handler = (NodeValue *)malloc(sizeof(NodeValue));
        node_handler[0] = to_node_value(context, local_ctx, handler);
        return {.type = PROXY,
                .proxy_target = node_target,
                .proxy_handler = node_handler};
    } else if (value->IsObject()) { // at the end to not override other objects.
        v8::Local<v8::Object> obj = value.As<v8::Object>();
        v8::Local<v8::Array> keys =
            obj->GetOwnPropertyNames(local_ctx).ToLocalChecked();
        int length = keys->Length();
        char **key_arr = (char **)malloc(length * sizeof(char *));
        NodeValue *objects = (NodeValue *)malloc(length * sizeof(NodeValue));
        NodeValue nv = {.type = OBJECT,
                        .self_ptr = self_heap,
                        .object_keys = key_arr,
                        .object_values = objects,
                        .object_len = length};
        for (uint32_t i = 0; i < length; ++i) {
            v8::Local<v8::Value> key = keys->Get(local_ctx, i).ToLocalChecked();
            v8::String::Utf8Value utf8(context->isolate, key);
            size_t len = strlen(*utf8);
            key_arr[i] = (char *)malloc(len + 1); // +1 for null terminator
            strcpy(key_arr[i], strdup(*utf8));
            v8::Local<v8::Value> oval;
            if (obj->Get(local_ctx, key).ToLocal(&oval)) {
                objects[i] = to_node_value(context, local_ctx, oval);
                Val *parent = new Val();
                parent->value.Reset(context->isolate, value);
                objects[i].parent = parent;
            }
        }
        return nv;
    } else {
        v8::String::Utf8Value typeStr(context->isolate,
                                      value->TypeOf(context->isolate)
                                          ->ToString(local_ctx)
                                          .ToLocalChecked());
        std::cout << "PYTHONODEJS: Unsupported type \"" << *typeStr
                  << "\" ignored.\n";
    }
    self_heap->Reset();
    return {};
}

v8::Local<v8::Value> to_v8_value(NodeContext *context,
                                 v8::Local<Context> local_ctx,
                                 NodeValue value) {
    if (value.type == UNDEFINED) {
        return v8::Undefined(context->isolate);
    } else if (value.type == NULL_T) {
        return v8::Null(context->isolate);
    } else if (value.type == NUMBER) {
        return v8::Number::New(context->isolate, value.val_num);
    } else if (value.type == BOOLEAN_T) {
        return v8::Boolean::New(context->isolate, value.val_bool);
    } else if (value.type == STRING) {
        return v8::String::NewFromUtf8(context->isolate, value.val_string)
            .ToLocalChecked();
    } else if (value.type == SYMBOL) {
        return static_cast<v8::Global<v8::Value> *>(value.val_external_ptr)
            ->Get(context->isolate);
    } else if (value.type == BIGINT) {
        int sign_bit = 0;
        std::string s = value.val_big;

        if (s.empty()) {
            std::cerr << "PYTHONODEJS: Empty bigint value." << std::endl;
            return {};
        }

        if (s[0] == '-') {
            sign_bit = 1;
        }
        s = s.substr(1);

        std::vector<uint8_t> digits;
        for (char c : s) {
            if (c < '0' || c > '9') {
                std::cerr << "PYTHONODEJS: Invalid digit in bigint."
                          << std::endl;
                return {};
            }
            digits.push_back(c - '0');
        }

        std::vector<uint64_t> words;
        const __uint128_t BASE = (__uint128_t)1 << 64;

        while (!digits.empty()) {
            __uint128_t value = 0;
            std::vector<uint8_t> next;

            for (size_t i = 0; i < digits.size(); ++i) {
                value = value * 10 + digits[i];
                if (!next.empty() || value >= BASE) {
                    next.push_back(static_cast<uint8_t>(value / BASE));
                    value %= BASE;
                } else if (!next.empty()) {
                    next.push_back(0);
                }
            }

            words.push_back(static_cast<uint64_t>(value));
            digits = next;
        }

        return v8::BigInt::NewFromWords(local_ctx, sign_bit,
                                        static_cast<int>(words.size()),
                                        words.data())
            .ToLocalChecked();
    } else if (value.type == FUNCTION) {
        return (*value.function).function.Get(context->isolate);
    } else if (value.type == ARRAY) {
        v8::Local<v8::Array> array =
            v8::Array::New(context->isolate, value.val_array_len);
        for (int i = 0; i < value.val_array_len; i++) {
            v8::Local<v8::Value> elem =
                to_v8_value(context, local_ctx, value.val_array[i]);
            if (elem.IsEmpty()) {
                std::cerr << "PYTHONODEJS: to_v8_value returned empty for "
                             "array index "
                          << i << std::endl;
                // Decide: skip or insert `undefined` explicitly
                continue;
            }
            // Now we know `elem` is real:
            array->Set(local_ctx, i, elem).Check();
        }

        return array;
    } else if (value.type == ARRAY_BUFFER) {
        auto backing_store = v8::ArrayBuffer::NewBackingStore(
            value.val_tarray, value.val_array_len,
            [](void *data, size_t length, void *deleter_data) {}, nullptr);

        return v8::ArrayBuffer::New(context->isolate, std::move(backing_store));
    } else if (value.type == TYPED_ARRAY) {
        size_t element_size = 0;
        switch (value.val_tarray_type) {
        case INT8_T:
            element_size = 1;
            break;
        case UINT8_T:
            element_size = 1;
            break;
        case INT16_T:
            element_size = 2;
            break;
        case UINT16_T:
            element_size = 2;
            break;
        case INT32_T:
            element_size = 4;
            break;
        case UINT32_T:
            element_size = 4;
            break;
        case BINT64_T:
            element_size = 8;
            break;
        case BUINT64_T:
            element_size = 8;
            break;
        case FLOAT32_T:
            element_size = 4;
            break;
        case FLOAT64_T:
            element_size = 8;
            break;
        default:
            return v8::Local<v8::TypedArray>();
        }

        size_t length_elements = value.val_array_len / element_size;

        auto backing_store = v8::ArrayBuffer::NewBackingStore(
            value.val_tarray, value.val_array_len,
            [](void *data, size_t length, void *deleter_data) {
                // Optional deleter or no-op
            },
            nullptr);

        v8::Local<v8::ArrayBuffer> array_buffer =
            v8::ArrayBuffer::New(context->isolate, std::move(backing_store));

        v8::Local<v8::TypedArray> typed_array;
        switch (value.val_tarray_type) {
        case INT8_T:
            typed_array = v8::Int8Array::New(array_buffer, 0, length_elements);
            break;
        case UINT8_T:
            typed_array = v8::Uint8Array::New(array_buffer, 0, length_elements);
            break;
        case INT16_T:
            typed_array = v8::Int16Array::New(array_buffer, 0, length_elements);
            break;
        case UINT16_T:
            typed_array =
                v8::Uint16Array::New(array_buffer, 0, length_elements);
            break;
        case INT32_T:
            typed_array = v8::Int32Array::New(array_buffer, 0, length_elements);
            break;
        case UINT32_T:
            typed_array =
                v8::Uint32Array::New(array_buffer, 0, length_elements);
            break;
        case BINT64_T:
            typed_array =
                v8::BigInt64Array::New(array_buffer, 0, length_elements);
            break;
        case BUINT64_T:
            typed_array =
                v8::BigUint64Array::New(array_buffer, 0, length_elements);
            break;
        case FLOAT32_T:
            typed_array =
                v8::Float32Array::New(array_buffer, 0, length_elements);
            break;
        case FLOAT64_T:
            typed_array =
                v8::Float64Array::New(array_buffer, 0, length_elements);
            break;
        }
        return typed_array;
    } else if (value.type == OBJECT) {
        v8::Local<v8::Object> object = v8::Object::New(context->isolate);
        for (int i = 0; i < value.object_len; i++) {
            v8::Local<v8::String> key =
                v8::String::NewFromUtf8(context->isolate, value.object_keys[i])
                    .ToLocalChecked();
            v8::Local<v8::Value> val =
                to_v8_value(context, local_ctx, value.object_values[i]);

            if (val.IsEmpty()) {
                std::cerr << "PYTHONODEJS: to_v8_value returned empty handle "
                             "at index "
                          << i << std::endl;
                continue;
            }

            v8::Maybe<bool> maybe_result = object->Set(local_ctx, key, val);
            if (maybe_result.IsNothing()) {
                std::cerr << "PYTHONODEJS: Failed to set key "
                          << value.object_keys[i] << std::endl;
            }
        }
        return object;
    } else if (value.type == DATE_T) {
        return v8::Date::New(local_ctx, value.val_date_unix).ToLocalChecked();
    } else if (value.type == REGEXP) {
        v8::Local<v8::String> pattern_str =
            v8::String::NewFromUtf8(context->isolate, value.val_string)
                .ToLocalChecked();
        return v8::RegExp::New(
                   local_ctx, pattern_str,
                   static_cast<v8::RegExp::Flags>(value.val_regex_flags))
            .ToLocalChecked();
    } else if (value.type == MAP) {
        v8::Local<v8::Map> map = v8::Map::New(context->isolate);
        for (uint32_t i = 0; i < value.object_len; ++i) {
            map = map->Set(local_ctx,
                           to_v8_value(context, local_ctx, value.map_keys[i]),
                           to_v8_value(context, local_ctx,
                                       value.object_values[i]))
                      .ToLocalChecked();
        }
        return map;
    } else if (value.type == SET) {
        v8::Local<v8::Set> set = v8::Set::New(context->isolate);
        for (uint32_t i = 0; i < value.object_len; ++i) {
            set = set->Add(local_ctx,
                           to_v8_value(context, local_ctx, value.val_array[i]))
                      .ToLocalChecked();
        }
        return set;
    } else if (value.type == PROXY) {
        return v8::Proxy::New(
                   local_ctx,
                   to_v8_value(context, local_ctx, *value.proxy_target)
                       .As<v8::Object>(),
                   to_v8_value(context, local_ctx, *value.proxy_handler)
                       .As<v8::Object>())
            .ToLocalChecked();
    } else if (value.type == EXTERNAL) {
        return v8::External::New(context->isolate, value.val_external_ptr);
    } else if (value.type == PROMISE) {
        v8::Local<v8::Promise::Resolver> resolver =
            v8::Promise::Resolver::New(local_ctx).ToLocalChecked();
        v8::Global<v8::Promise::Resolver> global(context->isolate, resolver);
        context->resolvers_from_python[value.future_id] = std::move(global);
        return resolver->GetPromise();
    }
    return {};
}

void NodeContext_FutureUpdate(NodeContext *context, int64_t id,
                              NodeValue result, bool rejected) {
    if (context->resolvers_from_python.contains(id)) {
        Locker locker(context->isolate);
        Isolate::Scope isolate_scope(context->isolate);
        HandleScope handle_scope(context->isolate);
        v8::Local<Context> local_ctx =
            context->global_ctx.Get(context->isolate);
        v8::Local<v8::Promise::Resolver> resolver =
            context->resolvers_from_python[id].Get(context->isolate);
        if (rejected) {
            resolver->Reject(local_ctx, to_v8_value(context, local_ctx, result))
                .ToChecked();
        } else {
            resolver
                ->Resolve(local_ctx, to_v8_value(context, local_ctx, result))
                .ToChecked();
        }
        context->resolvers_from_python.erase(id);
        run_loop_blocking(context);
        return;
    }
    std::cerr << "PYTHONODEJS: Invalid future " << id << std::endl;
}

int NodeContext_Init(NodeContext *context, char **imports, int num_imports,
                     int thread_pool_size) {

    std::vector<std::string> errors;

    std::string binary_path = context->args[0];
    std::vector<std::string> filtered_args;

    {
        context->setup =
            CommonEnvironmentSetup::Create(context->platform.get(), &errors,
                                           filtered_args, context->exec_args);
    }
    if (!context->setup) {
        for (const std::string &err : errors)
            fprintf(stderr, "%s: %s\n", binary_path.c_str(), err.c_str());
        return 1;
    }

    Environment *env = context->setup->env();
    uv_loop_t *loop = context->setup->event_loop();
    context->isolate = context->setup->isolate();
    context->env = env;
    context->loop = loop;
    Isolate *isolate = context->isolate;

    isolate->SetData(0, context);

    int exit_code = 0;
    {
        Locker locker(isolate);
        Isolate::Scope isolate_scope(isolate);
        HandleScope handle_scope(isolate);
        v8::Local<Context> local_ctx = context->setup->context();
        v8::Global<Context> global_ctx(isolate, local_ctx);
        context->global_ctx = std::move(global_ctx);
        Context::Scope context_scope(local_ctx);

        v8::Local<v8::Array> importArr =
            v8::Array::New(context->isolate, num_imports);
        for (int i = 0; i < num_imports; i++) {
            importArr
                ->Set(local_ctx, i,
                      v8::String::NewFromUtf8(isolate, imports[i])
                          .ToLocalChecked())
                .Check();
        }

        local_ctx->Global()
            ->Set(local_ctx,
                  v8::String::NewFromUtf8Literal(isolate, "importNames"),
                  importArr)
            .Check();

        v8::Local<v8::Function> require = v8::Local<v8::Function>::Cast(
            node::LoadEnvironment(
                env,
                R"(const { createRequire } = require('module');
                 const publicRequire = createRequire(process.cwd() + '/');
                 
(async (names) => {
  if (globalThis.imports || !names) return;

  globalThis.imports = {};

  await Promise.all(names.map(async name => {
    globalThis.imports[name] = await import(`./${name}.js`);
  }));

  delete globalThis.importNames;
  
})(globalThis.importNames);
                 return publicRequire;)")
                .ToLocalChecked());

        run_loop_blocking(context);

        local_ctx->Global()
            ->Set(local_ctx, v8::String::NewFromUtf8Literal(isolate, "require"),
                  require)
            .Check();

        v8::Local<Value> vm_string[] = {
            v8::String::NewFromUtf8Literal(isolate, "vm")};
        v8::Local<Value> vm =
            require->Call(isolate, local_ctx, local_ctx->Global(), 1, vm_string)
                .ToLocalChecked();

        v8::Global<v8::Function> runInThisContext(
            isolate, vm.As<v8::Object>()
                         ->Get(local_ctx, v8::String::NewFromUtf8Literal(
                                              isolate, "runInThisContext"))
                         .ToLocalChecked()
                         .As<v8::Function>());

        context->runInThisContext = std::move(runInThisContext);

        run_loop_blocking(context);
    }

    return exit_code;
}

std::string GetV8TypeAsString(v8::Isolate *isolate,
                              v8::Local<v8::Value> value) {
    v8::Local<v8::String> type_str = value->TypeOf(isolate);
    v8::String::Utf8Value utf8(isolate, type_str);
    return std::string(*utf8);
}

NodeValue NodeContext_Run_Script(NodeContext *context, const char *code) {

    NodeValue nv_res = {};

    {
        Locker locker(context->isolate);
        Isolate::Scope isolate_scope(context->isolate);
        HandleScope handle_scope(context->isolate);
        v8::Local<Context> local_ctx =
            context->global_ctx.Get(context->isolate);
        Context::Scope context_scope(local_ctx);

        v8::Local<Value> s[] = {
            v8::String::NewFromUtf8(context->isolate, code).ToLocalChecked(),
        };
        v8::Local<v8::Value> result =
            context->runInThisContext.Get(context->isolate)
                ->Call(context->isolate, local_ctx, local_ctx->Global(), 1, s)
                .ToLocalChecked();

        // v8::Local<v8::Value> result =
        //     node::LoadEnvironment(context->env, code).ToLocalChecked();

        nv_res = to_node_value(context, local_ctx, result);

        run_loop_blocking(context);
    }

    return nv_res;
}

void js_function_callback(const v8::FunctionCallbackInfo<v8::Value> &args) {

    v8::Local<v8::External> data = v8::Local<v8::External>::Cast(args.Data());
    FuncInfo *info = static_cast<FuncInfo *>(data->Value());

    Isolate *isolate = args.GetIsolate();

    HandleScope handle_scope(isolate);

    v8::Local<Context> local_ctx = info->context->global_ctx.Get(isolate);

    void *result = nullptr;

    if (args.Length() == 0) {
        result = info->context->py_callback(info->name, NULL, 0);
    } else {
        NodeValue *arr = (NodeValue *)malloc(args.Length() * sizeof(NodeValue));

        for (int i = 0; i < args.Length(); i++) {
            v8::Local<v8::Value> arg = args[i];
            arr[i] = to_node_value(info->context, local_ctx, arg);
        }

        result = info->context->py_callback(info->name, arr, args.Length());
    }
    if (result != nullptr) {
        args.GetReturnValue().Set(
            to_v8_value(info->context, local_ctx, *((NodeValue *)result)));
    }
}

NodeValue NodeContext_Create_Function(NodeContext *context,
                                      const char *function_name) {

    Locker locker(context->isolate);
    Isolate::Scope isolate_scope(context->isolate);
    HandleScope handle_scope(context->isolate);
    v8::Local<Context> local_ctx = context->global_ctx.Get(context->isolate);

    FuncInfo *info = new FuncInfo;
    info->name = strdup(function_name);
    info->context = context;
    v8::Local<v8::External> external_data =
        v8::External::New(context->isolate, info);

    v8::Local<v8::FunctionTemplate> tpl = v8::FunctionTemplate::New(
        context->isolate, js_function_callback, external_data);
    v8::Local<v8::Function> fn = tpl->GetFunction(local_ctx).ToLocalChecked();

    local_ctx->Global()
        ->Set(local_ctx,
              v8::String::NewFromUtf8(context->isolate, function_name)
                  .ToLocalChecked(),
              fn)
        .Check();

    return to_node_value(context, local_ctx, fn);
}

NodeValue NodeContext_Call_Function(NodeContext *context, NodeValue function,
                                    NodeValue *args, size_t args_length) {

    Locker locker(context->isolate);
    Isolate::Scope isolate_scope(context->isolate);
    HandleScope handle_scope(context->isolate);
    v8::Local<Context> local_ctx = context->global_ctx.Get(context->isolate);
    v8::Context::Scope context_scope(local_ctx);

    v8::Local<v8::Value> *args_arr = new v8::Local<v8::Value>[args_length];
    for (size_t i = 0; i < args_length; i++) {
        auto v = to_v8_value(context, local_ctx, args[i]);
        debugValue(v, context->isolate, local_ctx);
        args_arr[i] = v;
    }

    v8::Local<v8::Function> func =
        function.function->function.Get(context->isolate);

    v8::Local<Value> recv = local_ctx->Global();
    if (function.parent != nullptr) {
        recv = ((Val *)function.parent)->value.Get(context->isolate);
    }

    v8::Local<v8::Value> result =
        func->Call(local_ctx, // <— no Isolate* here
                   recv, static_cast<int>(args_length), args_arr)
            .ToLocalChecked();
    run_loop_blocking(context);

    return to_node_value(context, local_ctx, result);
}

void NodeContext_Define_Global(NodeContext *context, const char **keys,
                               NodeValue *values, int length) {

    Locker locker(context->isolate);
    Isolate::Scope isolate_scope(context->isolate);
    HandleScope handle_scope(context->isolate);
    v8::Local<Context> local_ctx = context->global_ctx.Get(context->isolate);
    v8::Context::Scope context_scope(local_ctx);
    for (int i = 0; i < length; i++) {
        local_ctx->Global()
            ->Set(local_ctx,
                  v8::String::NewFromUtf8(context->isolate, keys[i])
                      .ToLocalChecked(),
                  to_v8_value(context, local_ctx, values[i]))
            .Check();
    }
}

NodeValue NodeContext_Construct_Function(NodeContext *context,
                                         NodeValue function, NodeValue *args,
                                         size_t args_length) {

    Locker locker(context->isolate);
    Isolate::Scope isolate_scope(context->isolate);
    HandleScope handle_scope(context->isolate);
    v8::Local<Context> local_ctx = context->global_ctx.Get(context->isolate);
    v8::Context::Scope context_scope(local_ctx);
    std::vector<v8::Local<v8::Value>> args_vec = {};
    for (int i = 0; i < args_length; i++) {
        args_vec.push_back(to_v8_value(context, local_ctx, args[i]));
    }
    v8::Local<v8::Function> func =
        function.function->function.Get(context->isolate);

    v8::Local<v8::Value> result =
        func->NewInstance(local_ctx, args_length, args_vec.data())
            .ToLocalChecked();

    run_loop_blocking(context);

    return to_node_value(context, local_ctx, result);
}

void NodeContext_Stop(NodeContext *context) { node::Stop(context->env); }

void NodeContext_Dispose(NodeContext *context) {
    context->global_ctx.Reset();
    context->runInThisContext.Reset();
    V8::Dispose();
    V8::DisposePlatform();
    node::TearDownOncePerProcess();
}

void Node_Dispose_Value(NodeValue value) {
    if (value.function != nullptr) {
        value.function->function.Reset();
        delete value.function;
    }
    if (value.val_big != nullptr) {
        free(value.val_big);
        value.val_big = nullptr;
    }
    if (value.val_string != nullptr) {
        free(value.val_string);
        value.val_string = nullptr;
    }
    if (value.val_tarray != nullptr) {
        free(value.val_tarray);
        value.val_tarray = nullptr;
    }
    if (value.error_message != nullptr) {
        free(value.error_message);
        value.error_message = nullptr;
    }
    if (value.error_name != nullptr) {
        free(value.error_name);
        value.error_name = nullptr;
    }
    if (value.error_stack != nullptr) {
        free(value.error_stack);
        value.error_stack = nullptr;
    }
    if (value.parent != nullptr) {
        //((Val*) value.parent)->value.Reset();
        // delete ((Val*) value.parent);
    }
    if (value.val_array != nullptr) {
        free(value.val_array);
        value.val_array = nullptr;
    }
    if (value.object_keys != nullptr) {
        for (uint32_t i = 0; i < value.object_len; ++i) {
            if (value.object_keys[i] != nullptr) {
                free(value.object_keys[i]);
            }
        }
        free(value.object_keys);
        value.object_keys = nullptr;
    }
    if (value.object_values != nullptr) {
        free(value.object_values);
        value.object_values = nullptr;
    }
    if (value.map_keys != nullptr) {
        free(value.map_keys);
        value.map_keys = nullptr;
    }
}
