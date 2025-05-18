#ifndef NODE_EMBED_H
#define NODE_EMBED_H

#ifdef _WIN32
  #define EXPORT __declspec(dllexport)
#else
  #define EXPORT
#endif
#include <stddef.h>


#ifdef __cplusplus
extern "C" {
#endif

#ifndef __cplusplus
    typedef enum { false, true } bool;
#endif

    typedef struct NodeContext NodeContext;
    typedef struct Func Func;
    typedef struct Val Val;

    typedef enum NodeValueType : int {  // explicitly 4 bytes
      UNDEFINED, NULL_T, BOOLEAN_T, NUMBER, STRING,
      SYMBOL, FUNCTION, ARRAY, BIGINT, OBJECT, UNKNOWN
    } NodeValueType;

    typedef struct NodeValue {
        NodeValueType type;
        bool val_bool;
        double val_num;
        char* val_string;
        char* val_symbol;
        char* function_name;
        struct Func* function;
        struct NodeValue* val_array;
        int val_array_len;
        char* val_big;
        char** object_keys;
        struct NodeValue* object_values;
        int object_len;
        void* parent;
    } NodeValue;

    EXPORT NodeContext* NodeContext_Create();
    EXPORT int NodeContext_Setup(NodeContext* context, int argc, char** argv);
    EXPORT int NodeContext_Init(NodeContext* context, int thread_pool_size);

    EXPORT NodeValue NodeContext_Run_Script(NodeContext* context, const char* code);
    EXPORT NodeValue NodeContext_Call_Function(NodeContext* context, NodeValue function, NodeValue* args, size_t args_length);

    EXPORT void NodeContext_Stop(NodeContext* context);
    EXPORT void NodeContext_Destroy(NodeContext* context);
    EXPORT void NodeContext_Dispose(NodeContext* context);

    EXPORT void Node_Dispose_Value(NodeValue value);

#ifdef __cplusplus
}
#endif

#endif // NODE_EMBED_H
