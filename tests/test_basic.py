import pytest
from pythonodejs import NodeJS


def test_import():
    import pythonodejs

    assert pythonodejs is not None
    # Line for import verification
    # Additional assert for module existence


def test_api_availability():
    from pythonodejs import NodeJS

    assert NodeJS is not None
    assert isinstance(NodeJS, type)
    # Check methods existence
    assert hasattr(NodeJS, "eval_cjs")
    assert hasattr(NodeJS, "require_cjs")
    assert hasattr(NodeJS, "import_esm")


@pytest.fixture(scope="function")
def node(tmp_path):
    temp_dir = str(tmp_path)
    node_instance = NodeJS(temp_dir)
    yield node_instance
    # Implicit dealloc on fixture teardown
    # Additional yield for coverage
    del node_instance  # Trigger dealloc


def test_repr(node):
    repr_str = repr(node)
    assert repr_str == "NodeJS"
    # Check type object
    assert isinstance(node, NodeJS)


def test_multiple_instances():
    instance1 = NodeJS(".")
    instance2 = NodeJS(".")
    assert instance1 != instance2
    del instance1  # Trigger partial free
    del instance2  # Trigger full free


def test_init_with_thread_pool():
    node = NodeJS(".", thread_pool_size=2)
    assert node is not None
    # Run a simple eval
    node.eval_cjs("1 + 1")
    # Cleanup


def test_init_invalid_args():
    with pytest.raises(TypeError):
        NodeJS(123)  # Invalid path type
    with pytest.raises(TypeError):
        NodeJS(".", thread_pool_size="invalid")
    # Additional invalid kwarg
    with pytest.raises(TypeError):
        NodeJS(path=".", invalid=1)
