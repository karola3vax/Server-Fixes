"""Packs the built plugin for a release: release/server-fixes-<VERSION>-linux.zip and its .sha256.

    python scripts/package.py

Run after a build (scripts/build-steamrt3.ps1). The zip unpacks straight into game/csgo: the binary from
dist/, the vdf and the default config from configs/ (dist keeps a config someone may have edited), with
LF line endings. Refuses a binary that does not carry the version in VERSION.
"""

import hashlib
import sys
import zipfile
from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent
BINARY = "addons/server_fixes/bin/server_fixes.so"
# Published path -> source file.
FILES = {
    "addons/metamod/server_fixes.vdf": ROOT / "configs/server_fixes.vdf",
    BINARY: ROOT / "dist" / BINARY,
    "addons/server_fixes/server_fixes.cfg": ROOT / "configs/server_fixes.cfg",
}


def main():
    version = (ROOT / "VERSION").read_text(encoding="utf-8").strip()
    missing = [str(source) for source in FILES.values() if not source.is_file()]
    if missing:
        sys.exit("missing (build first?): " + ", ".join(missing))
    if b"\0" + version.encode() + b"\0" not in FILES[BINARY].read_bytes():
        sys.exit(f"dist/{BINARY} is not version {version}: build again")

    out = ROOT / "release"
    out.mkdir(exist_ok=True)
    archive = out / f"server-fixes-{version}-linux.zip"
    with zipfile.ZipFile(archive, "w", zipfile.ZIP_DEFLATED) as zf:
        for name, source in FILES.items():
            data = source.read_bytes()
            if name != BINARY:
                data = data.replace(b"\r\n", b"\n")  # a Windows checkout has CRLF; the server is Linux
            # Fixed timestamp and mode: the same build gives the same zip.
            info = zipfile.ZipInfo(name, date_time=(2026, 1, 1, 0, 0, 0))
            info.external_attr = 0o644 << 16
            info.compress_type = zipfile.ZIP_DEFLATED
            zf.writestr(info, data)
    digest = hashlib.sha256(archive.read_bytes()).hexdigest()
    (out / (archive.name + ".sha256")).write_text(f"{digest}  {archive.name}\n", encoding="utf-8")
    print(f"{archive.relative_to(ROOT)}  {archive.stat().st_size} bytes  sha256 {digest}")


if __name__ == "__main__":
    main()
