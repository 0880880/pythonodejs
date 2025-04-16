#ifndef NODE_EMBED_H
#define NODE_EMBED_H

#ifdef __cplusplus
extern "C" {
#endif

	#ifndef __cplusplus
    typedef enum { false, true } bool;
    #endif

    typedef struct NodeContext NodeContext;
    typedef struct Func Func;

    typedef enum { UNDEFINED, NULL_T, BOOLEAN_T, NUMBER, STRING, SYMBOL, FUNCTION, ARRAY, BIGINT, OBJECT, UNKOWN } NodeValueType;

    typedef struct NodeValue {
        NodeValueType type;
        bool val_bool;
        double val_num;
        char* val_string;
        char* val_symbol;
        char* function_name;
        Func* function;
        struct NodeValue* val_array;
        int val_array_len;
        char* val_big;
        char** object_keys;
        struct NodeValue* object_values;
        int object_len;
    } Node1;

    // Create a new NodeContext instance.
    // Returns a pointer to a new NodeContext, or NULL on failure.
    NodeContext* NodeContext_Create(void);

    // Set up the context using command-line arguments.
    // Returns 0 on success; nonzero indicates an error.
    int NodeContext_Setup(NodeContext* context, int argc, char** argv);

    // Initialize the Node environment using the provided thread pool size.
    int NodeContext_Init(NodeContext* context, int thread_pool_size);

    NodeValue NodeContext_Run_Script(NodeContext* context, const char* code);
    char* Node_Value_To_String(NodeValue value);

    NodeValue NodeContext_Call_Function(NodeContext* context, char* function_name, NodeValue* args, int args_length);

    // Stop the Node environment.
    void NodeContext_Stop(NodeContext* context);

    // Destroy the NodeContext instance and free associated resources.
    void NodeContext_Destroy(NodeContext* context);

    // Dispose of global resources (e.g. V8 and platform resources).
    void Node_Dispose(void);

#ifdef __cplusplus
}
#endif

#endif // NODE_EMBED_H
