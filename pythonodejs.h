#ifndef NODE_EMBED_H
#define NODE_EMBED_H

#ifdef _WIN32
#define EXPORT __declspec(dllexport)
#define NOMINMAX
#else
#define EXPORT
#endif
#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#ifndef __cplusplus
typedef enum { false, true } bool;
#endif

typedef struct NodeContext NodeContext;
typedef struct Func Func;
typedef struct Val Val;

typedef enum NodeValueType : int { // explicitly 4 bytes
    UNDEFINED,
    NULL_T,
    BOOLEAN_T,
    NUMBER,
    STRING,
    SYMBOL,
    FUNCTION,
    ARRAY,
    BIGINT,
    OBJECT,
    UNKNOWN,
    MAP,
    TYPED_ARRAY,
    ARRAY_BUFFER,
    DATA_VIEW, // UNUSED (ARRAY_BUFFER)
    EXTERNAL,
    DATE_T,
    REGEXP,
    PROXY,
    GENERATOR_OBJECT, // UNUSED (Object)
    MODULE_NAMESPACE, // UNUSED (Object)
    ERROR,
    PROMISE,
    SET
} NodeValueType;

typedef enum TypedArrayType : int { // explicitly 4 bytes
    INT8_T,
    UINT8_T,
    INT16_T,
    UINT16_T,
    INT32_T,
    UINT32_T,
    BINT64_T,
    BUINT64_T,
    FLOAT32_T,
    FLOAT64_T
} TypedArrayType;

typedef struct NodeValue {
    NodeValueType type;
    bool val_bool;
    double val_num;
    char *val_string; // Used for string, symbol description, function name, and
                      // regex pattern
    struct Func *function;
    struct NodeValue *val_array; // Reuse for set
    void *val_tarray;            // Reuse for arraybuffer and dataview
    TypedArrayType val_tarray_type;
    int val_array_len; // Reuse for set, typedarray and arraybuffer
    char *val_big;
    char **object_keys;
    struct NodeValue *map_keys;
    struct NodeValue *object_values; // Reuse for map
    int object_len;
    double val_date_unix;
    int val_regex_flags;
    void *val_external_ptr; // Reuse for Symbol

    int64_t future_id;

    char *error_message;
    char *error_name;
    char *error_stack;

    struct NodeValue *proxy_target;
    struct NodeValue *proxy_handler;
    void *parent;
} NodeValue;

typedef void *(*Callback)(const char *function_name, const NodeValue *values,
                          int length);
typedef void *(*FutureCallback)(int64_t id, NodeValue result, bool reject);

EXPORT NodeContext *NodeContext_Create();
EXPORT int NodeContext_Setup(NodeContext *context, int argc, char **argv);
EXPORT int NodeContext_Init(NodeContext *context, int thread_pool_size);

EXPORT void NodeContext_SetCallback(NodeContext *context, Callback cb);
EXPORT void NodeContext_SetFutureCallback(NodeContext *context,
                                          FutureCallback cb);
EXPORT void NodeContext_Define_Global(NodeContext *context, const char **keys,
                                      NodeValue *values, int length);

EXPORT void NodeContext_FutureUpdate(NodeContext *context, int64_t id,
                                     NodeValue value, bool rejected);

EXPORT NodeValue NodeContext_Run_Script(NodeContext *context, const char *code);
EXPORT NodeValue NodeContext_Create_Function(NodeContext *context,
                                             const char *function_name);
EXPORT NodeValue NodeContext_Call_Function(NodeContext *context,
                                           NodeValue function, NodeValue *args,
                                           size_t args_length);
EXPORT NodeValue NodeContext_Construct_Function(NodeContext *context,
                                                NodeValue function,
                                                NodeValue *args,
                                                size_t args_length);

EXPORT void NodeContext_Stop(NodeContext *context);
EXPORT void NodeContext_Destroy(NodeContext *context);
EXPORT void NodeContext_Dispose(NodeContext *context);

EXPORT void Node_Dispose_Value(NodeValue value);

#ifdef __cplusplus
}
#endif

#endif // NODE_EMBED_H
