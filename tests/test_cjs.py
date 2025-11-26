from pythonodejs import NodeJS
from pathlib import Path
import pytest
import os


@pytest.fixture
def node(request) -> NodeJS:
    project_root = request.config.rootpath / "test.js"
    return NodeJS(str(project_root))


def test_types_js_to_py(node):
    int_v = node.eval_cjs("123")
    assert isinstance(int_v, int)
    assert int_v == 123
    float_v = node.eval_cjs("3.1415")
    assert isinstance(float_v, float)
    assert float_v == 3.1415
    str_v = node.eval_cjs("'Hello'")
    assert isinstance(str_v, str)
    assert str_v == "Hello"
    list_v = node.eval_cjs("['abc', 123, 'def']")
    assert isinstance(list_v, list)
    assert list_v == ["abc", 123, "def"]
    dict_v = node.eval_cjs('{"a": 1, "b": 2}')
    assert isinstance(dict_v, dict)
    assert dict_v == {"a": 1, "b": 2}
    bool_true = node.eval_cjs("true")
    assert bool_true is True
    bool_false = node.eval_cjs("false")
    assert bool_false is False
    none_v = node.eval_cjs("null")
    assert none_v is None
    undef_v = node.eval_cjs("undefined")
    assert undef_v is None


def test_bigint_js_to_py(node):
    bigint_v = node.eval_cjs("BigInt('12345678901234567890')")
    assert isinstance(bigint_v, int)
    assert bigint_v == 12345678901234567890
    negative_bigint = node.eval_cjs("BigInt('-12345678901234567890')")
    assert negative_bigint == -12345678901234567890
    zero_bigint = node.eval_cjs("BigInt(0)")
    assert zero_bigint == 0


def test_set_js_to_py(node):
    set_v = node.eval_cjs("new Set([1, 2, 3])")
    assert isinstance(set_v, list)
    assert set_v == [1, 2, 3]  # Order may vary but for coverage
    empty_set = node.eval_cjs("new Set()")
    assert empty_set == []
    str_set = node.eval_cjs("new Set(['a', 'b'])")
    assert sorted(str_set) == ["a", "b"]


def test_object_js_to_py(node):
    obj_v = node.eval_cjs("({x: 1, y: 'test'})")
    assert isinstance(obj_v, dict)
    assert obj_v == {"x": 1, "y": "test"}
    nested_obj = node.eval_cjs("({a: {b: 2}})")
    assert nested_obj["a"] == {"b": 2}
    func_in_obj = node.eval_cjs("({func: () => 42})")
    assert callable(func_in_obj["func"])


def test_number_object_js_to_py(node):
    num_obj = node.eval_cjs("new Number(42.5)")
    assert isinstance(num_obj, float)
    assert num_obj == 42.5
    int_num_obj = node.eval_cjs("new Number(100)")
    assert int_num_obj == 100.0
    zero_num_obj = node.eval_cjs("new Number(0)")
    assert zero_num_obj == 0.0


def test_string_object_js_to_py(node):
    str_obj = node.eval_cjs("new String('hello')")
    assert isinstance(str_obj, str)
    assert str_obj == "hello"
    empty_str_obj = node.eval_cjs("new String('')")
    assert empty_str_obj == ""
    unicode_str_obj = node.eval_cjs("new String(' café')")
    assert unicode_str_obj == " café"


def test_require_cjs(node, tmp_path):
    module_path = tmp_path / "test_module.js"
    module_path.write_text("module.exports = { value: 42 };")
    required = node.require_cjs(str(module_path))
    assert isinstance(required, dict)
    assert required["value"] == 42
    # Require built-in
    fs = node.require_cjs("fs")
    assert callable(fs["readFileSync"])
    # Require invalid
    with pytest.raises(Exception):
        node.require_cjs("nonexistent")
