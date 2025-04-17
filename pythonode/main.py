from types import SimpleNamespace
from enum import IntEnum
import cffi
import sys
import os

__ffi__ = cffi.FFI()

__ffi__.cdef("""
typedef enum { false, true } bool;

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
} NodeValue;

NodeContext* NodeContext_Create();
int NodeContext_Setup(NodeContext* context, int argc, char** argv);
int NodeContext_Init(NodeContext* context, int thread_pool_size);
NodeValue NodeContext_Run_Script(NodeContext* context, const char* code);
NodeValue NodeContext_Call_Function(NodeContext* context, NodeValue function, NodeValue* args, size_t args_length);
void NodeContext_Stop(NodeContext* context);
void NodeContext_Destroy(NodeContext* context);
void NodeContext_Dispose(NodeContext* context);
char* Node_Value_To_String(NodeValue value);
void Node_Dispose_Value(NodeValue value);
""")
__lib__ = __ffi__.dlopen(os.path.join(os.path.dirname(os.path.dirname(__file__)), 'lib', 'pythonode-windows-amd64.dll'))

class NodeValueType(IntEnum):
    UNDEFINED = 0
    NULL = 1
    BOOLEAN = 2
    NUMBER = 3
    STRING = 4
    SYMBOL = 5
    FUNCTION = 6
    ARRAY = 7
    BIGINT = 8
    OBJECT = 9
    UNKOWN = 10

def __setup__():

    context = __lib__.NodeContext_Create()

    argv = __ffi__.new("char*[]", [__ffi__.new("char[]", sys.argv[0].encode("utf-8"))])

    error = __lib__.NodeContext_Setup(context, 1, argv)
    if not error == 0:
        raise Exception(f"Failed to setup node context: Error {error}")
    return context


class Function:

    def __init__(self, node, f):
        self.__node__ = node
        self.__f__ = f

    def __call__(self, *args, **kwargs):
        a = __ffi__.new("NodeValue[]", len(args))
        i = 0
        for arg in args:
            a[i] = __to_node__(self.__node__, arg)
            i += 1
        res = __lib__.NodeContext_Call_Function(self.__node__.__context__, self.__f__, a, len(args))
        return __to_python__(self.__node__, res)

def __to_python__(node, value):
    if value.type == NodeValueType.UNDEFINED or value.type == NodeValueType.NULL:
        return None
    elif value.type == NodeValueType.BOOLEAN:
        return bool(value.val_bool)
    elif value.type == NodeValueType.NUMBER:
        return value.val_num
    elif value.type == NodeValueType.STRING:
        return __ffi__.string(value.val_string).decode()
    elif value.type == NodeValueType.SYMBOL:
        return None # TODO Not implemented yet
    elif value.type == NodeValueType.FUNCTION:
        return Function(node, value)
    elif value.type == NodeValueType.ARRAY:
        return [__to_python__(node, value.val_array[i]) for i in range(value.val_array_len)]
    elif value.type == NodeValueType.BIGINT:
        return int(__ffi__.string(value.val_big).decode())
    elif value.type == NodeValueType.OBJECT:
        data = {__ffi__.string(value.object_keys[i]).decode(): __to_python__(value.object_values[i]) for i in range(value.object_len)}
        return SimpleNamespace(**data)
    return None

def __to_node__(node, value):
    v = __ffi__.new("NodeValue *")
    __ffi__.release(v)
    if not value:
        v.type = NodeValueType.NULL
    elif isinstance(value, (int, float)):
        if isinstance(value, int) and not (-(2**53 - 1) <= value <= 2**53 - 1):
            v.type = NodeValueType.BIGINT
            v.val_big = __ffi__.new("char[]", str(value).encode("utf-8"))
        else:
            v.type = NodeValueType.NUMBER
            v.val_num = float(value)
    elif isinstance(value, (list, tuple, set)):
        length = len(value)
        arr = __ffi__.new("NodeValue[]", length)
        for i in range(length):
            arr[i] = __to_node__(node, value[i])
        v.type = NodeValueType.ARRAY
        v.val_array_len = length
        v.val_array = arr
    elif isinstance(value, dict):
        v.type = NodeValueType.OBJECT
        length = len(value)
        v.object_len = length
        keys_p = []
        vals = __ffi__.new("NodeValue[]", length)
        i = 0
        for key in value:
            keys_p[i] = __ffi__.new("char[]]", key.encode("utf-8"))
            vals[i] = __to_node__(node, value[key])
            i += 1
        v.object_keys = __ffi__.new("char*[]", keys_p)
        v.object_values = vals
    elif isinstance(value, str):
        print(f"Converting string {value} to ")
        v.type = NodeValueType.STRING
        v.val_string = __ffi__.new("char[]", value.encode("utf-8"))
    else:
        v.type = NodeValueType.STRING
        v.val_string = __ffi__.new("char[]", value.__str__().encode("utf-8"))
    return v[0]

class Node:

    def __init__(self, thread_pool_size=1):
        self.__context__ = __setup__()
        self.__values__ = []
        error = __lib__.NodeContext_Init(self.__context__, thread_pool_size)
        if not error == 0:
            raise Exception(f"Failed to initialize node: Error {error}")

    def eval(self, code: str):
        value = __lib__.NodeContext_Run_Script(self.__context__, __ffi__.new("char[]", code.encode("utf-8")))
        Node.__values__ = value
        return __to_python__(self, value)

    def stop(self):
        __lib__.NodeContext_Stop(self.__context__)

    def dispose(self):
        for v in self.__values__:
            __lib__.Node_Dispose_Value(v)
        self.__values__.clear()
        __lib__.NodeContext_Stop(self.__context__)
        __lib__.NodeContext_Destroy(self.__context__)
        __lib__.NodeContext_Dispose(self.__context__)

    def __del__(self):
        self.dispose()
