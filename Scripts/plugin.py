#!/usr/bin/env python3
# SPDX-FileCopyrightText: 2026 Pascal Klose
# SPDX-License-Identifier: GPL-3.0-or-later

"""The SCS telemetry plugin: cross-compile it for Windows and put it where the game loads plugins from.

    python3 Scripts/plugin.py build      configure with the mingw-w64 toolchain and build build-plugin/wheelstar_telemetry.dll
    python3 Scripts/plugin.py install    build, then copy the DLL to <game>/bin/win_x64/plugins/
    python3 Scripts/plugin.py remove     delete it from there again

The game's folder is `[plugin] game_directory` in Scripts/project.toml; WHEELSTAR_GAME_DIR overrides it. Steam runs
the Windows build of the game through Proton, which is why this is a Windows DLL and not a Linux library.
"""

import argparse
import os
import shutil
import subprocess
import sys
import tomllib
from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent
BUILD_DIRECTORY = ROOT / "build-plugin"
DLL = BUILD_DIRECTORY / "wheelstar_telemetry.dll"
PLUGIN_NAME = DLL.name


def refuse(message: str) -> None:
    print(message, file=sys.stderr)
    sys.exit(1)


def run(*command: str) -> None:
    if subprocess.run(command, cwd=ROOT, check=False).returncode != 0:
        refuse("failed: " + " ".join(command))


def build() -> None:
    if shutil.which("x86_64-w64-mingw32-g++") is None:
        refuse("x86_64-w64-mingw32-g++ is missing: install the mingw64-gcc-c++ package")

    # Ninja when it is installed, like the application's own build; not every machine has make.
    generator = ["-G", "Ninja"] if shutil.which("ninja") else []
    toolchain = ROOT / "plugin" / "mingw-w64.cmake"
    run("cmake", "-S", str(ROOT / "plugin"), "-B", str(BUILD_DIRECTORY), f"-DCMAKE_TOOLCHAIN_FILE={toolchain}", *generator)
    run("cmake", "--build", str(BUILD_DIRECTORY))

    if not DLL.is_file():
        refuse(f"the build produced no {PLUGIN_NAME}")

    print(f"built {DLL.relative_to(ROOT)}")


def plugin_directory() -> Path:
    configured = os.environ.get("WHEELSTAR_GAME_DIR")

    if not configured:
        with open(ROOT / "Scripts" / "project.toml", "rb") as handle:
            configured = tomllib.load(handle).get("plugin", {}).get("game_directory")

    if not configured:
        refuse("no game directory: set [plugin] game_directory in Scripts/project.toml or WHEELSTAR_GAME_DIR")

    game = Path(configured).expanduser()
    binaries = game / "bin" / "win_x64"

    if not binaries.is_dir():
        refuse(f"{binaries} does not exist: is {game} the game's folder, and the Windows build?")

    return binaries / "plugins"


def install() -> None:
    build()
    directory = plugin_directory()
    directory.mkdir(exist_ok=True)
    shutil.copy2(DLL, directory / PLUGIN_NAME)
    # Without the package's marker, istar-wheelstar-ets2-plugin link leaves this build alone instead of replacing it.
    (directory / ".wheelstar_telemetry.package").unlink(missing_ok=True)
    print(f"installed {directory / PLUGIN_NAME}")


def remove() -> None:
    target = plugin_directory() / PLUGIN_NAME

    if not target.is_file():
        refuse(f"{target} is not there")

    target.unlink()
    print(f"removed {target}")


def main() -> None:
    parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument("command", choices=["build", "install", "remove"])
    arguments = parser.parse_args()
    {"build": build, "install": install, "remove": remove}[arguments.command]()


if __name__ == "__main__":
    main()
