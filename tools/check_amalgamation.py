#!/usr/bin/env python3
"""Fails when the Unreal module's single translation unit and the source tree disagree.

The engine host compiles the simulation through one file that includes each
source in turn (docs/decisions/0010-unreal-host.md). Three lists have to stay
equal: the sources on disk, the ones CMakeLists.txt compiles, and the ones that
file includes. A source added to two of the three builds in this repository and
fails in the engine, or the reverse, and neither failure is obvious. This check
is what makes the disagreement loud and immediate.
"""
import re
import sys
from pathlib import Path

REPO = Path(__file__).resolve().parent.parent
AMALGAMATION = REPO / "hosts/unreal/Source/ExpansionUE/Private/ExpansionAmalgamated.cpp"
# hostfs is excluded by design: a packaged build has no directory to read.
SOURCE_DIRS = ["core/src", "persistence/src", "presentation/src"]
CMAKE_VARS = [
    "EXPANSION_CORE_SOURCES",
    "EXPANSION_PERSISTENCE_SOURCES",
    "EXPANSION_PRESENTATION_SOURCES",
]


def on_disk():
    found = set()
    for directory in SOURCE_DIRS:
        for path in sorted((REPO / directory).glob("*.cpp")):
            found.add(f"{directory}/{path.name}")
    return found


def in_cmake():
    text = (REPO / "CMakeLists.txt").read_text()
    listed = set()
    for var in CMAKE_VARS:
        match = re.search(r"set\(" + var + r"\n(.*?)\n\)", text, re.S)
        if match is None:
            sys.exit(f"CMakeLists.txt no longer defines {var}")
        for line in match.group(1).splitlines():
            line = line.strip()
            if line:
                listed.add(line)
    return listed


def in_amalgamation():
    text = AMALGAMATION.read_text()
    return set(re.findall(r'^#include "([^"]+\.cpp)"', text, re.M))


def main():
    disk, cmake, unreal = on_disk(), in_cmake(), in_amalgamation()
    problems = []

    def report(name, left, right, other_name):
        missing = sorted(left - right)
        if missing:
            problems.append(
                f"{other_name} is missing {len(missing)} source(s) that {name} has:\n"
                + "".join(f"    {m}\n" for m in missing)
            )

    report("the source tree", disk, cmake, "CMakeLists.txt")
    report("the source tree", disk, unreal, AMALGAMATION.name)
    report("CMakeLists.txt", cmake, disk, "the source tree")
    report("CMakeLists.txt", cmake, unreal, AMALGAMATION.name)
    report(AMALGAMATION.name, unreal, disk, "the source tree")
    report(AMALGAMATION.name, unreal, cmake, "CMakeLists.txt")

    if problems:
        sys.stderr.write(
            "The simulation's source lists disagree. Every source has to appear in "
            "the tree, in CMakeLists.txt, and in the Unreal module's translation "
            "unit.\n\n" + "\n".join(problems)
        )
        return 1
    print(f"{len(disk)} simulation sources, listed consistently in all three places.")
    return 0


if __name__ == "__main__":
    sys.exit(main())
