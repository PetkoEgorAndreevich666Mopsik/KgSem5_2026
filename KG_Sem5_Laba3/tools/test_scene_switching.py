import ctypes
from ctypes import wintypes
from pathlib import Path
import re
import subprocess
import time

root = Path(__file__).resolve().parents[1]
startup = subprocess.STARTUPINFO()
startup.dwFlags |= subprocess.STARTF_USESHOWWINDOW
startup.wShowWindow = 0
process = subprocess.Popen([str(root / 'cmake-build-debug/CG_Sem2.exe')],
                           cwd=root / 'cmake-build-debug', startupinfo=startup)
user32 = ctypes.windll.user32
callback_type = ctypes.WINFUNCTYPE(wintypes.BOOL, wintypes.HWND, wintypes.LPARAM)
user32.PostMessageW.argtypes = [wintypes.HWND, wintypes.UINT, wintypes.WPARAM, wintypes.LPARAM]


def wait_for_scene(name):
    deadline = time.monotonic() + 90
    while time.monotonic() < deadline:
        if process.poll() is not None:
            raise AssertionError(f'Application exited: {process.returncode}')
        matches = []

        def visit(window, unused):
            pid = wintypes.DWORD()
            user32.GetWindowThreadProcessId(window, ctypes.byref(pid))
            if pid.value == process.pid:
                title = ctypes.create_unicode_buffer(2048)
                user32.GetWindowTextW(window, title, len(title))
                if title.value.startswith(name + ' | FPS '):
                    matches.append((window, title.value))
            return True

        user32.EnumWindows(callback_type(visit), 0)
        if matches:
            window, title = matches[0]
            match = re.search(r'(?:Patches|Draws) (\d+)', title)
            if match and int(match.group(1)) > 0:
                print(title, flush=True)
                return window
        time.sleep(0.1)
    raise AssertionError(f'Timed out waiting for {name}')


try:
    window = wait_for_scene('Terrain')
    for key, name in ((0x62, 'Earth'), (0x63, 'Sponza'), (0x61, 'Terrain')):
        if not user32.PostMessageW(window, 0x100, key, 0):
            raise ctypes.WinError()
        window = wait_for_scene(name)
    print('Default terrain and NumPad 2/3/1 switching passed')
finally:
    if process.poll() is None:
        process.terminate()
        process.wait(timeout=10)
