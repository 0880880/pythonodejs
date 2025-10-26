try:
    from ._version import version as __version__  # type: ignore
except ImportError:
    __version__ = "unknown"

__all__ = ["__version__"]
