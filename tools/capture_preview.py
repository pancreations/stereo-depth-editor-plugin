"""Exercise the native transmitter and capture only its temporary test window."""
import ctypes
from ctypes import wintypes
from pathlib import Path
import subprocess
import json
from PIL import ImageGrab

root = Path(__file__).resolve().parents[1]
user = ctypes.WinDLL('user32')
user.SetThreadDpiAwarenessContext.argtypes = [ctypes.c_void_p]
user.SetThreadDpiAwarenessContext(ctypes.c_void_p(-4))
user.FindWindowW.argtypes = [wintypes.LPCWSTR, wintypes.LPCWSTR]
user.FindWindowW.restype = wintypes.HWND
user.GetAncestor.argtypes = [wintypes.HWND, wintypes.UINT]
user.GetAncestor.restype = wintypes.HWND
user.GetParent.argtypes = [wintypes.HWND]
user.GetParent.restype = wintypes.HWND
user.IsWindowVisible.argtypes = [wintypes.HWND]
user.GetWindowThreadProcessId.argtypes = [wintypes.HWND, ctypes.POINTER(wintypes.DWORD)]
enum_callback = ctypes.WINFUNCTYPE(wintypes.BOOL, wintypes.HWND, wintypes.LPARAM)
user.EnumWindows.argtypes = [enum_callback, wintypes.LPARAM]
test = root/'build/native/preview/Release/s3d_preview_tests.exe'
plugin = root/'build/native/preview/Release/StereoDepthPreview.prm'
with subprocess.Popen([str(test), str(plugin), '--visual'], stdout=subprocess.PIPE,
                      stderr=subprocess.STDOUT, text=True, creationflags=subprocess.CREATE_NO_WINDOW) as process:
    for line in process.stdout:
        print(line.strip(), flush=True)
        if 'Preview visible' in line:
            hwnd = user.FindWindowW('StereoDepth.SBSPreview.v1', 'StereoDepth SBS Preview')
            if not hwnd:
                raise RuntimeError('Preview test window disappeared')
            listed = []
            @enum_callback
            def record_window(handle, _):
                if handle == hwnd:
                    listed.append(handle)
                return True
            user.EnumWindows(record_window, 0)
            assert listed and user.IsWindowVisible(hwnd)
            assert user.GetAncestor(hwnd, 2) == hwnd and not user.GetParent(hwnd)
            owner_pid = wintypes.DWORD()
            user.GetWindowThreadProcessId(hwnd, ctypes.byref(owner_pid))
            assert owner_pid.value == process.pid
            capture = ImageGrab.grab(window=hwnd)
            capture.save(root/'artifacts/sbs-preview-window.png')
            # Verify the captured full-SBS pattern rather than a blank or stale desktop.
            w, h = capture.size
            expected = [(255, 0, 0), (0, 255, 0), (255, 0, 255), (0, 255, 255)]
            for xy, color in zip([(w//4, h//4), (3*w//4, h//4), (w//4, 3*h//4), (3*w//4, 3*h//4)], expected):
                assert all(abs(a-b) < 8 for a, b in zip(capture.getpixel(xy)[:3], color))
            (root/'artifacts/preview-capture-validation.json').write_text(json.dumps({
                'window': 'StereoDepth SBS Preview', 'top_level': True, 'unowned': True,
                'visible_in_enum_windows': True, 'captured_from_separate_process': True,
                'size': capture.size, 'verified_sbs_pattern': True,
                'test_host': 'Native SDK harness; actual Premiere still needs validation'
            }, indent=2))
            print('Captured', capture.size, flush=True)
    if process.wait():
        raise RuntimeError('Preview test failed')
