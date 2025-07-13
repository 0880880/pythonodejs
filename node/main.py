from typing import Coroutine, Union, Any
from pathlib import Path

import datetime
import platform
import asyncio
import ctypes
import random
import array
import types
import copy
import os
import re


def _get_lib_path():
    base_dir = os.path.dirname(__file__)
    lib_dir = os.path.join(base_dir, "lib")

    system = platform.system().lower()

    if system == "windows":
        lib_name = f"pythonodejs.dll"
    elif system == "linux":
        lib_name = f"pythonodejs.so"
    elif system == "darwin":
        lib_name = f"pythonodejs.dylib"
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
    ("self_ptr", ctypes.c_void_p),
    ("val_bool", ctypes.c_bool),
    ("val_num", ctypes.c_double),
    ("val_string", ctypes.c_char_p),
    ("function", ctypes.c_void_p),
    ("val_array", ctypes.POINTER(NodeValue)),
    ("val_tarray", ctypes.c_void_p),
    ("val_tarray_type", ctypes.c_int),
    ("val_array_len", ctypes.c_int),
    ("val_big", ctypes.c_char_p),
    ("object_keys", ctypes.POINTER(ctypes.c_char_p)),
    ("map_keys", ctypes.POINTER(NodeValue)),
    ("object_values", ctypes.POINTER(NodeValue)),
    ("object_len", ctypes.c_int),
    ("val_date_unix", ctypes.c_double),
    ("val_regex_flags", ctypes.c_int),
    ("val_external_ptr", ctypes.c_void_p),
    ("future_id", ctypes.c_int64),
    ("error_message", ctypes.c_char_p),
    ("error_name", ctypes.c_char_p),
    ("error_stack", ctypes.c_char_p),
    ("proxy_target", ctypes.POINTER(NodeValue)),
    ("proxy_handler", ctypes.POINTER(NodeValue)),
    ("parent", ctypes.c_void_p),
]

CALLBACK = ctypes.CFUNCTYPE(
    ctypes.c_void_p, ctypes.c_char_p, ctypes.POINTER(NodeValue), ctypes.c_int
)
FUTURE_CALLBACK = ctypes.CFUNCTYPE(
    ctypes.c_void_p, ctypes.c_int64, NodeValue, ctypes.c_bool
)

# Set function signatures
_lib.NodeContext_Create.restype = ctypes.c_void_p
_lib.NodeContext_Create.argtypes = []

_lib.NodeContext_Setup.restype = ctypes.c_int
_lib.NodeContext_Setup.argtypes = [
    ctypes.c_void_p,
    ctypes.c_int,
    ctypes.POINTER(ctypes.c_char_p),
]

_lib.NodeContext_Init.restype = ctypes.c_int
_lib.NodeContext_Init.argtypes = [
    ctypes.c_void_p,
    ctypes.POINTER(ctypes.c_char_p),
    ctypes.c_int,
    ctypes.c_int,
]

_lib.NodeContext_SetCallback.restype = None
_lib.NodeContext_SetCallback.argtypes = [ctypes.c_void_p, CALLBACK]

_lib.NodeContext_SetFutureCallback.restype = None
_lib.NodeContext_SetFutureCallback.argtypes = [ctypes.c_void_p, FUTURE_CALLBACK]

_lib.NodeContext_Define_Global.restype = None
_lib.NodeContext_Define_Global.argtypes = [
    ctypes.c_void_p,
    ctypes.POINTER(ctypes.c_char_p),
    ctypes.POINTER(NodeValue),
    ctypes.c_int,
]

_lib.NodeContext_FutureUpdate.restype = None
_lib.NodeContext_FutureUpdate.argtypes = [
    ctypes.c_void_p,
    ctypes.c_int64,
    ctypes.POINTER(NodeValue),
    ctypes.c_bool,
]

_lib.NodeContext_Run_Script.restype = NodeValue
_lib.NodeContext_Run_Script.argtypes = [ctypes.c_void_p, ctypes.c_char_p]

_lib.NodeContext_Create_Function.restype = NodeValue
_lib.NodeContext_Create_Function.argtypes = [ctypes.c_void_p, ctypes.c_char_p]

_lib.NodeContext_Call_Function.restype = NodeValue
_lib.NodeContext_Call_Function.argtypes = [
    ctypes.c_void_p,
    NodeValue,
    ctypes.POINTER(NodeValue),
    ctypes.c_size_t,
]

_lib.NodeContext_Construct_Function.restype = NodeValue
_lib.NodeContext_Construct_Function.argtypes = [
    ctypes.c_void_p,
    NodeValue,
    ctypes.POINTER(NodeValue),
    ctypes.c_size_t,
]

_lib.NodeContext_Stop.restype = None
_lib.NodeContext_Stop.argtypes = [ctypes.c_void_p]

_lib.NodeContext_Destroy.restype = None
_lib.NodeContext_Destroy.argtypes = [ctypes.c_void_p]

_lib.NodeContext_Dispose.restype = None
_lib.NodeContext_Dispose.argtypes = [ctypes.c_void_p]

_lib.Node_Dispose_Value.restype = None
_lib.Node_Dispose_Value.argtypes = [NodeValue]

_import_pattern = re.compile(r"(?<![\w])import\(([^)]+)\)")

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
MAP = 11
TYPED_ARRAY = 12
ARRAY_BUFFER = 13
DATA_VIEW = 14
EXTERNAL = 15
DATE_T = 16
REGEXP = 17
PROXY = 18
GENERATOR_OBJECT = 19
MODULE_NAMESPACE = 20
ERROR_T = 21
PROMISE = 22
SET = 23
RETURN = 24


INT8_T = 0
UINT8_T = 1
INT16_T = 2
UINT16_T = 3
INT32_T = 4
UINT32_T = 5
BINT64_T = 6
BUINT64_T = 7
FLOAT32_T = 8
FLOAT64_T = 9


def random_int64():
    val = random.getrandbits(64)
    if val >= 2**63:
        val -= 2**64
    return val


class JSValue:
    def __init__(self, nv):
        self._nv = nv


class NativeArray(list, JSValue):
    def __init__(self, nv, iterable=()):
        list.__init__(self, iterable)
        JSValue.__init__(self, nv)

    def __del__(self):
        _lib.Node_Dispose_Value(self._nv)


class NativeSet(set, JSValue):
    def __init__(self, nv, *args):
        set.__init__(self, *args)
        JSValue.__init__(self, nv)

    def __del__(self):
        _lib.Node_Dispose_Value(self._nv)


class NativeObject(dict, JSValue):
    def __init__(self, nv, *args, **kwargs):
        dict.__init__(self, *args, **kwargs)
        JSValue.__init__(self, nv)

    def __getattr__(self, name):
        try:
            value = self[name]
            if isinstance(value, dict) and not isinstance(value, NativeObject):
                value = NativeObject(self._nv, value)
                self[name] = value
            return value
        except KeyError:
            raise AttributeError(name)

    def __setattr__(self, name, value):
        if name.startswith("_") or name in self.__dict__:
            super().__setattr__(name, value)
        else:
            self[name] = value

    def __del__(self):
        _lib.Node_Dispose_Value(self._nv)


class NativeDatetime(datetime.datetime, JSValue):
    def __init__(self, nv, *args, **kwargs):
        if len(args) == 1 and isinstance(args[0], datetime.datetime):
            dt = args[0]
            args = (
                dt.year,
                dt.month,
                dt.day,
                dt.hour,
                dt.minute,
                dt.second,
                dt.microsecond,
            )
        elif len(args) == 1 and isinstance(args[0], (int, float)):
            dt = datetime.datetime.fromtimestamp(args[0])
            args = (
                dt.year,
                dt.month,
                dt.day,
                dt.hour,
                dt.minute,
                dt.second,
                dt.microsecond,
            )

        datetime.datetime.__init__(self, *args, **kwargs)
        JSValue.__init__(self, nv)

    def __del__(self):
        _lib.Node_Dispose_Value(self._nv)


class NativePattern(JSValue):
    def __init__(self, nv, *args, **kwargs):
        if len(args) == 1 and isinstance(args[0], re.Pattern):
            self._target = args[0]
        else:
            self._target = re.Pattern(*args, **kwargs)
        super().__init__(nv)

    def __getattribute__(self, name):
        if name == "_target":
            return self._target
        return getattr(self._target, name)

    def __setattr__(self, name, value):
        setattr(self._target, name, value)

    def __delattr__(self, name):
        delattr(self._target, name)

    def __del__(self):
        _lib.Node_Dispose_Value(self._nv)

    def __repr__(self):
        return repr(self._target)

    def __str__(self):
        return str(self._target)

    def __eq__(self, other):
        return self._target == other

    def __hash__(self):
        return hash(self._target)

    def __reduce__(self):
        return self._target.__reduce__()

    def __copy__(self):
        return type(self)(self._target)

    def __deepcopy__(self, memo):
        import copy

        return type(self)(copy.deepcopy(self._target, memo))

    def __class_getitem__(cls, item):
        return re.Pattern[item]


class Func(JSValue):
    def __init__(self, name, node, f):
        super().__init__(f)
        self._node = node
        self.__name__ = name

    def __call__(self, *args, **kwargs):
        L = len(args)
        n_args = (NodeValue * L)()
        for i in range(L):
            n_args[i] = _to_node(self._node, args[i])
        return _to_python(
            self._node,
            _lib.NodeContext_Call_Function(
                self._node._context, self._nv, n_args, len(args)
            ),
        )

    def new(self, *args, **kwargs):
        L = len(args)
        n_args = (NodeValue * L)()
        for i in range(L):
            n_args[i] = _to_node(self._node, args[i])
        return _to_python(
            self._node,
            _lib.NodeContext_Construct_Function(
                self._node._context, self._nv, n_args, len(args)
            ),
        )

    def __str__(self):
        return f"{self.__name__}@Node"

    def __del__(self):
        _lib.Node_Dispose_Value(self._nv)


class JSExternal:
    def __init__(self, ptr):
        self._ptr = ptr

    def __getattr__(self, name):
        try:
            value = self[name]
            if isinstance(value, dict) and not isinstance(value, NativeObject):
                value = NativeObject(self._nv, value)
                self[name] = value
            return value
        except KeyError:
            raise AttributeError(name)

    def __setattr__(self, name, value):
        if name.startswith("_") or name in self.__dict__:
            super().__setattr__(name, value)
        else:
            self[name] = value


class JSSymbol:
    def __init__(self, ptr, description):
        self._ptr = ptr
        self.description = description

    def __str__(self):
        if self.description:
            return f"Symbol({self.description})"
        return f"Symbol"


class JSProxy(JSValue):
    def __init__(self, nv, target, handler):
        super().__init__(nv)
        self._target = target
        self._handler = handler


def _to_node(node, value):  # TODO SYMBOL
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
    elif isinstance(value, datetime.datetime):
        v.type = DATE_T
        v.val_date_unix = value.timestamp()
    elif isinstance(value, JSExternal):
        v.type = EXTERNAL
        v.val_external_ptr = value._ptr
    elif isinstance(value, JSSymbol):
        v.type = SYMBOL
        v.val_external_ptr = value._ptr
        if value.description:
            v.val_string = value.description.encode("utf-8")
    elif isinstance(value, re.Pattern):
        v.type = REGEXP
        v.val_string = value.pattern.encode("utf-8")
        flags = 0
        if value.flags & re.IGNORECASE:
            flags |= 1 << 1  # kIgnoreCase
        if value.flags & re.MULTILINE:
            flags |= 1 << 2  # kMultiline
        if value.flags & re.UNICODE:
            flags |= 1 << 4  # kUnicode
        if value.flags & re.DOTALL:
            flags |= 1 << 5  # kDotAll
        v.val_regex_flags = pflags
    elif isinstance(value, Coroutine):
        v.type = PROMISE
        cid = random_int64()
        v.future_id = cid
        node._tracker.track(value, cid)
    elif isinstance(value, JSExternal):
        v.type = EXTERNAL
        v.val_external_ptr = value._ptr
    elif isinstance(value, datetime.datetime):
        v.type = DATE_T
        v.val_date_unix = value.timestamp()
    elif isinstance(value, BaseException):
        v.type = ERROR_T
        v.error_message = str(value)
        v.error_name = type(value).__name__
        v.error_stack = str(getattr(value, "__traceback__", None))
    elif isinstance(value, (list, tuple, set)):  # Same for set
        v.type = SET if isinstance(value, set) else ARRAY
        val = list(value)
        L = len(value)
        arr = (NodeValue * L)()
        for i in range(L):
            arr[i] = _to_node(node, val[i])
        v.val_array_len = L
        v.val_array = arr
    elif isinstance(value, array.array):  # Same for set
        v.type = TYPED_ARRAY
        items = list(value)
        L = len(value)

        kind = INT8_T
        c_kind = ctypes.c_int8
        if value.typecode == "B":
            kind = UINT8_T
            c_kind = ctypes.c_uint8
        elif value.typecode == "h":
            kind = INT16_T
            c_kind = ctypes.c_int16
        elif value.typecode == "H":
            kind = UINT16_T
            c_kind = ctypes.c_uint16
        elif value.typecode == "i":
            kind = INT32_T
            c_kind = ctypes.c_int32
        elif value.typecode == "I":
            kind = UINT32_T
            c_kind = ctypes.c_uint32
        elif value.typecode == "l":
            kind = BINT64_T
            c_kind = ctypes.c_int64
        elif value.typecode == "L":
            kind = BUINT64_T
            c_kind = ctypes.c_uint64
        elif value.typecode == "f":
            kind = FLOAT32_T
            c_kind = ctypes.c_float
        elif value.typecode == "d":
            kind = FLOAT64_T
            c_kind = ctypes.c_double

        arr = (c_kind * L)()
        for i in range(L):
            arr[i] = c_kind(val[i])
        v.val_array_len = L
        v.val_tarray_type = kind
        v.val_tarray = arr
    elif isinstance(value, dict):
        keys = list(value.keys())
        is_map = any(not isinstance(x, str) for x in keys)
        v.type = MAP if is_map else OBJECT
        L = len(value)
        values = (NodeValue * L)()
        for i in range(L):
            values[i] = _to_node(node, value[keys[i]])
        v.object_len = L
        v.object_values = values
        if is_map:
            n_keys = (NodeValue * L)()
            for i in range(L):
                n_keys[i] = _to_node(node, keys[i])
            v.map_keys = n_keys
        else:
            keys = [key.encode("utf-8") for key in keys]
            n_keys = (ctypes.c_char_p * L)(*keys)
            v.object_keys = n_keys
    elif callable(value):
        name_enc = value.__name__.encode("utf-8")
        fun = node._create_function(value)
        v.type = FUNCTION
        v.val_string = name_enc
        v.function = fun.function
    else:
        v.type = STRING
        v.val_string = value.__str__().encode("utf-8")
    return v


def _to_python(node, value: NodeValue):  # TODO SYMBOL
    if value.type == BOOLEAN_T:
        return bool(value.val_bool)
    elif value.type == NUMBER:
        return value.val_num
    elif value.type == STRING:
        s = value.val_string.decode("utf-8")
        _lib.Node_Dispose_Value(value)
        return s
    elif value.type == FUNCTION:
        return Func(value.val_string.decode("utf-8"), node, value)
    elif value.type == SET:
        arr = NativeSet(value)
        L = value.val_array_len
        for i in range(L):
            arr.add(_to_python(node, value.val_array[i]))
        return arr
    elif value.type == ARRAY:
        arr = NativeArray(value)
        L = value.val_array_len
        for i in range(L):
            arr.append(_to_python(node, value.val_array[i]))
        return arr
    elif value.type == TYPED_ARRAY:
        kind = "b"
        c_kind = ctypes.c_int8
        if value.val_tarray_type == UINT8_T:
            kind = "B"
            c_kind = ctypes.c_uint8
        elif value.val_tarray_type == INT16_T:
            kind = "h"
            c_kind = ctypes.c_int16
        elif value.val_tarray_type == UINT16_T:
            kind = "H"
            c_kind = ctypes.c_uint16
        elif value.val_tarray_type == INT32_T:
            kind = "i"
            c_kind = ctypes.c_int32
        elif value.val_tarray_type == UINT32_T:
            kind = "I"
            c_kind = ctypes.c_uint32
        elif value.val_tarray_type == BINT64_T:
            kind = "l"
            c_kind = ctypes.c_int64
        elif value.val_tarray_type == BUINT64_T:
            kind = "L"
            c_kind = ctypes.c_uint64
        elif value.val_tarray_type == FLOAT32_T:
            kind = "f"
            c_kind = ctypes.c_float
        elif value.val_tarray_type == FLOAT64_T:
            kind = "d"
            c_kind = ctypes.c_double
        ptr = ctypes.cast(value.val_tarray, ctypes.POINTER(c_kind))
        elements = []
        for i in range(value.val_array_len):
            elements.append(ptr[i])
        return array.array(kind, elements)
    elif value.type == BIGINT:
        i = int(value.val_big.decode("utf-8"))
        _lib.Node_Dispose_Value(value)
        return i
    elif value.type == OBJECT:
        obj = NativeObject(value)
        L = value.object_len
        for i in range(L):
            obj[value.object_keys[i].decode("utf-8")] = _to_python(
                node, value.object_values[i]
            )
        return obj
    elif value.type == MAP:
        obj = NativeObject(value)
        L = value.object_len
        for i in range(L):
            obj[_to_python(node, value.object_keys[i])] = _to_python(
                node, value.object_values[i]
            )
        return obj
    elif value.type == DATE_T:
        return NativeDatetime(node, value.val_date_unix)
    elif value.type == EXTERNAL:
        return NativeSymbol(value.val_external_ptr)
    elif value.type == SYMBOL:
        if value.val_string != 0:
            return JSSymbol(value.val_external_ptr, value.val_string.decode("utf-8"))
        return JSSymbol(value.val_external_ptr)
    elif value.type == REGEXP:
        flags = 0
        if value.val_regex_flags & (1 << 1):  # kIgnoreCase
            flags |= re.IGNORECASE
        if value.val_regex_flags & (1 << 2):  # kMultiline
            flags |= re.MULTILINE
        if value.val_regex_flags & (1 << 4):  # kUnicode (has no effect in Python 3)
            flags |= re.UNICODE
        if value.val_regex_flags & (1 << 5):  # kDotAll
            flags |= re.DOTALL
        return NativePattern(node, re.compile(value.val_string, flags))
    elif value.type == PROXY:
        return JSProxy(
            node,
            _to_python(node, value.val_proxy_target),
            _to_python(node, value.val_proxy_handler),
        )
    elif value.type == PROMISE:
        promise = JSPromise()
        node._promises[value.future_id] = promise
        return promise
    return None


class CoroutineTracker:
    def __init__(self, listener):
        self._listener = listener

    def track(self, coro, data):
        return asyncio.create_task(self._wrap(coro, data))

    async def _wrap(self, coro, data):
        await self._emit("start", data)
        try:
            result = await coro
            await self._emit("complete", data, result)
            return result
        except Exception as e:
            await self._emit("error", data, e)
            raise

    async def _emit(self, event, *args):
        if self._listener:
            (
                await self._listener(event, *args)
                if asyncio.iscoroutinefunction(self._listener)
                else self._listener(event, *args)
            )


class JSPromise:
    def __init__(self):
        self._loop = asyncio.get_event_loop()
        self._future = self._loop.create_future()
        self._waiters = []

    def __await__(self):
        async def waiter():
            result = await self._future
            return result

        return waiter().__await__()

    def resolve(self, value):
        if not self._future.done():
            self._future.set_result(value)


class Node:
    def __init__(self, path=__file__, thread_pool_size=1):
        self.cleaned = False
        self._context = _lib.NodeContext_Create()
        self._python_funcs = {}
        self._promises = {}

        argc = 1
        argv = (ctypes.c_char_p * argc)(path.encode("utf-8"))

        def _trackcb(action, *args):
            if action == "complete":
                _lib.NodeContext_FutureUpdate(
                    self._context, args[0], _to_node(self, args[1]), False
                )
            elif action == "error":
                _lib.NodeContext_FutureUpdate(
                    self._context, args[0], _to_node(self, args[1]), True
                )

        self._tracker = CoroutineTracker(_trackcb)

        def _callback(function_name, values_ptr, length):
            function_name = re.sub(
                r"([a-zA-Z_\$][\w\$]*)\s*\(\s*\)\s*;*",
                r"\1",
                function_name.decode("utf-8").strip(),
            )
            if function_name in self._python_funcs:
                args = [None] * length
                for i in range(length):
                    args[i] = _to_python(self, values_ptr[i])
                res = self._python_funcs[function_name](*args)
                if res:
                    return _to_node(self, res)
                return 0
            else:
                raise Exception(f"Function not found. {function_name}")

        self._callback = CALLBACK(_callback)

        _lib.NodeContext_SetCallback(self._context, self._callback)

        def _future_callback(i, result, reject):
            if i in self._promises:
                self._promises[i].resolve(result)

        self._future_callback = FUTURE_CALLBACK(_future_callback)

        _lib.NodeContext_SetFutureCallback(self._context, self._future_callback)

        error = _lib.NodeContext_Setup(self._context, 1, argv)
        if not error == 0:
            raise Exception("Failed to setup node.")
        ImportsArrayType = ctypes.c_char_p * 0
        c_array = ctypes.cast(ImportsArrayType(*[]), ctypes.POINTER(ctypes.c_char_p))

        error = _lib.NodeContext_Init(self._context, c_array, 0, thread_pool_size)
        if not error == 0:
            raise Exception("Failed to init node.")
        
    def _create_function(self, func):
        if func in self._registered_functions or func.__name__ in self._python_funcs:
            self._python_funcs[func.__name__] = func
            return self._registered_functions[func]
        self._python_funcs[func.__name__] = func
        node_func = _lib.NodeContext_Create_Function(self._context, func.__name__.encode("utf-8"))
        self._registered_functions[func] = node_func
        return node_func

    def require(self, module: str):
        js_mod = self.eval(
            f"(() => {{ try {{ return require('{module}'); }} catch {{}} }})()"
        )
        if not js_mod:
            raise Exception(f"Failed to import module {module}")
        mod = types.ModuleType(module)
        for key in js_mod:
            setattr(mod, key, js_mod[key])
        return mod

    def define(self, vars: Union[dict, str], value: Any = None) -> None:
        if isinstance(vars, dict):
            keys = (ctypes.c_char_p * len(vars))()
            vals = (NodeValue * len(vars))()
            for i, k in enumerate(vars):
                keys[i] = k.encode("utf-8")
                vals[i] = _to_node(self, vars[k])
            _lib.NodeContext_Define_Global(self._context, keys, vals, len(vars))
        else:
            self.define({vars: value})

    def eval(self, code: str):
        return _to_python(
            self,
            _lib.NodeContext_Run_Script(
                self._context,
                code.encode("utf-8"),
            ),
        )

    def run(self, fp: Union[str, Path]):
        if isinstance(fp, str):
            fp = Path(fp)
        return eval(Path(fp).read_text("utf-8"))

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


_context = Node()


def NodeRegister(func):
    """
    Registers a function in the node context with the same name.
    """
    global _context
    if not isinstance(func, types.FunctionType):
        raise TypeError("Cannot register non-function")
    _context._create_function(func)
    return func

def require(module: str):
    """
    Requires a module in the node context and returns the module.
    Note that the module must be available in the node context.

    Args:
        module (str): The name of the module to require.

    Returns:
        The module object from the node context.
    """
    global _context
    _context.require(module)

def define(vars: Union[dict, str], value: Any = None) -> None:
    """
    Defines a global variable in the node context. If the variable is a string,
    it must be the name of the variable to define. If the variable is a
    dictionary, it must contain a mapping of variable names to values to assign
    to those variables. The value must be a Python object that can be
    converted to a node value.

    Args:
        vars (Union[dict, str]): A string or dictionary containing the
            variable(s) to define.
        value (Any, optional): The value to assign to the variable, if
            `vars` is a string. Defaults to None.

    Returns:
        None
    """
    global _context
    _context.define(vars, value)

def node_eval(code: str):
    """
    Evaluates the provided JavaScript code within the node context and returns
    the result. The code is executed as if it were run in a JavaScript
    environment, allowing for interaction with defined global variables and
    functions.

    Args:
        code (str): The JavaScript code to evaluate.

    Returns:
        Any: The result of the evaluated code, converted to a Python equivalent.
    """

    global _context
    return _context.eval(code)

def js_eval(code: str):
    """
    Evaluates the provided JavaScript code within the node context and returns
    the result. The code is executed as if it were run in a JavaScript
    environment, allowing for interaction with defined global variables and
    functions.

    Same as node_eval.

    Args:
        code (str): The JavaScript code to evaluate.

    Returns:
        Any: The result of the evaluated code, converted to a Python equivalent.
    """

    global _context
    return _context.eval(code)

def node_run(fp: Union[str, Path]):
    """
    Runs the provided JavaScript file in the node context and returns the result.
    The file is executed as if it were run in a JavaScript environment, allowing
    for interaction with defined global variables and functions.

    Args:
        fp (Union[str, Path]): The path to the JavaScript file to run.

    Returns:
        Any: The result of the evaluated file, converted to a Python equivalent.
    """
    global _context
    return _context.run(fp)

def node_dispose():
    """
    Disposes of the node context. This stops the event loop and cleans up
    any remaining resources.
    """
    global _context
    _context.dispose()

def node_stop():
    """
    Stops the node context event loop. This will allow the Python thread to exit
    and will prevent any new callbacks from being scheduled. However, this will
    not stop any currently running callbacks from completing, so the event loop
    may not be stopped immediately.

    Returns:
        None
    """
    global _context
    _context.stop()
