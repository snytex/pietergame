import hashlib
import io
import json
import os
from pathlib import Path
import sys
import tempfile
import unittest
from unittest.mock import patch
import zipfile

sys.path.insert(0, str(Path(__file__).resolve().parents[1] / "tools"))
import launcher as updater
from package_release import package


class Updates(unittest.TestCase):
    def setUp(self):
        self.temp = tempfile.TemporaryDirectory()
        self.addCleanup(self.temp.cleanup)
        self.root = Path(self.temp.name)
        self.target = "linux-x64"
        self.manifest = {"version": "3.0.2", "platform": self.target}

    def payload(self, extra=None, manifest=None):
        buffer = io.BytesIO()
        with zipfile.ZipFile(buffer, "w") as archive:
            archive.writestr("pietergame3", "binary")
            archive.writestr("assets/images/menu_bg.png", "image")
            archive.writestr("version.json", json.dumps(manifest or self.manifest))
            if extra:
                name, content = extra
                if isinstance(name, str):
                    # Preserve hostile raw names even on Windows, where ZipInfo
                    # normally normalizes backslashes before writing the fixture.
                    info = zipfile.ZipInfo(name)
                    info.filename = name
                    name = info
                archive.writestr(name, content)
        return buffer.getvalue()

    def asset(self, data):
        return {"name": "PieterGame3-linux-x64-update.zip", "size": len(data),
                "digest": "sha256:" + hashlib.sha256(data).hexdigest(),
                "browser_download_url": f"https://github.com/{updater.REPOSITORY}/releases/download/v3.0.2/PieterGame3-linux-x64-update.zip"}

    def test_versions_and_no_downgrades(self):
        release = {"tag_name": "v3.0.2", "assets": [self.asset(b"x")]}
        self.assertIsNotNone(updater.release_asset(release, "3.0.1", self.target))
        for current in ("3.0.2", "3.0.10", "4.0.0"):
            self.assertIsNone(updater.release_asset(release, current, self.target))
        release["prerelease"] = True
        self.assertIsNone(updater.release_asset(release, "3.0.1", self.target))
        for invalid in ("../3.0.2", "3.0.2-beta", "03.0.2", "3.2"):
            with self.assertRaises(ValueError):
                updater.version(invalid)

    def test_untrusted_source_and_missing_digest(self):
        asset = self.asset(b"x")
        release = {"tag_name": "v3.0.2", "assets": [asset]}
        asset["browser_download_url"] = "https://example.com/game.zip"
        with self.assertRaises(ValueError):
            updater.release_asset(release, "3.0.1", self.target)
        asset.update(self.asset(b"x"))
        del asset["digest"]
        with self.assertRaises(ValueError):
            updater.release_asset(release, "3.0.1", self.target)

    def test_install_switches_pointer_after_validation(self):
        data = self.payload()
        messages = []
        with patch.object(updater, "request", return_value=io.BytesIO(data)):
            updater.install(self.root, "3.0.2", self.target, self.asset(data), messages.append)
        pointer = json.loads((self.root / "current.json").read_text())
        binary = self.root / pointer["directory"] / "pietergame3"
        self.assertTrue(binary.is_file())
        if os.name != "nt":
            self.assertTrue(binary.stat().st_mode & 0o100)
        self.assertIn("installed!", messages[-1])
        self.assertFalse(list(self.root.glob("staging-*")))

    def test_checksum_failure_preserves_current(self):
        (self.root / "current.json").write_text("old pointer")
        data = self.payload()
        asset = self.asset(data)
        asset["digest"] = "sha256:" + "0" * 64
        with patch.object(updater, "request", return_value=io.BytesIO(data)):
            with self.assertRaises(ValueError):
                updater.install(self.root, "3.0.2", self.target, asset, lambda _: None)
        self.assertEqual((self.root / "current.json").read_text(), "old pointer")
        self.assertFalse(list(self.root.glob("version-*")))

    def test_bad_archive_paths_and_manifest(self):
        symlink = zipfile.ZipInfo("assets/link")
        symlink.external_attr = (0o120777 << 16)
        for extra in (("../escape", "bad"), ("/absolute", "bad"), ("assets\\escape", "bad"),
                      ("assets/x:stream", "bad"), ("unknown", "bad"), (symlink, "../../outside")):
            with self.subTest(extra=extra), self.assertRaises(ValueError):
                updater.extract(io.BytesIO(self.payload(extra)), self.root / "out", "3.0.2", self.target)
        with self.assertRaises(ValueError):
            updater.extract(io.BytesIO(self.payload()), self.root / "out", "9.0.0", self.target)

    def test_offline_is_nonfatal(self):
        messages = []
        with patch.object(updater, "request", side_effect=OSError("offline")):
            self.assertEqual(updater.check(self.root, "3.0.1", self.target, messages.append), "3.0.1")
        self.assertIn("keep playing", messages[-1])

    def test_select_updated_or_fallback(self):
        bundled = self.root / "bundle"
        bundled.mkdir()
        (bundled / "version.json").write_text(json.dumps({"version": "3.0.1"}))
        data = self.payload()
        with patch.object(updater, "request", return_value=io.BytesIO(data)):
            updater.install(self.root, "3.0.2", self.target, self.asset(data), lambda _: None)
        game, selected = updater.selected_game(self.root, bundled, self.target)
        self.assertEqual(selected, "3.0.2")
        (game / "pietergame3").unlink()
        self.assertEqual(updater.selected_game(self.root, bundled, self.target), (bundled, "3.0.1"))

    def test_truncated_download_preserves_installation(self):
        data = self.payload()
        with patch.object(updater, "request", return_value=io.BytesIO(data[:-4])):
            with self.assertRaises(ValueError):
                updater.install(self.root, "3.0.2", self.target, self.asset(data), lambda _: None)
        self.assertFalse((self.root / "current.json").exists())

    def test_package_roundtrip(self):
        build = self.root / "build"
        (build / "assets/images").mkdir(parents=True)
        (build / "assets/images/menu_bg.png").write_text("image")
        (build / "pietergame3").write_text("game")
        launcher = self.root / "launcher"
        launcher.write_text("launcher")
        output = self.root / "output"
        package(build, launcher, output, "3.0.2", self.target)
        updater.extract(output / "PieterGame3-linux-x64-update.zip", self.root / "extracted", "3.0.2", self.target)
        with zipfile.ZipFile(output / "PieterGame3-linux-x64.zip") as full:
            self.assertIn("PieterGame3", full.namelist())
            self.assertIn("game/version.json", full.namelist())

    def test_installed_release_is_not_downgraded(self):
        data = self.payload()
        with patch.object(updater, "request", return_value=io.BytesIO(data)):
            updater.install(self.root, "3.0.2", self.target, self.asset(data), lambda _: None)
        previous = (self.root / "current.json").read_text()
        with patch.object(updater, "request", side_effect=AssertionError("should not download")):
            updater.install(self.root, "3.0.1", self.target, {}, lambda _: None)
        self.assertEqual((self.root / "current.json").read_text(), previous)


if __name__ == "__main__":
    unittest.main()
