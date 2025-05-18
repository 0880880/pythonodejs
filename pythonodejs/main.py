from typing import Union
from pathlib import Path
import platform
import weakref
import ctypes
import types
import os

#get_arch = lambda: {"x86_64": "amd64","aarch64": "arm64","arm64": "arm64","amd64": "amd64"}.get(machine := platform.machine().lower()) or (_ for _ in ()).throw(RuntimeError(f"Unsupported architecture: {machine}"))

def _get_lib_path():
    base_dir = os.path.dirname(__file__)
    lib_dir = os.path.join(base_dir, 'lib')

    system = platform.system().lower()
    #arch = get_arch()

    if system == 'windows':
        lib_name = f'pythonodejs.dll'
    elif system == 'linux':
        lib_name = f'pythonodejs.so'
    elif system == 'darwin':  # macOS
        lib_name = f'pythonodejs.dylib'
    else:
        raise RuntimeError(f"Unsupported platform: {system}")

    path = os.path.join(lib_dir, lib_name)
    return path


_lib = ctypes.CDLL(_get_lib_path())

# Define the NodeValue structure
class NodeValue(ctypes.Structure):
    pass

NodeValue._fields_ = [
    ("type", ctypes.c_int),
    ("val_bool", ctypes.c_int),
    ("val_num", ctypes.c_double),
    ("val_string", ctypes.c_char_p),
    ("val_symbol", ctypes.c_char_p),
    ("function_name", ctypes.c_char_p),
    ("function", ctypes.c_void_p),
    ("val_array", ctypes.POINTER(NodeValue)),
    ("val_array_len", ctypes.c_int),
    ("val_big", ctypes.c_char_p),
    ("object_keys", ctypes.POINTER(ctypes.c_char_p)),
    ("object_values", ctypes.POINTER(NodeValue)),
    ("object_len", ctypes.c_int),
    ("parent", ctypes.c_void_p),
]

# Set function signatures
_lib.NodeContext_Create.restype = ctypes.c_void_p
_lib.NodeContext_Create.argtypes = []

_lib.NodeContext_Setup.restype = ctypes.c_int
_lib.NodeContext_Setup.argtypes = [ctypes.c_void_p, ctypes.c_int, ctypes.POINTER(ctypes.c_char_p)]

_lib.NodeContext_Init.restype = ctypes.c_int
_lib.NodeContext_Init.argtypes = [ctypes.c_void_p, ctypes.c_int]

_lib.NodeContext_Run_Script.restype = NodeValue
_lib.NodeContext_Run_Script.argtypes = [ctypes.c_void_p, ctypes.c_char_p]

_lib.NodeContext_Call_Function.restype = NodeValue
_lib.NodeContext_Call_Function.argtypes = [ctypes.c_void_p, NodeValue, ctypes.POINTER(NodeValue), ctypes.c_size_t]

_lib.NodeContext_Stop.restype = None
_lib.NodeContext_Stop.argtypes = [ctypes.c_void_p]

_lib.NodeContext_Destroy.restype = None
_lib.NodeContext_Destroy.argtypes = [ctypes.c_void_p]

_lib.NodeContext_Dispose.restype = None
_lib.NodeContext_Dispose.argtypes = [ctypes.c_void_p]

_lib.Node_Dispose_Value.restype = None
_lib.Node_Dispose_Value.argtypes = [NodeValue]

# Optional enum constants for NodeValueType
UNDEFINED = 0
NULL_T = 1
BOOLEAN_T = 2
NUMBER = 3
STRING = 4
SYMBOL = 5
FUNCTION = 6
ARRAY = 7
BIGINT = 8
OBJECT = 9
UNKOWN = 10

class NativeArray(list):
    def __init__(self, node, iterable=()):
        super().__init__(iterable)
        self._node = node

    def __del__(self):
        _lib.Node_Dispose_Value(self._node)

class NativeObject(dict):
    def __init__(self, *args, node=None, **kwargs):
        super().__init__(*args, **kwargs)
        self._node = node

    def __del__(self):
        _lib.Node_Dispose_Value(self._node)


class Func:
    def __init__(self, name, node, f):
        self.name = name
        self._node = node
        self._f = f

    def __call__(self, *args, **kwargs):
        L = len(args)
        n_args = (NodeValue * L)()
        for i in range(L):
            n_args[i] = _to_node(self._node, args[i])
        return _to_python(self._node, _lib.NodeContext_Call_Function(self._node._context, self._f, n_args, len(args)))

    def __str__(self):
        return f"{self.name}@Node"
        
    def __del__(self):
        _lib.Node_Dispose_Value(self._f)

def _to_node(node, value): # TODO SYMBOL
    v = NodeValue()
    if not value:
        v.type = NULL_T
    elif isinstance(value, bool):
        v.type = BOOLEAN_T
        v.val_bool = 1 if value else 0
    elif isinstance(value, int):
        v.type = NUMBER
        v.val_num = value
    elif isinstance(value, str):
        v.type = STRING
        v.val_string = value.encode("utf-8")
    elif isinstance(value, (list, tuple, set)):
        v.type = ARRAY
        val = list(value)
        L = len(value)
        arr = (NodeValue * L)()
        for i in range(L):
            arr[i] = _to_node(node, val[i])
        v.val_array_len = L
        v.val_array = arr
    elif isinstance(value, dict):
        v.type = OBJECT
        L = len(value)
        keys = list(value.keys())
        values = (NodeValue * L)()
        for i in range(L):
            values[i] = _to_node(node, value[keys[i]])
        keys = [key.encode("utf-8") for key in keys]
        n_keys = (ctypes.c_char_p * L)(*keys)
        v.object_len = L
        v.object_keys = n_keys
        v.object_values = values
    else:
        v.type = STRING
        v.val_string = value.__str__().encode("utf-8")
    return v

def _to_python(node, value: NodeValue): # TODO SYMBOL
    if value.type == BOOLEAN_T:
        return bool(value.val_bool)
    elif value.type == NUMBER:
        return value.val_num
    elif value.type == STRING:
        s = value.val_string.decode("utf-8")
        _lib.Node_Dispose_Value(value)
        return s
    elif value.type == FUNCTION:
        return Func(value.function_name, node, value)
    elif value.type == ARRAY:
        arr = NativeArray(value)
        L = value.val_array_len
        for i in range(L):
            arr.append(_to_python(node, value.val_array[i]))
        return arr
    elif value.type == BIGINT:
        i = int(value.val_big.decode("utf-8"))
        _lib.Node_Dispose_Value(value)
        return i
    elif value.type == OBJECT:
        obj = NativeObject(node=value)
        L = value.object_len
        for i in range(L):
            obj[value.object_keys[i].decode("utf-8")] = _to_python(node, value.object_values[i])
        return obj
    return None

class Node:
    def __init__(self, path=__file__, thread_pool_size=1):
        self.cleaned = False
        self._context = _lib.NodeContext_Create()

        argc = 1
        argv = (ctypes.c_char_p * argc)(path.encode("utf-8"))

        error = _lib.NodeContext_Setup(self._context, 1, argv)
        if not error == 0:
            raise Exception("Failed to setup node.")
        error = _lib.NodeContext_Init(self._context, thread_pool_size)
        if not error == 0:
            raise Exception("Failed to init node.")
        
    def require(self, module: str):
        js_mod = self.eval(f"(() => {{ try {{ return require('{module}'); }} catch {{}} }})()")
        if not js_mod:
            raise Exception(f"Failed to import module {module}")
        mod = types.ModuleType(module)
        for key in js_mod:
            setattr(mod, key, js_mod[key])
        return mod

    def eval(self, code: str):
        return _to_python(self, _lib.NodeContext_Run_Script(self._context, code.encode("utf-8")))

    def run(self, fp: Union[str, Path]):
        eval(Path(fp).read_text("utf-8"))

    def stop(self):
        _lib.NodeContext_Stop(self._context)

    def dispose(self):
        self.cleaned = True
        self.stop()
        _lib.NodeContext_Dispose(self._context)

    def __del__(self):
        self.stop()
        if not self.cleaned:
            self.dispose()
