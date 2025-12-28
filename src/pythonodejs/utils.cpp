
#include "utils.h"
#include "common.h"
#include <atomic>
#include <cctype>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <random>
#include <thread>
#include <vector>

using std::string;
using std::vector;
using v8::Context;
using v8::Function;
using v8::Isolate;
using v8::Local;
using v8::Object;
using v8::String;
using v8::Value;

#ifdef _WIN32
void FixupMain(int argc, char* raw_argv[], char*** argv)
{
    *argv = raw_argv;
}
#else
void FixupMain(int argc, char* raw_argv[], char*** argv)
{
    *argv = uv_setup_args(argc, raw_argv);
    setvbuf(stdout, nullptr, _IONBF, 0);
    setvbuf(stderr, nullptr, _IONBF, 0);
}
#endif

v8::Local<v8::Value> GetValueByKey(v8::Local<v8::Context> context, v8::Isolate* isolate,
    v8::Local<v8::Object> obj,
    const std::string& key)
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

void SpinEventLoopAsync(node::Environment* env)
{
    CHECK_NOT_NULL(env);
    node::MultiIsolatePlatform* platform = node::GetMultiIsolatePlatform(env);
    CHECK_NOT_NULL(platform);

    v8::Isolate* isolate = env->isolate();
    v8::HandleScope handle_scope(isolate);
    v8::Context::Scope context_scope(env->context());
    v8::SealHandleScope seal(isolate);

    env->set_trace_sync_io(env->options()->trace_sync_io);
    {
        if (!env->is_stopping())
            uv_run(env->event_loop(), UV_RUN_NOWAIT);
        if (!env->is_stopping())
            platform->DrainTasks(isolate);
    }
}

void SpinEventLoopSync(node::Environment* env, bool until_idle)
{
    CHECK_NOT_NULL(env);
    node::MultiIsolatePlatform* platform = node::GetMultiIsolatePlatform(env);
    CHECK_NOT_NULL(platform);

    v8::Isolate* isolate = env->isolate();
    v8::HandleScope handle_scope(isolate);
    v8::Context::Scope context_scope(env->context());
    v8::SealHandleScope seal(isolate);

    env->set_trace_sync_io(env->options()->trace_sync_io);
    {

        bool more = true;
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
