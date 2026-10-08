"""Bounded native-window evidence for Windows gates; never builds or blesses."""

import argparse
import ctypes
from ctypes import wintypes
import json
import os
from pathlib import Path
import struct
import subprocess
import tempfile
import time


user = ctypes.WinDLL("user32", use_last_error=True)
kernel = ctypes.WinDLL("kernel32", use_last_error=True)
gdi = ctypes.WinDLL("gdi32", use_last_error=True)


class Mouse(ctypes.Structure):
    _fields_ = [("dx", wintypes.LONG), ("dy", wintypes.LONG),
                ("data", wintypes.DWORD), ("flags", wintypes.DWORD),
                ("time", wintypes.DWORD), ("extra", ctypes.c_size_t)]


class Keyboard(ctypes.Structure):
    _fields_ = [("vk", wintypes.WORD), ("scan", wintypes.WORD),
                ("flags", wintypes.DWORD), ("time", wintypes.DWORD),
                ("extra", ctypes.c_size_t)]


class Payload(ctypes.Union):
    _fields_ = [("mouse", Mouse), ("keyboard", Keyboard)]


class Input(ctypes.Structure):
    _anonymous_ = ("payload",)
    _fields_ = [("type", wintypes.DWORD), ("payload", Payload)]


def bind(library, name, result, *arguments):
    function = getattr(library, name)
    function.restype = result
    function.argtypes = arguments
    return function


bind(user, "SetThreadDpiAwarenessContext", wintypes.HANDLE, wintypes.HANDLE)(-4)
bind(user, "GetForegroundWindow", wintypes.HWND)
bind(user, "SetForegroundWindow", wintypes.BOOL, wintypes.HWND)
bind(user, "AttachThreadInput", wintypes.BOOL, wintypes.DWORD, wintypes.DWORD, wintypes.BOOL)
bind(kernel, "GetCurrentThreadId", wintypes.DWORD)
bind(user, "GetWindowThreadProcessId", wintypes.DWORD, wintypes.HWND,
     ctypes.POINTER(wintypes.DWORD))
bind(user, "GetClientRect", wintypes.BOOL, wintypes.HWND, ctypes.POINTER(wintypes.RECT))
bind(user, "ClientToScreen", wintypes.BOOL, wintypes.HWND, ctypes.POINTER(wintypes.POINT))
bind(user, "GetDpiForWindow", wintypes.UINT, wintypes.HWND)
bind(user, "MoveWindow", wintypes.BOOL, wintypes.HWND, ctypes.c_int, ctypes.c_int,
     ctypes.c_int, ctypes.c_int, wintypes.BOOL)
bind(user, "ShowWindow", wintypes.BOOL, wintypes.HWND, ctypes.c_int)
bind(user, "PostMessageW", wintypes.BOOL, wintypes.HWND, wintypes.UINT,
     wintypes.WPARAM, wintypes.LPARAM)
bind(user, "SendInput", wintypes.UINT, wintypes.UINT, ctypes.POINTER(Input), ctypes.c_int)
bind(user, "GetDC", wintypes.HDC, wintypes.HWND)
bind(user, "ReleaseDC", ctypes.c_int, wintypes.HWND, wintypes.HDC)
bind(kernel, "OpenProcess", wintypes.HANDLE, wintypes.DWORD, wintypes.BOOL, wintypes.DWORD)
bind(kernel, "GetExitCodeProcess", wintypes.BOOL, wintypes.HANDLE,
     ctypes.POINTER(wintypes.DWORD))
bind(kernel, "GetProcessTimes", wintypes.BOOL, wintypes.HANDLE,
     *([ctypes.POINTER(wintypes.FILETIME)] * 4))
bind(kernel, "CloseHandle", wintypes.BOOL, wintypes.HANDLE)
bind(kernel, "WaitForSingleObject", wintypes.DWORD, wintypes.HANDLE, wintypes.DWORD)
bind(gdi, "CreateCompatibleDC", wintypes.HDC, wintypes.HDC)
bind(gdi, "CreateCompatibleBitmap", wintypes.HBITMAP, wintypes.HDC, ctypes.c_int, ctypes.c_int)
bind(gdi, "SelectObject", wintypes.HANDLE, wintypes.HDC, wintypes.HANDLE)
bind(gdi, "DeleteObject", wintypes.BOOL, wintypes.HANDLE)
bind(gdi, "DeleteDC", wintypes.BOOL, wintypes.HDC)
bind(gdi, "BitBlt", wintypes.BOOL, wintypes.HDC, ctypes.c_int, ctypes.c_int,
     ctypes.c_int, ctypes.c_int, wintypes.HDC, ctypes.c_int, ctypes.c_int, wintypes.DWORD)
bind(gdi, "GetDIBits", ctypes.c_int, wintypes.HDC, wintypes.HBITMAP, wintypes.UINT,
     wintypes.UINT, ctypes.c_void_p, ctypes.c_void_p, wintypes.UINT)


def process_identity(pid):
    handle = kernel.OpenProcess(0x1000, False, pid)
    if not handle:
        raise RuntimeError(f"Owned client PID {pid} no longer exists")
    try:
        code = wintypes.DWORD()
        times = [wintypes.FILETIME() for _ in range(4)]
        if not kernel.GetExitCodeProcess(handle, ctypes.byref(code)):
            raise ctypes.WinError(ctypes.get_last_error())
        if not kernel.GetProcessTimes(handle, *(ctypes.byref(item) for item in times)):
            raise ctypes.WinError(ctypes.get_last_error())
        return code.value, (times[0].dwHighDateTime << 32) | times[0].dwLowDateTime
    finally:
        kernel.CloseHandle(handle)


def find_window(pid):
    matches = []
    callback_type = ctypes.WINFUNCTYPE(wintypes.BOOL, wintypes.HWND, wintypes.LPARAM)

    @callback_type
    def callback(hwnd, parameter):
        owner = wintypes.DWORD()
        user.GetWindowThreadProcessId(hwnd, ctypes.byref(owner))
        rect = wintypes.RECT()
        user.GetClientRect(hwnd, ctypes.byref(rect))
        if owner.value == pid and rect.right > 400 and rect.bottom > 200:
            matches.append(hwnd)
        return True

    user.EnumWindows(callback, 0)
    return matches[0] if matches else None


class Probe:
    def __init__(self, state):
        self.path = Path(state)
        self.state = json.loads(self.path.read_text())
        self.root = self.path.parent
        self.env = os.environ.copy()
        self.env.update(self.state["environment"])

    def record(self, kind, **values):
        with (self.root / "events.jsonl").open("a", encoding="utf-8") as output:
            output.write(json.dumps({"time": time.time(), "kind": kind, **values}) + "\n")

    def cli(self, *arguments):
        command = [self.state["exe"], *arguments, "--server-runtime-dir",
                   str(self.root / "r"), "--session", "gate", "--json"]
        result = subprocess.run(command, env=self.env, capture_output=True,
                                text=True, encoding="utf-8", timeout=15)
        self.record("cli", command=command, code=result.returncode,
                    stdout=result.stdout, stderr=result.stderr)
        if result.returncode:
            raise RuntimeError(f"CLI failed: {arguments}: {result.stdout} {result.stderr}")
        if arguments == ("--shutdown-server", "--yes"):
            return {"exit_code": result.returncode, "shutdown_requested": True,
                    "stdout": result.stdout}
        return json.loads(result.stdout)

    def guard(self, route=True):
        code, started = process_identity(self.state["pid"])
        if code != 259 or started != self.state["started"]:
            raise RuntimeError(f"Owned client exited/replaced: {code}, {started}")
        hwnd = find_window(self.state["pid"])
        if not hwnd:
            raise RuntimeError("Owned client has no render window")
        rect = wintypes.RECT()
        point = wintypes.POINT()
        user.GetClientRect(hwnd, ctypes.byref(rect))
        user.ClientToScreen(hwnd, ctypes.byref(point))
        result = dict(pid=self.state["pid"], started=started, hwnd=hwnd,
                      dpi=user.GetDpiForWindow(hwnd), x=point.x, y=point.y,
                      width=rect.right, height=rect.bottom, exit_code=code)
        log_path = self.root / "client.log"
        if log_path.exists():
            log_bytes = log_path.read_bytes()
            result["client_log_bytes"] = len(log_bytes)
            result["validation_error_count"] = log_bytes.count(b"[ERROR:Validation]")
        if route and self.state.get("control_id"):
            routes = self.cli("ui", "list")
            if not any(item["control_id"] == self.state["control_id"] for item in routes):
                raise RuntimeError("Exact owned client UI route detached")
            result["control_id"] = self.state["control_id"]
        self.record("liveness", **result)
        return result

    def foreground(self):
        info = self.guard()
        user.ShowWindow(info["hwnd"], 5)
        user.SetForegroundWindow(info["hwnd"])
        time.sleep(0.15)
        if user.GetForegroundWindow() != info["hwnd"]:
            owner = wintypes.DWORD()
            foreground_thread = user.GetWindowThreadProcessId(
                user.GetForegroundWindow(), ctypes.byref(owner))
            current_thread = kernel.GetCurrentThreadId()
            attached = user.AttachThreadInput(current_thread, foreground_thread, True)
            try:
                if attached:
                    user.SetForegroundWindow(info["hwnd"])
            finally:
                if attached:
                    user.AttachThreadInput(current_thread, foreground_thread, False)
        if user.GetForegroundWindow() != info["hwnd"]:
            raise RuntimeError("Foreground ownership unavailable; refusing native input")
        return info

    def send(self, item):
        if user.GetForegroundWindow() != find_window(self.state["pid"]):
            raise RuntimeError("Foreground ownership lost; refusing native input")
        if user.SendInput(1, ctypes.byref(item), ctypes.sizeof(Input)) != 1:
            raise ctypes.WinError(ctypes.get_last_error())

    def key(self, key, down):
        item = Input(type=1)
        item.keyboard = Keyboard(key, 0, 0 if down else 2, 0, 0)
        self.send(item)

    def text(self, value):
        self.foreground()
        for character in value:
            item = Input(type=1)
            item.keyboard = Keyboard(0, ord(character), 4, 0, 0)
            self.send(item)
            item.keyboard.flags = 6
            self.send(item)
            time.sleep(0.006)
        self.record("native-text", text=value)

    def chord(self, *keys):
        self.foreground()
        for key in keys:
            self.key(key, True)
        for key in reversed(keys):
            self.key(key, False)
        self.record("native-keys", keys=keys)

    def zoom(self):
        self.chord(0x11, ord("S"))
        time.sleep(0.1)
        self.chord(ord("Z"))
        time.sleep(0.3)

    def mouse(self, x, y, action="move"):
        info = self.foreground()
        screen_x, screen_y = info["x"] + x, info["y"] + y
        left, top, width, height = (user.GetSystemMetrics(index) for index in (76, 77, 78, 79))
        item = Input(type=0)
        item.mouse = Mouse(round((screen_x - left) * 65535 / (width - 1)),
                           round((screen_y - top) * 65535 / (height - 1)),
                           0, 0xC001, 0, 0)
        self.send(item)
        time.sleep(0.08)
        for flags, data in {"move": [], "click": [(2, 0), (4, 0)],
                            "down": [(2, 0)], "up": [(4, 0)],
                            "wheel": [(0x800, 120)]}[action]:
            item.mouse = Mouse(0, 0, data, flags, 0, 0)
            self.send(item)
        self.record("native-mouse", x=x, y=y, action=action)

    def move(self, x, y, width=1400, height=950):
        info = self.guard()
        if not user.MoveWindow(info["hwnd"], x, y, width, height, True):
            raise ctypes.WinError(ctypes.get_last_error())
        time.sleep(1)
        return self.guard()

    def capture(self, name):
        info = self.foreground()
        time.sleep(0.3)
        width, height = info["width"], info["height"]
        screen = user.GetDC(None)
        memory = gdi.CreateCompatibleDC(screen)
        bitmap = gdi.CreateCompatibleBitmap(screen, width, height)
        previous = gdi.SelectObject(memory, bitmap)
        try:
            if not gdi.BitBlt(memory, 0, 0, width, height, screen,
                              info["x"], info["y"], 0x00CC0020 | 0x40000000):
                raise ctypes.WinError(ctypes.get_last_error())
            gdi.SelectObject(memory, previous)
            header = struct.pack("<IiiHHIIiiII", 40, width, -height, 1, 32, 0,
                                 width * height * 4, 0, 0, 0, 0)
            pixels = ctypes.create_string_buffer(width * height * 4)
            bitmap_info = ctypes.create_string_buffer(header)
            if gdi.GetDIBits(memory, bitmap, 0, height, pixels, bitmap_info, 0) != height:
                raise RuntimeError("Native screen capture failed")
            path = self.root / (name + ".bmp")
            path.write_bytes(struct.pack("<2sIHHI", b"BM", 54 + len(pixels), 0, 0, 54)
                             + header + pixels.raw)
            self.record("capture", path=str(path), **info)
            return str(path)
        finally:
            gdi.SelectObject(memory, previous)
            gdi.DeleteObject(bitmap)
            gdi.DeleteDC(memory)
            user.ReleaseDC(None, screen)

    def close(self):
        info = self.guard()
        handle = kernel.OpenProcess(0x101000, False, self.state["pid"])
        if not handle:
            raise RuntimeError("Cannot retain exact client exit handle")
        try:
            user.PostMessageW(info["hwnd"], 0x10, 0, 0)
            if kernel.WaitForSingleObject(handle, 20000) != 0:
                raise RuntimeError("Exact client failed normal shutdown in 20 seconds")
            code = wintypes.DWORD()
            if not kernel.GetExitCodeProcess(handle, ctypes.byref(code)):
                raise ctypes.WinError(ctypes.get_last_error())
            self.record("client-exit", exit_code=code.value, started=self.state["started"])
            if code.value:
                raise RuntimeError(f"Exact client exited with {code.value}")
        finally:
            kernel.CloseHandle(handle)
        return self.cli("--shutdown-server", "--yes")


def start(executable, host):
    root = Path(tempfile.mkdtemp(prefix="dg"))
    environment = {name: str(root / suffix) for name, suffix in
                   [("APPDATA", "c"), ("LOCALAPPDATA", "l"), ("TEMP", "t"), ("TMP", "t")]}
    environment["DRAXUL_SERVER_RUNTIME_DIR"] = str(root / "r")
    environment["DRAXUL_SESSION_ID"] = "gate"
    for value in environment.values():
        if value != "gate":
            Path(value).mkdir(parents=True, exist_ok=True)
    config = root / "c/draxul/config.toml"
    config.parent.mkdir(parents=True)
    config.write_text("window_width = 1400\nwindow_height = 950\nfont_size = 11.0\n"
                      "space_sidebar_columns = 8\n[markdown]\nfont_size = 12.0\n")
    env = {key: value for key, value in os.environ.items() if not key.startswith("DRAXUL_")}
    env.update(environment)
    env["VK_LOADER_DEBUG"] = "error,warn,layer"
    environment["VK_LOADER_DEBUG"] = env["VK_LOADER_DEBUG"]
    args = [str(Path(executable).resolve()), "--session", "gate", "--server-runtime-dir",
            str(root / "r"), "--log-file", str(root / "client.log"), "--log-level", "debug"]
    if host == "markdown":
        source = root / "density.md"
        source.write_text("# Density gate\n\n" + "\n\n".join(
            f"## Section {index:02}\n\nDensity geometry row {index:02}: repeated readable text."
            for index in range(1, 70)))
        args += ["--host", "markdown", "--source", str(source)]
    startup = subprocess.STARTUPINFO()
    startup.dwFlags |= subprocess.STARTF_USESHOWWINDOW
    startup.wShowWindow = 0
    with (root / "stdout.log").open("wb") as stdout, (root / "stderr.log").open("wb") as stderr:
        child = subprocess.Popen(args, env=env, stdout=stdout, stderr=stderr,
                                 startupinfo=startup, creationflags=subprocess.CREATE_NEW_PROCESS_GROUP)
    code, started = process_identity(child.pid)
    state = dict(exe=args[0], pid=child.pid, started=started, environment=environment,
                 host=host, command=args)
    path = root / "state.json"
    path.write_text(json.dumps(state, indent=2))
    print(str(path), flush=True)
    probe = Probe(path)
    for attempt in range(40):
        if child.poll() is not None:
            raise RuntimeError(f"Client startup exited {child.returncode}; evidence: {root}")
        if find_window(child.pid):
            try:
                routes = probe.cli("ui", "list")
                if len(routes) == 1:
                    state["control_id"] = routes[0]["control_id"]
                    path.write_text(json.dumps(state, indent=2))
                    probe.state = state
                    probe.move(150, 120)
                    print(json.dumps(probe.guard()), flush=True)
                    return
            except (RuntimeError, KeyError):
                pass
        time.sleep(0.5)
    raise RuntimeError(f"Startup did not attach one UI; evidence: {root}")


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--state")
    parser.add_argument("--exe")
    parser.add_argument("--host", default="terminal")
    parser.add_argument("--gpu-slot", action="store_true", required=True)
    parser.add_argument("operation", choices=["start", "status", "capture", "cli", "close"])
    parser.add_argument("arguments", nargs=argparse.REMAINDER)
    args = parser.parse_args()
    if args.operation == "start":
        start(args.exe, args.host)
        return
    probe = Probe(args.state)
    if args.operation == "cli":
        result = probe.cli(*args.arguments)
    elif args.operation == "capture":
        result = probe.capture(args.arguments[0])
    elif args.operation == "close":
        result = probe.close()
    else:
        result = probe.guard()
    print(json.dumps(result, indent=2))


if __name__ == "__main__":
    main()
