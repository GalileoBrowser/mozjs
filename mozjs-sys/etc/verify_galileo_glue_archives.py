#!/usr/bin/env python3
"""Verify Galileo's versioned glue contract in published mozjs archives."""

from __future__ import annotations

import argparse
import sys
import tarfile
from pathlib import Path


ABI_SYMBOL = b"GalileoMozjsGlueAbi_140_12_1"

REQUIRED_ARCHIVES = {
    "libmozjs-aarch64-apple-darwin.tar.gz",
    "libmozjs-aarch64-apple-darwin-debugmozjs-O3.tar.gz",
    "libmozjs-x86_64-apple-darwin.tar.gz",
    "libmozjs-x86_64-apple-darwin-debugmozjs-O3.tar.gz",
    "libmozjs-aarch64-unknown-linux-gnu.tar.gz",
    "libmozjs-aarch64-unknown-linux-gnu-debugmozjs-O3.tar.gz",
    "libmozjs-x86_64-unknown-linux-gnu.tar.gz",
    "libmozjs-x86_64-unknown-linux-gnu-debugmozjs-O3.tar.gz",
    "libmozjs-aarch64-pc-windows-msvc.tar.gz",
    "libmozjs-aarch64-pc-windows-msvc-debugmozjs-O3.tar.gz",
    "libmozjs-x86_64-pc-windows-msvc.tar.gz",
    "libmozjs-x86_64-pc-windows-msvc-debugmozjs-O3.tar.gz",
    "libmozjs-armv7-linux-androideabi.tar.gz",
    "libmozjs-aarch64-linux-android.tar.gz",
    "libmozjs-x86_64-linux-android.tar.gz",
    "libmozjs-aarch64-unknown-linux-ohos.tar.gz",
    "libmozjs-x86_64-unknown-linux-ohos.tar.gz",
}


def member_by_basename(archive: tarfile.TarFile, basename: str) -> tarfile.TarInfo:
    matches = [member for member in archive.getmembers() if Path(member.name).name == basename]
    if len(matches) != 1:
        raise ValueError(f"expected exactly one {basename}, found {len(matches)}")
    return matches[0]


def member_bytes(archive: tarfile.TarFile, member: tarfile.TarInfo) -> bytes:
    extracted = archive.extractfile(member)
    if extracted is None:
        raise ValueError(f"archive member {member.name} is not a regular file")
    return extracted.read()


def verify_archive(path: Path) -> None:
    with tarfile.open(path, mode="r:gz") as archive:
        bindings = member_bytes(archive, member_by_basename(archive, "gluebindings.rs"))
        if ABI_SYMBOL not in bindings:
            raise ValueError("gluebindings.rs lacks the Galileo ABI declaration")

        library_name = "jsglue.lib" if "windows-msvc" in path.name else "libjsglue.a"
        library = member_bytes(archive, member_by_basename(archive, library_name))
        if ABI_SYMBOL not in library:
            raise ValueError(f"{library_name} lacks the Galileo ABI link symbol")


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("directory", type=Path)
    parser.add_argument(
        "--require-release-matrix",
        action="store_true",
        help="require all supported desktop/mobile release archives",
    )
    args = parser.parse_args()

    archives = {path.name: path for path in args.directory.glob("libmozjs-*.tar.gz")}
    if args.require_release_matrix:
        missing = sorted(REQUIRED_ARCHIVES - archives.keys())
        if missing:
            print("missing required Galileo mozjs release assets:", file=sys.stderr)
            print("\n".join(f"  {name}" for name in missing), file=sys.stderr)
            return 1

    if not archives:
        print(f"no libmozjs archives found under {args.directory}", file=sys.stderr)
        return 1

    failed = False
    for name, path in sorted(archives.items()):
        try:
            verify_archive(path)
        except (OSError, tarfile.TarError, ValueError) as error:
            print(f"{name}: {error}", file=sys.stderr)
            failed = True
        else:
            print(f"{name}: Galileo glue ABI verified")
    return int(failed)


if __name__ == "__main__":
    raise SystemExit(main())
