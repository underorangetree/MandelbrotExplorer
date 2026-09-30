"""Python binding for the MandelbrotExplorer C ABI.

The binding is a thin, dependency-light wrapper around the shared library
(``mandelbrot.dll`` / ``libmandelbrot.so`` / ``libmandelbrot.dylib``) and only
needs numpy for the array helpers (``render``/``render_into``). Rendering is
zero-copy: the library writes straight into numpy buffers, and the returned
arrays are views cropped to the requested width.

Typical use::

    import mandelbrot

    with mandelbrot.Mandelbrot(1280, 720, max_iterations=1000) as m:
        m.set_view(4.0, -0.743643887037158704752191506114774,
                   0.131825904205311970493132056385139)
        colors, iterations = m.render(colors=True, iterations=True)

The library is located through ``load_library(path)``, the
``MANDELBROT_LIBRARY`` environment variable, the package directory, the
repository build tree or the system search path, in that order. Video export
(``write_video``) uses the optional OpenCV-based ``mandelbrot_video`` library,
located the same way through ``load_video_library``/``MANDELBROT_VIDEO_LIBRARY``.
"""

from __future__ import annotations

import ctypes
import os
import sys
from ctypes import POINTER, c_char_p, c_double, c_int, c_int32, c_size_t, c_ubyte, c_void_p
from pathlib import Path

try:  # numpy is optional; only the array helpers need it
    import numpy as _np
except ImportError:  # pragma: no cover - exercised on minimal installs
    _np = None

__all__ = [
    "Mandelbrot",
    "MandelbrotError",
    "VideoError",
    "load_library",
    "load_video_library",
    "render_frame",
    "render_image",
    "version",
    "write_video",
]

MB_OK = 0
MB_INVALID_ARGUMENT = 1
MB_INTERNAL_ERROR = 2
MB_CANCELLED = 3
MB_VIDEO_ERROR = 4

_STATUS_NAMES = {
    MB_OK: "MB_OK",
    MB_INVALID_ARGUMENT: "MB_INVALID_ARGUMENT",
    MB_INTERNAL_ERROR: "MB_INTERNAL_ERROR",
    MB_CANCELLED: "MB_CANCELLED",
    MB_VIDEO_ERROR: "MB_VIDEO_ERROR",
}

_PIXEL_ORDERS = {"bgr": 0, "rgb": 1}
_IMAGE_FORMATS = {"bmp": 0, "pnm": 1, "ppm": 1}

_CORE_LIBRARY_NAMES = ("mandelbrot.dll", "libmandelbrot.so", "libmandelbrot.dylib")
_CORE_PATTERNS = ("build/*/Release/mandelbrot.dll", "build/*/mandelbrot.dll",
                  "build/*/libmandelbrot.so*", "build/libmandelbrot.so*",
                  "build/*/libmandelbrot.dylib")
_VIDEO_LIBRARY_NAMES = ("mandelbrot_video.dll", "libmandelbrot_video.so", "libmandelbrot_video.dylib")
_VIDEO_PATTERNS = ("build/*/Release/mandelbrot_video.dll", "build/*/mandelbrot_video.dll",
                   "build/*/libmandelbrot_video.so*", "build/libmandelbrot_video.so*",
                   "build/*/libmandelbrot_video.dylib")


class MandelbrotError(RuntimeError):
    """Raised when the C API reports a failure."""

    def __init__(self, status: int, message: str) -> None:
        self.status = status
        self.message = message
        super().__init__(f"{_STATUS_NAMES.get(status, status)}: {message}")


class _RenderParams(ctypes.Structure):
    _fields_ = [
        ("struct_size", c_size_t),
        ("width", c_int),
        ("height", c_int),
        ("max_iterations", c_int),
        ("threads", c_int),
        ("zoom", c_double),
        ("center_x", c_double),
        ("center_y", c_double),
        ("kernel", c_char_p),
        ("pixel_order", c_int),
    ]


_library: ctypes.CDLL | None = None
_library_path: str | None = None


def _search_candidates(explicit: str | None, env_var: str,
                       names: tuple[str, ...], patterns: tuple[str, ...]) -> list[str]:
    candidates: list[str] = []
    if explicit:
        candidates.append(explicit)
    from_env = os.environ.get(env_var)
    if from_env:
        candidates.append(from_env)
    package_dir = Path(__file__).resolve().parent
    candidates.extend(str(package_dir / name) for name in names)
    # Repository build tree (handy when running from a checkout).
    repo = package_dir.parents[2]
    for pattern in patterns:
        candidates.extend(str(path) for path in sorted(repo.glob(pattern)))
    candidates.extend(names)
    return candidates


def load_library(path: str | None = None) -> str:
    """Loads the shared library (once) and returns its resolved path.

    Pass ``path`` to override the search; otherwise the ``MANDELBROT_LIBRARY``
    environment variable, the package directory, the repository build tree and
    the system search path are tried in order.
    """
    global _library, _library_path
    if _library is not None:
        if path is None or Path(path).resolve() == Path(_library_path or "").resolve():
            return _library_path or ""
    candidates = _search_candidates(path, "MANDELBROT_LIBRARY", _CORE_LIBRARY_NAMES, _CORE_PATTERNS)
    errors: list[str] = []
    for candidate in candidates:
        if candidate in _CORE_LIBRARY_NAMES or Path(candidate).exists():
            try:
                library = ctypes.CDLL(candidate)
            except OSError as error:  # pragma: no cover - platform dependent
                errors.append(f"{candidate}: {error}")
                continue
            _configure(library)
            _library = library
            _library_path = candidate
            return candidate
    raise FileNotFoundError(
        "could not locate the mandelbrot shared library; build the `mandelbrot` "
        "target or set MANDELBROT_LIBRARY. Tried: " + ", ".join(candidates)
        + (("; errors: " + "; ".join(errors)) if errors else "")
    )


def _configure(library: ctypes.CDLL) -> None:
    library.mb_version.restype = c_char_p
    library.mb_last_error.restype = c_char_p
    library.mb_create.restype = c_void_p
    library.mb_create.argtypes = [c_int, c_int, c_int, c_int]
    library.mb_destroy.argtypes = [c_void_p]
    library.mb_width.argtypes = [c_void_p]
    library.mb_height.argtypes = [c_void_p]
    library.mb_storage_width.argtypes = [c_void_p]
    library.mb_thread_count.argtypes = [c_void_p]
    library.mb_kernel_name.argtypes = [c_void_p]
    library.mb_kernel_name.restype = c_char_p
    library.mb_set_view.argtypes = [c_void_p, c_double, c_double, c_double]
    library.mb_set_kernel.argtypes = [c_void_p, c_char_p]
    library.mb_set_pixel_order.argtypes = [c_void_p, c_int]
    library.mb_render.argtypes = [c_void_p]
    library.mb_render_into.argtypes = [c_void_p, POINTER(c_ubyte), c_size_t]
    library.mb_render_outputs.argtypes = [c_void_p, POINTER(c_ubyte), c_size_t,
                                          POINTER(c_int32), c_size_t]
    library.mb_write_image.argtypes = [c_void_p, c_char_p, c_int]
    library.mb_render_frame.argtypes = [POINTER(_RenderParams), POINTER(c_ubyte), c_size_t,
                                        POINTER(c_int32), c_size_t]
    library.mb_render_image.argtypes = [POINTER(_RenderParams), c_char_p, c_int]
    library.mb_cancel.argtypes = [c_void_p]


def _lib() -> ctypes.CDLL:
    if _library is None:
        load_library()
    assert _library is not None
    return _library


def _error_message() -> str:
    message = _lib().mb_last_error()
    return message.decode("utf-8", "replace") if message else "unknown error"


def _check(status: int) -> None:
    if status != MB_OK:
        raise MandelbrotError(status, _error_message())


# -- optional video library --------------------------------------------------

class VideoError(MandelbrotError):
    """Raised when no usable video encoder is available."""


class _VideoParams(ctypes.Structure):
    _fields_ = [
        ("struct_size", c_size_t),
        ("width", c_int),
        ("height", c_int),
        ("max_iterations", c_int),
        ("threads", c_int),
        ("fps", c_double),
        ("duration_seconds", c_double),
        ("kernel", c_char_p),
        ("pixel_order", c_int),
        ("codec", c_char_p),
        ("quality", c_int),
    ]


_video_library: ctypes.CDLL | None = None
_video_library_path: str | None = None


def load_video_library(path: str | None = None) -> str:
    """Loads the optional OpenCV-based ``mandelbrot_video`` shared library.

    Needed by :func:`write_video`; searches the same places as
    :func:`load_library`, with ``MANDELBROT_VIDEO_LIBRARY`` as the override.
    Raises :class:`FileNotFoundError` when the library is not built.
    """
    global _video_library, _video_library_path
    if _video_library is not None:
        if path is None or Path(path).resolve() == Path(_video_library_path or "").resolve():
            return _video_library_path or ""
    candidates = _search_candidates(path, "MANDELBROT_VIDEO_LIBRARY",
                                    _VIDEO_LIBRARY_NAMES, _VIDEO_PATTERNS)
    errors: list[str] = []
    for candidate in candidates:
        if candidate in _VIDEO_LIBRARY_NAMES or Path(candidate).exists():
            try:
                library = ctypes.CDLL(candidate)
            except OSError as error:  # pragma: no cover - platform dependent
                errors.append(f"{candidate}: {error}")
                continue
            library.mb_write_video.restype = c_int
            library.mb_write_video.argtypes = [POINTER(_VideoParams), c_char_p]
            library.mb_video_last_error.restype = c_char_p
            _video_library = library
            _video_library_path = candidate
            return candidate
    raise FileNotFoundError(
        "could not locate the mandelbrot_video shared library; build the "
        "`mandelbrot_video` target or set MANDELBROT_VIDEO_LIBRARY. Tried: "
        + ", ".join(candidates)
        + (("; errors: " + "; ".join(errors)) if errors else "")
    )


def _lib_video() -> ctypes.CDLL:
    if _video_library is None:
        load_video_library()
    assert _video_library is not None
    return _video_library


def _video_error_message() -> str:
    message = _lib_video().mb_video_last_error()
    return message.decode("utf-8", "replace") if message else "unknown error"


def _check_video(status: int) -> None:
    if status == MB_OK:
        return
    if status == MB_VIDEO_ERROR:
        raise VideoError(status, _video_error_message())
    raise MandelbrotError(status, _video_error_message())


def _pixel_order_value(name: str) -> int:
    try:
        return _PIXEL_ORDERS[name.lower()]
    except KeyError:
        raise ValueError(f"pixel_order must be one of {sorted(_PIXEL_ORDERS)}, got {name!r}") from None


def _image_format_value(name: str) -> int:
    try:
        return _IMAGE_FORMATS[name.lower()]
    except KeyError:
        raise ValueError(f"format must be one of {sorted(_IMAGE_FORMATS)}, got {name!r}") from None


def version() -> str:
    """Version of the loaded shared library, e.g. ``"0.5.0"``."""
    return _lib().mb_version().decode()


def _require_numpy() -> None:
    if _np is None:
        raise ImportError("numpy is required for the array helpers; install numpy or use render_frame with your own buffers")


class Mandelbrot:
    """A render context owning its own worker pool.

    Contexts are not thread safe: use one context per thread. ``with`` blocks
    close the context.
    """

    def __init__(self, width: int, height: int, max_iterations: int = 2000,
                 threads: int = 0, kernel: str | None = None,
                 pixel_order: str = "bgr", library: str | None = None) -> None:
        if library is not None:
            load_library(library)
        self._lib = _lib()
        self._handle = self._lib.mb_create(width, height, max_iterations, threads)
        if not self._handle:
            raise MandelbrotError(MB_INVALID_ARGUMENT, _error_message())
        try:
            self.set_pixel_order(pixel_order)
            if kernel is not None:
                self.set_kernel(kernel)
        except Exception:
            self.close()
            raise

    # -- lifecycle ---------------------------------------------------------
    def close(self) -> None:
        if self._handle:
            self._lib.mb_destroy(self._handle)
            self._handle = None

    def __enter__(self) -> "Mandelbrot":
        return self

    def __exit__(self, *_: object) -> None:
        self.close()

    def __del__(self) -> None:  # pragma: no cover - defensive
        try:
            self.close()
        except Exception:
            pass

    # -- properties --------------------------------------------------------
    @property
    def width(self) -> int:
        return self._lib.mb_width(self._handle)

    @property
    def height(self) -> int:
        return self._lib.mb_height(self._handle)

    @property
    def storage_width(self) -> int:
        """Padded row width (the kernels render whole 32-column blocks)."""
        return self._lib.mb_storage_width(self._handle)

    @property
    def thread_count(self) -> int:
        return self._lib.mb_thread_count(self._handle)

    @property
    def kernel_name(self) -> str:
        name = self._lib.mb_kernel_name(self._handle)
        return name.decode() if name else ""

    # -- configuration -----------------------------------------------------
    def set_view(self, zoom: float, center_x: float, center_y: float) -> None:
        _check(self._lib.mb_set_view(self._handle, zoom, center_x, center_y))

    def set_kernel(self, name: str) -> None:
        _check(self._lib.mb_set_kernel(self._handle, name.encode()))

    def set_pixel_order(self, order: str) -> None:
        _check(self._lib.mb_set_pixel_order(self._handle, _pixel_order_value(order)))

    def cancel(self) -> None:
        """Requests cancellation of a running render (thread safe)."""
        self._lib.mb_cancel(self._handle)

    # -- rendering ---------------------------------------------------------
    def render(self, colors: bool = True, iterations: bool = False):
        """Renders the current view into fresh numpy arrays.

        Returns the color array (``height x width x 3`` uint8, cropped view) when
        only colors are requested, the iteration array (``height x width``
        int32) when only iterations are requested, or a ``(colors, iterations)``
        tuple when both are requested.
        """
        _require_numpy()
        color_buffer = _np.empty((self.height, self.storage_width, 3), dtype=_np.uint8) if colors else None
        iteration_buffer = _np.empty((self.height, self.storage_width), dtype=_np.int32) if iterations else None
        self.render_into(color_buffer, iteration_buffer)
        outputs = []
        if color_buffer is not None:
            outputs.append(color_buffer[:, :self.width])
        if iteration_buffer is not None:
            outputs.append(iteration_buffer[:, :self.width])
        return outputs[0] if len(outputs) == 1 else tuple(outputs)

    def render_into(self, colors=None, iterations=None) -> None:
        """Renders into caller-provided C-contiguous arrays (zero copy).

        ``colors`` must be ``uint8`` with shape ``(height, >= storage_width, 3)``;
        ``iterations`` must be ``int32`` with shape ``(height, >= storage_width)``.
        """
        _require_numpy()
        if colors is None and iterations is None:
            raise ValueError("at least one of colors and iterations must be provided")
        if colors is not None:
            if colors.dtype != _np.uint8 or colors.ndim != 3 or colors.shape[2] != 3 or \
                    colors.shape[0] != self.height or colors.shape[1] < self.storage_width or \
                    not colors.flags["C_CONTIGUOUS"]:
                raise ValueError("colors must be a C-contiguous uint8 array of shape "
                                 f"({self.height}, >= {self.storage_width}, 3)")
        if iterations is not None:
            if iterations.dtype != _np.int32 or iterations.ndim != 2 or \
                    iterations.shape[0] != self.height or iterations.shape[1] < self.storage_width or \
                    not iterations.flags["C_CONTIGUOUS"]:
                raise ValueError("iterations must be a C-contiguous int32 array of shape "
                                 f"({self.height}, >= {self.storage_width})")
        color_pointer = colors.ctypes.data_as(POINTER(c_ubyte)) if colors is not None else None
        iteration_pointer = iterations.ctypes.data_as(POINTER(c_int32)) if iterations is not None else None
        _check(self._lib.mb_render_outputs(
            self._handle,
            color_pointer, colors.strides[0] if colors is not None else 0,
            iteration_pointer, iterations.strides[0] if iterations is not None else 0,
        ))

    def write_image(self, path: str, format: str = "bmp") -> None:
        """Renders the current view and writes it as ``bmp`` or ``pnm``."""
        _check(self._lib.mb_write_image(self._handle, str(path).encode(), _image_format_value(format)))


def _make_params(width: int, height: int, max_iterations: int, threads: int, zoom: float,
                 center_x: float, center_y: float, kernel: str | None, pixel_order: str) -> _RenderParams:
    params = _RenderParams()
    params.struct_size = ctypes.sizeof(_RenderParams)
    params.width = width
    params.height = height
    params.max_iterations = max_iterations
    params.threads = threads
    params.zoom = zoom
    params.center_x = center_x
    params.center_y = center_y
    params.kernel = kernel.encode() if kernel else None
    params.pixel_order = _pixel_order_value(pixel_order)
    return params


def render_frame(width: int, height: int, *, max_iterations: int = 2000, threads: int = 0,
                 zoom: float = 1.0, center: tuple[float, float] = (-0.5, 0.0),
                 kernel: str | None = None, pixel_order: str = "bgr",
                 colors: bool = True, iterations: bool = False, library: str | None = None):
    """One-shot render; see :meth:`Mandelbrot.render` for the return value."""
    _require_numpy()
    if not colors and not iterations:
        raise ValueError("at least one of colors and iterations must be requested")
    if library is not None:
        load_library(library)
    storage_width = (width + 31) & ~31
    color_buffer = _np.empty((height, storage_width, 3), dtype=_np.uint8) if colors else None
    iteration_buffer = _np.empty((height, storage_width), dtype=_np.int32) if iterations else None
    params = _make_params(width, height, max_iterations, threads, zoom, center[0], center[1],
                          kernel, pixel_order)
    color_pointer = color_buffer.ctypes.data_as(POINTER(c_ubyte)) if color_buffer is not None else None
    iteration_pointer = iteration_buffer.ctypes.data_as(POINTER(c_int32)) if iteration_buffer is not None else None
    _check(_lib().mb_render_frame(
        ctypes.byref(params),
        color_pointer, color_buffer.strides[0] if color_buffer is not None else 0,
        iteration_pointer, iteration_buffer.strides[0] if iteration_buffer is not None else 0,
    ))
    outputs = []
    if color_buffer is not None:
        outputs.append(color_buffer[:, :width])
    if iteration_buffer is not None:
        outputs.append(iteration_buffer[:, :width])
    return outputs[0] if len(outputs) == 1 else tuple(outputs)


def render_image(path: str, width: int, height: int, *, max_iterations: int = 2000, threads: int = 0,
                 zoom: float = 1.0, center: tuple[float, float] = (-0.5, 0.0),
                 kernel: str | None = None, pixel_order: str = "bgr", format: str = "bmp",
                 library: str | None = None) -> None:
    """One-shot render written directly to ``path`` (bmp or pnm)."""
    if library is not None:
        load_library(library)
    params = _make_params(width, height, max_iterations, threads, zoom, center[0], center[1],
                          kernel, pixel_order)
    _check(_lib().mb_render_image(ctypes.byref(params), str(path).encode(), _image_format_value(format)))


def write_video(path: str, width: int = 1920, height: int = 1080, *, max_iterations: int = 2000,
                threads: int = 0, fps: float = 60.0, duration: float = 10.0,
                kernel: str | None = None, pixel_order: str = "bgr",
                codec: str | None = None, quality: int = -1,
                library: str | None = None) -> None:
    """Renders the application's zoom animation and writes it to ``path``.

    Needs the optional OpenCV-based ``mandelbrot_video`` library. Raises
    :class:`VideoError` when no usable encoder is available, and
    :class:`FileNotFoundError` when the library itself is not built.
    """
    if library is not None:
        load_video_library(library)
    params = _VideoParams()
    params.struct_size = ctypes.sizeof(_VideoParams)
    params.width = width
    params.height = height
    params.max_iterations = max_iterations
    params.threads = threads
    params.fps = fps
    params.duration_seconds = duration
    params.kernel = kernel.encode() if kernel else None
    params.pixel_order = _pixel_order_value(pixel_order)
    params.codec = codec.encode() if codec else None
    params.quality = quality
    _check_video(_lib_video().mb_write_video(ctypes.byref(params), str(path).encode()))
