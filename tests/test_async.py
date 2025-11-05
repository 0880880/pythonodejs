from pythonodejs import NodeJS
import asyncio
import pytest


@pytest.fixture
def node(request) -> NodeJS:
    project_root = request.config.rootpath / "test.js"
    return NodeJS(str(project_root))


async def async_add(a, b):
    await asyncio.sleep(0.01)
    return a + b


def test_coro_py_to_js(node):
    js_call_coro = node.eval_cjs("(coro) => coro.then(res => res)")
    with pytest.raises(NotImplementedError):  # Since promise out not impl
        js_call_coro(async_add(1, 2))
    # Coverage for setup
    # Additional call
    coro = async_add(3, 4)
    node.eval_cjs("(x) => x")(coro)  # Trigger PyToJS coro branch
    # Poll to process
    node.eval_cjs("setImmediate(() => {})")


def test_promise_js_to_py(node):
    promise_v = node.eval_cjs("Promise.resolve(42)")
    assert promise_v is NotImplemented
    reject_p = node.eval_cjs("Promise.reject('error')")
    assert reject_p is NotImplemented
    pending_p = node.eval_cjs("new Promise(() => {})")
    assert pending_p is NotImplemented
