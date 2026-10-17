#!/usr/bin/env python
#
# Builds the firmwares in src/. Without arguments every firmware is built,
# otherwise only the named ones, e.g. `python build_firmwares.py doppio`.
# `--libs` builds libDaisy and DaisySP, `--clean` removes the build output of
# all firmwares and both libraries.
#
import argparse
import os
import pathlib
import subprocess
import sys

ROOT = pathlib.Path(__file__).resolve().parent
SRC = ROOT / "src"
LIBS = [ROOT / "lib" / "libDaisy", ROOT / "lib" / "DaisySP"]
# Half the cores keeps the machine usable while the libraries compile.
JOBS = "-j{}".format(max(1, (os.cpu_count() or 2) // 2))


def firmwares():
    return sorted(path.parent for path in SRC.glob("*/Makefile"))


def make(folder, *targets):
    return subprocess.call(["make", "-s", *targets], cwd=folder)


def build_libs():
    for folder in LIBS:
        if not (folder / "Makefile").exists():
            print("{} is missing, run: git submodule update --init "
                  "--recursive".format(folder.relative_to(ROOT)))
            return 1
        print("Building: {}".format(folder.name))
        if make(folder, JOBS) != 0:
            return 1
    return 0


def clean():
    for folder in firmwares() + LIBS:
        make(folder, "clean")
    return 0


def build(names):
    available = {folder.name: folder for folder in firmwares()}
    unknown = [name for name in names if name not in available]
    if unknown:
        print("Unknown firmware: {}".format(", ".join(unknown)))
        print("Available: {}".format(", ".join(available)))
        return 1

    for name in names or available:
        print("Building: {}".format(name))
        folder = available[name]
        make(folder, "clean")
        if make(folder, JOBS) != 0:
            return 1
        binary = folder / "build" / "{}.bin".format(name)
        print("  -> {}".format(binary.relative_to(ROOT)))
    return 0


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("firmware", nargs="*", help="firmwares to build")
    parser.add_argument("--libs", action="store_true",
                        help="build libDaisy and DaisySP")
    parser.add_argument("--clean", action="store_true",
                        help="remove all build output")
    args = parser.parse_args()
    os.nice(10)
    sys.stdout.reconfigure(line_buffering=True)
    if args.clean:
        sys.exit(clean())
    if args.libs:
        sys.exit(build_libs())
    sys.exit(build(args.firmware))


if __name__ == "__main__":
    main()
