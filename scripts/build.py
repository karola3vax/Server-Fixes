"""Run the gate tests, fetch the pinned dependencies, build the plugin with AMBuild and stage dist/addons.

Linux only (the plugin is for Linux servers); scripts/build-steamrt3.ps1 runs this inside Valve's SteamRT3 image.
"""

from __future__ import annotations

import argparse
import os
import shutil
import subprocess
import sys
from pathlib import Path


ROOT = Path(__file__).resolve().parents[1]


def run(*args: str, cwd: Path = ROOT, env: dict[str, str] | None = None) -> None:
  subprocess.run(args, cwd=cwd, env=env, check=True)


def gate_tests(build: Path) -> None:
  """src/core has no SDK dependency; its tests build with the plain compiler and must pass first."""
  binary = build / "test_core"
  sources = ["tests/test_core.cpp", "src/core/config.cpp", "src/core/pattern.cpp", "src/core/rules.cpp"]
  run("g++", "-std=c++20", "-O1", "-Wall", "-Wextra", "-Werror", "-Isrc", *sources, "-o", str(binary))
  run(str(binary))


def main() -> None:
  parser = argparse.ArgumentParser()
  parser.add_argument("--debug", action="store_true")
  parser.add_argument("--tests-only", action="store_true", help="run the gate tests and stop")
  args = parser.parse_args()

  build = ROOT / "build-linux"
  build.mkdir(exist_ok=True)
  gate_tests(build)
  if args.tests_only:
    return

  python = sys.executable
  dependencies = ROOT / ".build-deps" / "linux"
  run(python, str(ROOT / "scripts" / "bootstrap.py"))
  env = os.environ.copy()
  env["PYTHONPATH"] = str(dependencies / "ambuild")
  env.update(CC="gcc", CXX="g++")
  configure = [python, str(ROOT / "configure.py"),
    "--hl2sdk-root", str(dependencies),
    "--hl2sdk-manifests", str(dependencies / "hl2sdk-manifests"),
    "--mms-path", str(dependencies / "metamod-source")]
  if args.debug:
    configure.append("--enable-debug")
  run(*configure, cwd=build, env=env)
  run(python, "-c", "from ambuild2.run import cli_run; cli_run()", cwd=build, env=env)

  binary = build / "server_fixes" / "linux-x86_64" / "server_fixes.so"
  if not binary.is_file() or binary.stat().st_size == 0:
    raise SystemExit(f"plugin binary was not produced: {binary}")
  # Release layout: copy dist/addons into game/csgo/addons. Metamod adds the extension.
  stage = ROOT / "dist" / "addons"
  (stage / "server_fixes" / "bin").mkdir(parents=True, exist_ok=True)
  (stage / "metamod").mkdir(parents=True, exist_ok=True)
  shutil.copy2(binary, stage / "server_fixes" / "bin" / "server_fixes.so")
  shutil.copy2(ROOT / "configs" / "server_fixes.vdf", stage / "metamod" / "server_fixes.vdf")
  config = stage / "server_fixes" / "server_fixes.cfg"
  if not config.exists():  # never overwrite a staged config someone edited
    shutil.copy2(ROOT / "configs" / "server_fixes.cfg", config)
  print(f"Built {binary}")


if __name__ == "__main__":
  main()
