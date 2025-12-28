#include "node_env.h"
#include "utils.h"
#include <memory>
#include <string>
#include <vector>

using std::string;
using std::unique_ptr;
using std::vector;

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
            node::ProcessInitializationFlags::kDisableNodeOptionsEnv,
            node::ProcessInitializationFlags::kNoInitializeCppgc,
        });

    platform = MultiIsolatePlatform::Create(thread_pool_size);
    v8::V8::InitializePlatform(platform.get());
    cppgc::InitializeProcess(platform->GetPageAllocator());
    v8::V8::Initialize();
    args = result->args();
    exec_args = result->exec_args();
}

void NodeFree()
{
    v8::V8::Dispose();
    v8::V8::DisposePlatform();
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
        MaybeLocal<Value> ret = node::LoadEnvironment(env,
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

        for (auto& pair : node->promises) {
            pair.second.Reset();
        }

        node->promises.clear();
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
