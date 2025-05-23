#include "pythonodejs.h"

#include <string>
#include <memory>
#include <iostream>
#include <vector>

#include "node.h"
#include "uv.h"
#include <Python.h>

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
  Environment* env;
  Isolate* isolate;
  v8::Global<Context> global_ctx;
  v8::Global<v8::Function> runInThisContext;
  Callback py_callback;
};

struct FuncInfo {
    const char* name;
    NodeContext* context;
};

struct Func {
  v8::Global<v8::Function> function;
};

struct Val {
  v8::Global<v8::Value> value;
};

NodeContext* NodeContext_Create() {
  return new NodeContext();
}

void NodeContext_Destroy(NodeContext* context) {
  //delete context;
}

int NodeContext_Setup(NodeContext* context, int argc, char** argv) {
  argv = uv_setup_args(argc, argv);
  std::vector<std::string> args(argv, argv + argc);
  std::shared_ptr<node::InitializationResult> result =
      node::InitializeOncePerProcess(
          args,
          {node::ProcessInitializationFlags::kNoInitializeV8,
           node::ProcessInitializationFlags::kNoInitializeNodeV8Platform});

  for (const std::string& error : result->errors())
    fprintf(stderr, "%s: %s\n", args[0].c_str(), error.c_str());

  if (result->early_return() != 0)
    return result->exit_code();

  context->args = {result->args()[0]};
  context->exec_args = result->exec_args();

  return result->exit_code();
}

void NodeContext_SetCallback(NodeContext* context, Callback cb) {
    if (!context) {
        std::cerr << "C ERROR: NodeContext_SetCallback called with NULL context!" << std::endl;
        return;
    }
    context->py_callback = cb;
}


NodeValue to_node_value(NodeContext* context, v8::Local<Context> local_ctx, v8::Local<Value> value) {
    if (value->IsUndefined()) {
    	return {.type=UNDEFINED};
    } else if (value->IsNull()) {
    	return {.type=NULL_T};
    } else if (value->IsNumber()) {
    	return {.type=NUMBER, .val_num=value.As<v8::Number>()->Value()};
    } else if (value->IsBoolean()) {
    	return {.type=BOOLEAN_T, .val_bool=value.As<v8::Boolean>()->Value()};
    } else if (value->IsString()) {
        v8::String::Utf8Value utf8(context->isolate, value.As<v8::String>());
        return {.type=STRING, .val_string=strdup(*utf8)};
    } else if (value->IsBigInt()) {
        v8::String::Utf8Value utf8(context->isolate, value.As<v8::BigInt>()->ToString(local_ctx).ToLocalChecked());
        return {.type=BIGINT, .val_big=strdup(*utf8)};
    } else if (value->IsFunction()) {
        NodeValue ret = {};
        v8::Local<v8::Function> func = value.As<v8::Function>();
        v8::String::Utf8Value utf8(context->isolate, func->GetName());
        Func* f = new Func();
		    f->function.Reset(context->isolate, func);
        ret.type = FUNCTION;
        ret.function_name = strdup(*utf8);
        ret.function = f;
        return ret;
    } else if (value->IsArray()) {
        v8::Local<v8::Array> array = value.As<v8::Array>();
        int length = array->Length();
        NodeValue* arr = (NodeValue*)malloc(length * sizeof(NodeValue));
        NodeValue nv = {.type=ARRAY, .val_array=arr, .val_array_len=length};
        for (int i = 0; i < length; i++) {
            arr[i] = to_node_value(context, local_ctx, array->Get(local_ctx, i).ToLocalChecked());
            Val* parent = new Val();
            parent->value.Reset(context->isolate, value);
            arr[i].parent = parent;
        }
    	return nv;
    } else if (value->IsObject()) {
        v8::Local<v8::Object> obj = value.As<v8::Object>();
		    v8::Local<v8::Array> keys = obj->GetOwnPropertyNames(local_ctx).ToLocalChecked();
        int length = keys->Length();
        char** key_arr = (char**)malloc(length * sizeof(char*));
        NodeValue* objects = (NodeValue*)malloc(length * sizeof(NodeValue));
        NodeValue nv = {.type=OBJECT, .object_keys=key_arr, .object_values=objects, .object_len=length};
        for (uint32_t i = 0; i < length; ++i) {
            v8::Local<v8::Value> key = keys->Get(local_ctx, i).ToLocalChecked();
            v8::String::Utf8Value utf8(context->isolate, key);
            size_t len = strlen(*utf8);
            key_arr[i] = (char*)malloc(len + 1);  // +1 for null terminator
            strcpy(key_arr[i], strdup(*utf8));
            v8::Local<v8::Value> oval;
			      if (obj->Get(local_ctx, key).ToLocal(&oval)) {
                objects[i] = to_node_value(context, local_ctx, oval);
				        Val* parent = new Val();
				        parent->value.Reset(context->isolate, value);
                objects[i].parent = parent;
			      }
		    }
    	return nv;
    }
    return {};
}


v8::Local<v8::Value> to_v8_value(NodeContext* context, v8::Local<Context> local_ctx, NodeValue value) {
    if (value.type == UNDEFINED) {
    	return v8::Undefined(context->isolate);
    } else if (value.type == NULL_T) {
    	return v8::Null(context->isolate);
    } else if (value.type == NUMBER) {
    	return v8::Number::New(context->isolate, value.val_num);
    } else if (value.type == BOOLEAN_T) {
    	return v8::Boolean::New(context->isolate, value.val_bool);
    } else if (value.type == STRING) {
        return v8::String::NewFromUtf8(context->isolate, value.val_string).ToLocalChecked();
    } else if (value.type == BIGINT) {
        v8::Local<v8::String> str = v8::String::NewFromUtf8(context->isolate, value.val_big).ToLocalChecked();
        //return v8::BigInt::New(context->isolate, str);
    } else if (value.type == FUNCTION) {
        return (*value.function).function.Get(context->isolate);
    } else if (value.type == ARRAY) {
		v8::Local<v8::Array> array = v8::Array::New(context->isolate, value.val_array_len);
        for (int i = 0; i < value.val_array_len; i++) {
        	v8::Maybe<bool> maybe_result = array->Set(local_ctx, i, to_v8_value(context, local_ctx, value.val_array[i]));
          if (maybe_result.IsNothing()) {
            std::cerr << "PYTHONODEJS: Failed to set array." << std::endl;
          }
        }
    	return array;
    } else if (value.type == OBJECT) {
        v8::Local<v8::Object> object = v8::Object::New(context->isolate);
        for (int i = 0; i < value.object_len; i++) {
        	v8::Maybe<bool> maybe_result = object->Set(local_ctx, v8::String::NewFromUtf8(context->isolate, value.object_keys[i]).ToLocalChecked(), to_v8_value(context, local_ctx, value.object_values[i]));
          if (maybe_result.IsNothing()) {
            std::cerr << "PYTHONODEJS: Failed to set object." << std::endl;
          }
        }
    	return object;
    }
    return {};
}

int NodeContext_Init(NodeContext* context, int thread_pool_size) {
  context->platform = MultiIsolatePlatform::Create(thread_pool_size);
  V8::InitializePlatform(context->platform.get());
  V8::Initialize();

  std::vector<std::string> errors;
  context->setup =
      CommonEnvironmentSetup::Create(context->platform.get(), &errors, context->args, context->exec_args);
  CommonEnvironmentSetup* setup = context->setup.get();

  if (!setup) {
    for (const std::string& err : errors)
      fprintf(stderr, "%s: %s\n", context->args[0].c_str(), err.c_str());
    return 1;
  }

  context->isolate = setup->isolate();
  context->env = setup->env();
  Isolate* isolate = context->isolate;
  Environment* env = context->env;

  int exit_code = 0;
  Locker locker(isolate);
  Isolate::Scope isolate_scope(isolate);
  HandleScope handle_scope(isolate);
  v8::Local<Context> local_ctx = setup->context();
  v8::Global<Context> global_ctx(isolate, local_ctx);
  context->global_ctx = std::move(global_ctx);
  Context::Scope context_scope(local_ctx);

  v8::Local<v8::Function> require = v8::Local<v8::Function>::Cast(node::LoadEnvironment(
      env,
      "const { createRequire } = require('module');"
      "const publicRequire = createRequire(process.cwd() + '/');"
      "globalThis.require = publicRequire;"
	  "return globalThis.require;"
  ).ToLocalChecked());

  exit_code = node::SpinEventLoop(env).FromMaybe(1);

  v8::Local<Value> vm_string[] = { v8::String::NewFromUtf8Literal(isolate, "vm") };
  v8::Local<Value> vm = require->Call(
      isolate,
      local_ctx,
      local_ctx->Global(),
      1,
      vm_string).ToLocalChecked();

  v8::Global<v8::Function> runInThisContext(isolate, vm.As<v8::Object>()->Get(
        setup->context(),
        v8::String::NewFromUtf8Literal(isolate, "runInThisContext")).ToLocalChecked().As<v8::Function>());
  context->runInThisContext = std::move(runInThisContext);

  return exit_code;
}

std::string GetV8TypeAsString(v8::Isolate* isolate, v8::Local<v8::Value> value) {
  v8::Local<v8::String> type_str = value->TypeOf(isolate);
  v8::String::Utf8Value utf8(isolate, type_str);
  return std::string(*utf8);
}

NodeValue NodeContext_Run_Script(NodeContext* context, const char* code) {
  Locker locker(context->isolate);
  Isolate::Scope isolate_scope(context->isolate);
  HandleScope handle_scope(context->isolate);
  v8::Local<Context> local_ctx = context->global_ctx.Get(context->isolate);
  Context::Scope context_scope(local_ctx);
  v8::Local<Value> s[] = { v8::String::NewFromUtf8(context->isolate, code).ToLocalChecked() };
  v8::Local<v8::Value> result = context->runInThisContext.Get(context->isolate)->Call(
    context->isolate,
    local_ctx,
    local_ctx->Global(),
    1,
    s
  ).ToLocalChecked();

  return to_node_value(context, local_ctx, result);
}

void js_function_callback(const v8::FunctionCallbackInfo<v8::Value>& args) {

    v8::Local<v8::External> data = v8::Local<v8::External>::Cast(args.Data());
    FuncInfo* info = static_cast<FuncInfo*>(data->Value());

    Isolate* isolate = args.GetIsolate();

    HandleScope handle_scope(isolate);

    v8::Local<Context> local_ctx = info->context->global_ctx.Get(isolate);

    void* result = nullptr;

    if (args.Length() == 0) {
        result = info->context->py_callback(info->name, NULL, 0);
    } else {
      NodeValue* arr = (NodeValue*)malloc(args.Length() * sizeof(NodeValue));

      for (int i = 0; i < args.Length(); i++) {
          v8::Local<v8::Value> arg = args[i];
          arr[i] = to_node_value(info->context, local_ctx, arg);
      }

      result = info->context->py_callback(info->name, arr, args.Length());
    }
    if (result != nullptr) {
        args.GetReturnValue().Set(to_v8_value(info->context, local_ctx, *((NodeValue*)result)));
    }
}


NodeValue NodeContext_Create_Function(NodeContext* context, const char* function_name) {

    Locker locker(context->isolate);
    Isolate::Scope isolate_scope(context->isolate);
    HandleScope handle_scope(context->isolate);
    v8::Local<Context> local_ctx = context->global_ctx.Get(context->isolate);

    FuncInfo* info = new FuncInfo;
    info->name=function_name;
    info->context=context;
    v8::Local<v8::External> external_data = v8::External::New(context->isolate, info);

    v8::Local<v8::FunctionTemplate> tpl = v8::FunctionTemplate::New(context->isolate, js_function_callback, external_data);
    v8::Local<v8::Function> fn = tpl->GetFunction(local_ctx).ToLocalChecked();

    local_ctx->Global()->Set(local_ctx, v8::String::NewFromUtf8(context->isolate, function_name).ToLocalChecked(), fn).Check();

    return to_node_value(context, local_ctx, fn);
}

NodeValue NodeContext_Call_Function(NodeContext* context, NodeValue function, NodeValue* args, size_t args_length) {

    Locker locker(context->isolate);
    Isolate::Scope isolate_scope(context->isolate);
    HandleScope handle_scope(context->isolate);
    v8::Local<Context> local_ctx = context->global_ctx.Get(context->isolate);
    std::vector<v8::Local<v8::Value>> args_vec = {};
    for (int i = 0; i < args_length; i++) {
        args_vec.push_back(to_v8_value(context, local_ctx, args[i]));
    }
    Context::Scope context_scope(local_ctx);
    v8::Local<v8::Function> func = function.function->function.Get(context->isolate);

    v8::Local<Value> recv = local_ctx->Global();
    if (function.parent != nullptr) {
        recv = ((Val*) function.parent)->value.Get(context->isolate);
    }

  	v8::Local<v8::Value> result = func->Call(
		context->isolate,
    	local_ctx,
    	recv,
        args_length,
        args_vec.data()
    ).ToLocalChecked();

    return to_node_value(context, local_ctx, result);
}

void NodeContext_Define_Global(NodeContext* context, const char** keys, NodeValue* values, int length) {

    Locker locker(context->isolate);
    Isolate::Scope isolate_scope(context->isolate);
    HandleScope handle_scope(context->isolate);
    v8::Local<Context> local_ctx = context->global_ctx.Get(context->isolate);
    for (int i = 0; i < length; i++) {
        local_ctx->Global()->Set(local_ctx, v8::String::NewFromUtf8(context->isolate, keys[i]).ToLocalChecked(), to_v8_value(context, local_ctx, values[i])).Check();
    }
}

NodeValue NodeContext_Construct_Function(NodeContext* context, NodeValue function, NodeValue* args, size_t args_length) {

    Locker locker(context->isolate);
    Isolate::Scope isolate_scope(context->isolate);
    HandleScope handle_scope(context->isolate);
    v8::Local<Context> local_ctx = context->global_ctx.Get(context->isolate);
    std::vector<v8::Local<v8::Value>> args_vec = {};
    for (int i = 0; i < args_length; i++) {
        args_vec.push_back(to_v8_value(context, local_ctx, args[i]));
    }
    v8::Local<v8::Function> func = function.function->function.Get(context->isolate);

  	v8::Local<v8::Value> result = func->NewInstance(
    	  local_ctx,
        args_length,
        args_vec.data()
    ).ToLocalChecked();

    return to_node_value(context, local_ctx, result);
}

void NodeContext_Stop(NodeContext* context) {
  node::Stop(context->env);
}

void NodeContext_Dispose(NodeContext* context) {
  context->global_ctx.Reset();
  context->runInThisContext.Reset();
  V8::Dispose();
  V8::DisposePlatform();
  node::TearDownOncePerProcess();
}

void Node_Dispose_Value(NodeValue value) {
    if (value.function != nullptr) {
        value.function->function.Reset();
        free(value.function_name);
        delete value.function;
    }
    if (value.val_big != nullptr) {
        free(value.val_big);
    }
    if (value.val_string != nullptr) {
        free(value.val_string);
    }
    if (value.parent != nullptr) {
        //((Val*) value.parent)->value.Reset();
        //delete ((Val*) value.parent);
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
    /*
    if (value.object_values != nullptr) {
        free(value.object_values);
        value.object_values = nullptr;
    }
    */
}


int main(int argc, char** argv) {
    NodeContext* context = NodeContext_Create();
    int exit_code = NodeContext_Setup(context, argc, argv);
    if (exit_code != 0) {
        NodeContext_Destroy(context);
        return exit_code;
    }

    NodeContext_Init(context, 4);
    NodeValue v = NodeContext_Run_Script(context, "const fs = require('fs');function readFile(filePath) {return fs.readFileSync(filePath, 'utf8');}readFile; // Returns readFile.");

    NodeValue j = {.type = STRING, .val_string = "simple.txt"};
    NodeValue n = NodeContext_Call_Function(context, v, &j, 1);
    std::cout << n.val_string << std::endl;

    NodeContext_Dispose(context);
    return 0;
}
