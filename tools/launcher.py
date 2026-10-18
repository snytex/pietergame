"""PieterGame release launcher. Frozen with PyInstaller; no Python needed by players."""
import hashlib
import json
import os
from pathlib import Path, PurePosixPath
import platform
import re
import stat
import subprocess
import sys
import tempfile
import threading
import time
import urllib.request
import zipfile

REPOSITORY = "snytex/pietergame"
API = f"https://api.github.com/repos/{REPOSITORY}/releases/latest"
MAX_DOWNLOAD = 1024 * 1024 * 1024
MAX_UNPACKED = 2 * MAX_DOWNLOAD


def version(value):
    if not isinstance(value, str) or not re.fullmatch(r"(0|[1-9]\d*)\.(0|[1-9]\d*)\.(0|[1-9]\d*)", value):
        raise ValueError("Expected a stable major.minor.patch version")
    return tuple(map(int, value.split(".")))


def platform_name():
    if platform.machine().lower() not in ("amd64", "x86_64"):
        raise RuntimeError("This release requires an x86-64 computer")
    return "windows-x64" if sys.platform == "win32" else "linux-x64"


def executable(target):
    return "pietergame3.exe" if target == "windows-x64" else "pietergame3"


def atomic_text(path, value):
    path.parent.mkdir(parents=True, exist_ok=True)
    fd, temporary = tempfile.mkstemp(dir=path.parent, prefix=".write-")
    try:
        with os.fdopen(fd, "w", encoding="utf-8") as stream:
            stream.write(value)
            stream.flush()
            os.fsync(stream.fileno())
        os.replace(temporary, path)
    finally:
        Path(temporary).unlink(missing_ok=True)


def request(url):
    return urllib.request.urlopen(urllib.request.Request(url, headers={
        "User-Agent": "PieterGame3-Updater", "Accept": "application/vnd.github+json"
    }), timeout=20)


def release_asset(release, current, target):
    if release.get("draft") or release.get("prerelease"):
        return None
    tag = release["tag_name"]
    latest = tag.removeprefix("v")
    if version(latest) <= version(current):
        return None
    name = f"PieterGame3-{target}-update.zip"
    asset = next(a for a in release["assets"] if a["name"] == name)
    # Only binaries deliberately published by this project's release repository.
    expected = f"https://github.com/{REPOSITORY}/releases/download/{tag}/{name}"
    if asset["browser_download_url"] != expected:
        raise ValueError("Unexpected update download source")
    digest = asset.get("digest", "")
    if not re.fullmatch(r"sha256:[a-f0-9]{64}", digest):
        raise ValueError("Release has no SHA-256 digest")
    if not 0 < asset["size"] <= MAX_DOWNLOAD:
        raise ValueError("Update is too large")
    return latest, asset


def extract(archive, destination, latest, target):
    with zipfile.ZipFile(archive) as bundle:
        entries = bundle.infolist()
        if len(entries) > 20000 or sum(i.file_size for i in entries) > MAX_UNPACKED:
            raise ValueError("Update exceeds extraction limits")
        seen = set()
        for item in entries:
            path = PurePosixPath(item.filename)
            mode = item.external_attr >> 16
            if (path.is_absolute() or ".." in path.parts or "\\" in item.orig_filename
                    or ":" in item.filename or stat.S_ISLNK(mode)
                    or item.filename.lower() in seen):
                raise ValueError("Unsafe archive entry")
            seen.add(item.filename.lower())
            if path.parts and path.parts[0] not in ("assets", "version.json", executable(target)):
                raise ValueError("Unexpected archive content")
        bundle.extractall(destination)
    manifest = json.loads((destination / "version.json").read_text())
    if manifest != {"version": latest, "platform": target}:
        raise ValueError("Update version or platform does not match release")
    if not (destination / executable(target)).is_file() or not (destination / "assets/images/menu_bg.png").is_file():
        raise ValueError("Incomplete game update")
    (destination / executable(target)).chmod(0o755)


class UpdateLock:
    """OS-owned lock: automatically released after a crash, including on Windows."""
    def __init__(self, root):
        self.path = root / "update.lock"

    def __enter__(self):
        self.stream = self.path.open("a+b")
        try:
            if sys.platform == "win32":
                import msvcrt
                self.stream.write(b"0")
                self.stream.flush()
                self.stream.seek(0)
                msvcrt.locking(self.stream.fileno(), msvcrt.LK_NBLCK, 1)
            else:
                import fcntl
                fcntl.flock(self.stream, fcntl.LOCK_EX | fcntl.LOCK_NB)
        except OSError:
            self.stream.close()
            raise
        return self

    def __exit__(self, *_):
        self.stream.close()


def install(root, latest, target, asset, notify):
    with UpdateLock(root), tempfile.TemporaryDirectory(prefix="staging-", dir=root) as temporary:
        try:
            installed = json.loads((root / "current.json").read_text())
            directory = installed["directory"]
            valid_directory = re.fullmatch(r"version-[0-9.]+-staging-[a-zA-Z0-9_-]+", directory)
            candidate = root / directory if valid_directory else root / "missing"
            manifest = json.loads((candidate / "version.json").read_text())
            if (version(installed["version"]) >= version(latest)
                    and manifest == {"version": installed["version"], "platform": target}
                    and (candidate / executable(target)).is_file()
                    and (candidate / "assets/images/menu_bg.png").is_file()):
                notify(f"Update {installed['version']} installed! Close and reopen the game to play it.")
                return
        except (OSError, ValueError, KeyError, TypeError):
            pass
        staging = Path(temporary)
        archive = staging / "update.zip"
        digest = hashlib.sha256()
        downloaded = 0
        deadline = time.monotonic() + 1800
        with request(asset["browser_download_url"]) as response, archive.open("wb") as stream:
            while chunk := response.read(1024 * 1024):
                downloaded += len(chunk)
                if downloaded > asset["size"] or time.monotonic() > deadline:
                    raise ValueError("Download exceeds release size or time limit")
                digest.update(chunk)
                stream.write(chunk)
                notify(f"Update {latest}: downloading {downloaded * 100 // asset['size']}%")
        if downloaded != asset["size"] or "sha256:" + digest.hexdigest() != asset["digest"]:
            raise ValueError("Update checksum mismatch")
        notify(f"Update {latest}: installing...")
        unpacked = staging / "game"
        extract(archive, unpacked, latest, target)
        # Unique immutable directories allow multiple running game instances and
        # Windows' executable locks. Only the tiny pointer is replaced.
        destination = root / ("version-" + latest + "-" + staging.name)
        unpacked.rename(destination)
        atomic_text(root / "current.json", json.dumps({"version": latest, "directory": destination.name}))
        notify(f"Update {latest} installed! Close and reopen the game to play it.")


def selected_game(root, bundled, target):
    baseline = json.loads((bundled / "version.json").read_text())["version"]
    version(baseline)
    try:
        installed = json.loads((root / "current.json").read_text())
        directory = installed["directory"]
        if not re.fullmatch(r"version-[0-9.]+-staging-[a-zA-Z0-9_-]+", directory):
            raise ValueError("Invalid installation pointer")
        candidate = root / directory
        manifest = json.loads((candidate / "version.json").read_text())
        if (version(installed["version"]) > version(baseline)
                and manifest == {"version": installed["version"], "platform": target}
                and (candidate / executable(target)).is_file()
                and (candidate / "assets/images/menu_bg.png").is_file()):
            return candidate, installed["version"]
    except (OSError, ValueError, KeyError, TypeError):
        pass
    return bundled, baseline


def check(root, current, target, notify):
    try:
        notify("Checking for game updates...")
        with request(API) as response:
            release = json.loads(response.read(2 * 1024 * 1024))
        found = release_asset(release, current, target)
        if found:
            latest, asset = found
            notify(f"New update {latest} found! Downloading automatically...")
            install(root, latest, target, asset, notify)
            return latest
        notify(f"PieterGame {current} is up to date.")
    except Exception as error:
        # Network/rate-limit/disk/checksum failures never prevent playing.
        atomic_text(root / "last-error.txt", f"{type(error).__name__}: {error}\n")
        notify("Update unavailable. You can keep playing; retrying later.")
    return current


def main():
    home = Path(sys.executable).parent if getattr(sys, "frozen", False) else Path(__file__).resolve().parent
    if len(sys.argv) == 3 and sys.argv[1] == "--bundle":
        home = Path(sys.argv[2]).resolve()
    target = platform_name()
    data = Path(os.environ.get("LOCALAPPDATA", str(Path.home() / "AppData/Local"))) if sys.platform == "win32" else Path(os.environ.get("XDG_DATA_HOME", str(Path.home() / ".local/share")))
    root = data / "PieterGame3" / "updates" / target
    root.mkdir(parents=True, exist_ok=True)
    game, current = selected_game(root, home / "game", target)
    with tempfile.TemporaryDirectory(prefix="session-", dir=root) as session:
        status = Path(session) / "status.txt"
        notify = lambda message: atomic_text(status, message)
        notify(f"PieterGame {current}")
        environment = dict(os.environ, PG_UPDATE_STATUS=str(status))
        # PyInstaller's private libraries belong to the launcher, not the game.
        if getattr(sys, "frozen", False):
            if sys.platform == "win32":
                import ctypes
                ctypes.windll.kernel32.SetDllDirectoryW(None)
            elif "LD_LIBRARY_PATH_ORIG" in environment:
                environment["LD_LIBRARY_PATH"] = environment["LD_LIBRARY_PATH_ORIG"]
            else:
                environment.pop("LD_LIBRARY_PATH", None)
        process = subprocess.Popen([str(game / executable(target))], cwd=game, env=environment)
        stopped = threading.Event()

        def worker():
            installed = current
            while not stopped.is_set():
                updated = check(root, installed, target, notify)
                if updated != installed:  # Keep the restart notice visible.
                    return
                if stopped.wait(1800):
                    return
        thread = threading.Thread(target=worker, daemon=True)
        if os.environ.get("PG_NO_UPDATES") != "1":
            thread.start()
        result = process.wait()
        stopped.set()
        # A download already started is allowed to finish after the game closes.
        if thread.is_alive():
            thread.join()
        return result


if __name__ == "__main__":
    try:
        sys.exit(main())
    except Exception as error:
        if sys.platform == "win32":
            import ctypes
            ctypes.windll.user32.MessageBoxW(None, str(error), "PieterGame could not start", 0x10)
        else:
            print(f"PieterGame could not start: {error}", file=sys.stderr)
        sys.exit(1)
