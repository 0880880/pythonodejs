from ._pythonodejs import NodeJS as _NodeJS
from typing import Optional
from pathlib import Path
import sys


class NodeJS:
    def __init__(self, path: Optional[str] = None, thread_pool_size: int = 4):
        if not path:
            p = Path(sys.argv[0])
            if not p.exists():
                raise RuntimeError(f"File {p} does not exist")
            path = p.resolve() + ".js"
        else:
            p = Path(path)
            if p.is_dir():
                path = (p / "main.js").resolve()
            else:
                path = p.resolve()
        self._node = _NodeJS(path, thread_pool_size)

    def __repr__(self):
        return f"<NodeJS at {hex(id(self))}>"

    def eval_cjs(self, code: str) -> any:
        return self._node.eval_cjs(code)

    def require_cjs(self, url: str) -> any:
        return self._node.require_cjs(url)

    async def import_esm(self, url: str) -> any:
        return self._node.import_esm(url)
