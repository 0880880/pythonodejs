from pythonodejs import NodeJS
import pytest
import asyncio


@pytest.fixture
def node(request) -> NodeJS:
    project_root = request.config.rootpath / "test.js"
    return NodeJS(str(project_root))


def test_types_py_to_js(node):
    # Pass Py types to JS and echo back
    echo_code = "(x) => x"
    echo_func = node.eval_cjs(echo_code)
    none_res = echo_func(None)
    assert none_res is None
    bool_true = echo_func(True)
    assert bool_true is True
    bool_false = echo_func(False)
    assert bool_false is False


def test_int_py_to_js(node):
    echo_func = node.eval_cjs("(x) => x")
    small_int = echo_func(123)
    assert small_int == 123
    large_int = echo_func(2**63 - 1)
    assert large_int == 2**63 - 1
    negative_int = echo_func(-123)
    assert negative_int == -123


def test_bigint_py_to_js(node):
    echo_func = node.eval_cjs("(x) => x")
    big_int = echo_func(12345678901234567890)
    assert big_int == 12345678901234567890
    neg_big = echo_func(-12345678901234567890)
    assert neg_big == -12345678901234567890
    zero_big = echo_func(0)
    assert zero_big == 0


def test_float_py_to_js(node):
    echo_func = node.eval_cjs("(x) => x")
    float_v = echo_func(3.1415)
    assert float_v == 3.1415
    inf_v = echo_func(float("inf"))
    assert str(inf_v) == "inf"  # JS Infinity to Py float
    nan_v = echo_func(float("nan"))
    assert str(nan_v) == "nan"


def test_str_py_to_js(node):
    echo_func = node.eval_cjs("(x) => x")
    str_v = echo_func("hello")
    assert str_v == "hello"
    unicode_v = echo_func("café")
    assert unicode_v == "café"
    empty_str = echo_func("")
    assert empty_str == ""


def test_list_tuple_py_to_js(node):
    echo_func = node.eval_cjs("(x) => x")
    list_v = echo_func([1, "a", True])
    assert list_v == [1, "a", True]
    tuple_v = echo_func((2, "b", False))
    assert tuple_v == [2, "b", False]  # Becomes list
    nested_list = echo_func([[1], [2]])
    assert nested_list == [[1], [2]]


def test_dict_py_to_js(node):
    echo_func = node.eval_cjs("(x) => x")
    dict_v = echo_func({"a": 1, "b": "test"})
    assert dict_v == {"a": 1, "b": "test"}
    nested_dict = echo_func({"x": {"y": 2}})
    assert nested_dict == {"x": {"y": 2}}
    empty_dict = echo_func({})
    assert empty_dict == {}


def test_object_py_to_js(node):
    class TestObj:
        def __init__(self):
            self.a = 1
            self.b = "test"

    echo_func = node.eval_cjs("(x) => x")
    obj_v = echo_func(TestObj())
    assert obj_v == {"a": 1, "b": "test"}

    class Empty:
        pass

    no_dict_v = echo_func(Empty())
    assert no_dict_v is {}
    with pytest.raises(RuntimeError):
        echo_func(object())  # No __dict__
