from pythonodejs import NodeJS
import pytest
import os
from pathlib import Path


@pytest.fixture
def node(request) -> NodeJS:
    project_root = Path(request.config.rootpath)
    return NodeJS(str(project_root))


def test_import_esm_basic(node, tmp_path):
    module_path = tmp_path / "test_esm.mjs"
    module_path.write_text("export const value = 42;")
    with pytest.raises(NotImplementedError):  # Since promise not implemented
        node.import_esm(str(module_path))
    # Coverage for call
    # Additional for path string check
    with pytest.raises(TypeError):
        node.import_esm(123)


def test_import_esm_promise(node):
    # Since returns promise, and JSToPy returns NOTIMPLEMENTED
    result = node.import_esm("some/module")
    assert result is NotImplemented  # Assuming that's what it returns
    # Poll to trigger event loop
    # But since not exposed, indirect via multiple calls
    node.eval_cjs("setTimeout(() => {}, 10)")
    node.eval_cjs("1")  # To trigger poll indirectly if any
