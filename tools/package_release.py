"""Make the initial download and update payload from a CMake build + frozen launcher."""
import argparse
import json
from pathlib import Path
import shutil
import tempfile
import zipfile


def package(build, launcher, output, version, target):
    from launcher import version as validate_version
    validate_version(version)
    output.mkdir(parents=True, exist_ok=True)
    with tempfile.TemporaryDirectory() as temporary:
        root = Path(temporary)
        game = root / "game"
        game.mkdir()
        binary = "pietergame3.exe" if target == "windows-x64" else "pietergame3"
        shutil.copy2(build / binary, game / binary)
        shutil.copytree(build / "assets", game / "assets")
        (game / "version.json").write_text(json.dumps({"version": version, "platform": target}))
        for suffix, base in (("-update", game), ("", root)):
            if not suffix:
                name = "PieterGame3.exe" if target == "windows-x64" else "PieterGame3"
                shutil.copy2(launcher, root / name)
                (root / name).chmod(0o755)
                (root / "READ ME.txt").write_text("Launch PieterGame3 to play with automatic updates.\nUpdates install in your user data folder and activate on next launch.\nSaves are preserved. Extract this entire zip before playing.\n")
            with zipfile.ZipFile(output / f"PieterGame3-{target}{suffix}.zip", "w", zipfile.ZIP_DEFLATED) as archive:
                for path in sorted(base.rglob("*")):
                    if path.is_file():
                        archive.write(path, path.relative_to(base))


if __name__ == "__main__":
    parser = argparse.ArgumentParser()
    parser.add_argument("--build", type=Path, required=True)
    parser.add_argument("--launcher", type=Path, required=True)
    parser.add_argument("--output", type=Path, default=Path("dist/releases"))
    parser.add_argument("--version", required=True)
    parser.add_argument("--platform", choices=["windows-x64", "linux-x64"], required=True)
    args = parser.parse_args()
    package(args.build, args.launcher, args.output, args.version, args.platform)
