#!/usr/bin/env python
#
# recompiles all Make-based projects within the repository
#
import sys
import os
import subprocess
import pathlib


def main():
    cwd = os.getcwd()
    for file in sorted(pathlib.Path("src").rglob("Makefile")):
        folder = file.parents[0]
        os.chdir(folder)
        os.system("echo Building: {}".format(folder))
        subprocess.call("make -s clean", shell=True)
        exit_code = subprocess.call("make -s", shell=True)
        os.chdir(cwd)
        if exit_code != 0:
            sys.exit(1)


if __name__ == '__main__':
    main()
