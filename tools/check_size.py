#!/usr/bin/env python3
"""Check linked AVR sizes; this does not measure peak runtime stack use."""

import argparse
import subprocess
from pathlib import Path


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("elf")
    parser.add_argument("--cli", default="arduino-cli")
    args = parser.parse_args()

    properties = subprocess.check_output(
        [args.cli, "compile", "--fqbn", "arduino:avr:leonardo",
         "--show-properties=expanded", "GarbageCollector"], text=True
    )
    compiler_path = next(
        line.partition("=")[2] for line in properties.splitlines()
        if line.startswith("compiler.path=")
    )
    output = subprocess.check_output(
        [str(Path(compiler_path) / "avr-size"), "-A", args.elf], text=True
    )
    sections = {}
    for line in output.splitlines():
        fields = line.split()
        if len(fields) >= 2 and fields[0].startswith("."):
            sections[fields[0]] = int(fields[1])

    flash = sections[".text"] + sections.get(".data", 0)
    static_ram = sum(sections.get(name, 0) for name in (".data", ".bss", ".noinit"))
    print(f"Flash: {flash} / 28672 bytes (28 KiB jam limit)")
    print(f"Static SRAM: {static_ram} / 1400 bytes (prototype budget)")
    print(f"Headroom below 2048-byte jam RAM limit: {2048 - static_ram} bytes")
    print("Runtime stack and interrupt usage are NOT included; verify on hardware.")
    if flash > 28672 or static_ram > 1400:
        raise SystemExit("Memory budget exceeded")


if __name__ == "__main__":
    main()
