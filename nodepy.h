#ifndef NODE_EMBED_H
#define NODE_EMBED_H

#ifdef __cplusplus
extern "C" {
#endif

    typedef struct NodeContext NodeContext;

    // Create a new NodeContext instance.
    // Returns a pointer to a new NodeContext, or NULL on failure.
    NodeContext* NodeContext_Create(void);

    // Set up the context using command-line arguments.
    // Returns 0 on success; nonzero indicates an error.
    int NodeContext_Setup(NodeContext* context, int argc, char** argv);

    // Initialize the Node environment using the provided thread pool size.
    void NodeContext_Init(NodeContext* context, int thread_pool_size);

    // Set the script/code string to run.
    // 'code' must be a null-terminated C string.
    void NodeContext_SetCode(NodeContext* context, const char* code);

    // Run the Node environment.
    // Returns an exit code; nonzero usually indicates that an error occurred.
    int NodeContext_Run(NodeContext* context);

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
