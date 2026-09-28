"""Control only the add-on's already-loaded native preview window."""
import ctypes
import os
from ctypes import wintypes


def open_preview():
    if os.name != 'nt':
        raise ValueError('SBS Preview currently requires Windows.')
    user = ctypes.WinDLL('user32', use_last_error=True)
    user.FindWindowW.argtypes = [wintypes.LPCWSTR, wintypes.LPCWSTR]
    user.FindWindowW.restype = wintypes.HWND
    user.PostMessageW.argtypes = [wintypes.HWND, wintypes.UINT, wintypes.WPARAM, wintypes.LPARAM]
    user.PostMessageW.restype = wintypes.BOOL
    handle = user.FindWindowW('StereoDepth.SBSPreview.v1', 'StereoDepth SBS Preview')
    if not handle:
        raise ValueError('In Premiere, open Edit > Preferences > Playback, enable Mercury Transmit '
                         'and StereoDepth SBS Preview, then click its Setup button or play the timeline. '
                         'Restart Premiere after installing the add-on update if the device is missing.')
    if not user.PostMessageW(handle, 0x8003, 0, 0):
        raise OSError(ctypes.get_last_error(), 'Could not open SBS Preview')
    return {'opened': True, 'window': 'StereoDepth SBS Preview'}
