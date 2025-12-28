#pragma once
#include "common.h"

void NodeInit(int thread_pool_size);
void NodeFree();
NodeEnv* NodeEnvCreate(const char* absolute_path);
void NodeEnvFree(NodeEnv* node);
void PollSync(NodeEnv* node, bool blocking);
void PollAsync(NodeEnv* node);
