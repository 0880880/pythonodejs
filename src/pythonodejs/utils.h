#pragma once
#include "common.h"

#ifdef _WIN32
void FixupMain(int argc, char* raw_argv[], char*** argv);
#else
using argv_type = char*;
void FixupMain(int argc, argv_type raw_argv[], char*** argv);
#endif

std::string encode_base62(uint64_t value);
std::string random_string();

v8::Local<v8::Value> GetValueByKey(v8::Local<v8::Context> context, v8::Isolate* isolate,
    v8::Local<v8::Object> obj,
    const std::string& key);

void SpinEventLoopAsync(node::Environment* env);
void SpinEventLoopSync(node::Environment* env, bool until_idle);
