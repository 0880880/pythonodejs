from pythonodejs import NodeJS
import pytest


@pytest.fixture
def node(request) -> NodeJS:
    project_root = request.config.rootpath / "test.js"
    return NodeJS(str(project_root))


def test_js_func_to_py(node):
    js_func = node.eval_cjs("() => 42")
    assert callable(js_func)
    result = js_func()
    assert result == 42
    js_func_with_args = node.eval_cjs("(a, b) => a + b")
    sum_res = js_func_with_args(1, 2)
    assert sum_res == 3


def test_py_func_to_js(node):
    def py_add(a, b):
        return a + b

    js_call_py = node.eval_cjs("(f) => f(3, 4)")
    result = js_call_py(py_add)
    assert result == 7

    def py_no_args():
        return "hello"

    js_call_no_args = node.eval_cjs("(f) => f()")
    assert js_call_no_args(py_no_args) == "hello"

    def py_with_list(l):
        return l[0]

    js_call_list = node.eval_cjs("(f) => f([5,6])")
    assert js_call_list(py_with_list) == 5


def test_func_cleanup(node):
    js_func = node.eval_cjs("() => {}")
    del js_func  # Trigger cleanup
    # Multiple creations
    for _ in range(3):
        temp_func = node.eval_cjs("() => {}")
        del temp_func
    # Check no crash
