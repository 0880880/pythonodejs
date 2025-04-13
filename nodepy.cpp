#include "nodepy.h"

#include <assert.h>
#include <vector>
#include <string>
#include <memory>

#include "node.h"
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
  Environment* env;
  std::vector<std::string> args;
  std::vector<std::string> exec_args;
};

NodeContext* NodeContext_Create() {
  return new NodeContext();
}

void NodeContext_Destroy(NodeContext* context) {
  delete context;
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

void NodeContext_Init(NodeContext* context, int thread_pool_size) {
  context->platform = MultiIsolatePlatform::Create(thread_pool_size);
  V8::InitializePlatform(context->platform.get());
  V8::Initialize();
}

void NodeContext_SetCode(NodeContext* context, const char* code) {
  if (context->args.size() == 1) {
    context->args.push_back(code);
    return;
  }
  context->args[1] = code;
}

int NodeContext_Run(NodeContext* context) {
  std::vector<std::string> errors;
  std::unique_ptr<CommonEnvironmentSetup> setup =
      CommonEnvironmentSetup::Create(context->platform.get(), &errors, context->args, context->exec_args);

  if (!setup) {
    for (const std::string& err : errors)
      fprintf(stderr, "%s: %s\n", context->args[0].c_str(), err.c_str());
    return 1;
  }

  Isolate* isolate = setup->isolate();
  Environment* env = setup->env();
  context->env = env;

  int exit_code = 0;
  {
    Locker locker(isolate);
    Isolate::Scope isolate_scope(isolate);
    HandleScope handle_scope(isolate);
    v8::Local<Context> local_ctx = setup->context();

    Context::Scope context_scope(local_ctx);

    MaybeLocal<Value> loadenv_ret = node::LoadEnvironment(
        env,
        "const publicRequire ="
        "  require('node:module').createRequire(process.cwd() + '/');"
        "globalThis.require = publicRequire;"
        "require('node:vm').runInThisContext(process.argv[1]);");

    if (loadenv_ret.IsEmpty())
      return 1;

    exit_code = node::SpinEventLoop(env).FromMaybe(1);

    node::Stop(env);
  }

  return exit_code;
}

void NodeContext_Stop(NodeContext* context) {
  node::Stop(context->env);
}

void Node_Dispose() {
  V8::Dispose();
  V8::DisposePlatform();
  node::TearDownOncePerProcess();
}

int main(int argc, char** argv) {
  NodeContext* context = NodeContext_Create();
  int exit_code = NodeContext_Setup(context, argc, argv);
  if (exit_code != 0) {
    NodeContext_Destroy(context);
    return exit_code;
  }

  NodeContext_Init(context, 4);
  NodeContext_SetCode(context, "console.log('Hello, World!');");
  int ret = NodeContext_Run(context);

  Node_Dispose();
  NodeContext_Destroy(context);
  return ret;
}
