from ._pythonodejs import NodeJS
from ._event_loop import event_loop_driver

try:
    from ._version import version as __version__  # type: ignore
except ImportError:
    __version__ = "unknown"

__all__ = ["__version__", "NodeJS", "event_loop_driver"]
