#!/usr/bin/env python3
"""Wraps the built FlowerMachine.exe in the header the installer expects.

The installer carries FlowerMachine.exe as a Win32 RCDATA resource rather than as a
Projucer BinaryData array: 7 MB of C source is a ~26 MB translation unit that MSVC
has to compile on every build, while the resource compiler simply reads the file.

SizeofResource reports the padded resource size, not the payload size, so the blob
carries its own length and a checksum in front of it.

    python Installer/pack-payload.py [--config Release]

Run it after building FlowerMachine and before building the installer.
"""

import argparse
import pathlib
import struct
import sys

MAGIC = 0x464D5031  # "FMP1"


def fnv1a(data: bytes) -> int:
    """Must match payloadChecksum() in Installer/Source/Payload.cpp."""
    h = 2166136261
    for byte in data:
        h = ((h ^ byte) * 16777619) & 0xFFFFFFFF
    return h


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--config", default="Release", choices=["Release", "Debug"])
    args = parser.parse_args()

    root = pathlib.Path(__file__).resolve().parent.parent
    source = root / "Builds" / "VisualStudio2022" / "x64" / args.config / "App" / "FlowerMachine.exe"
    destination = root / "Installer" / "Payload" / "FlowerMachine_payload.bin"

    if not source.is_file():
        print(f"error: {source} is not there. Build FlowerMachine {args.config} first.", file=sys.stderr)
        return 1

    payload = source.read_bytes()
    checksum = fnv1a(payload)

    destination.parent.mkdir(parents=True, exist_ok=True)
    with destination.open("wb") as out:
        out.write(struct.pack("<IqI", MAGIC, len(payload), checksum))
        out.write(payload)

    # rc.exe only reads the .bin; touching the .rc makes MSBuild notice a changed payload.
    rc = root / "Installer" / "Payload" / "payload.rc"
    if rc.is_file():
        rc.touch()

    print(f"packed {len(payload):,} bytes from {args.config} -> {destination.name} "
          f"(checksum {checksum:#010x})")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
