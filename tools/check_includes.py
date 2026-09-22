#!/usr/bin/env python3
"""Fails when a file uses a standard-library name without including its header.

This exists because of a real failure rather than a hypothetical one.
`test_host_api.cpp` called `std::reverse` without including `<algorithm>`. It
compiled with GCC 13 and with Clang 18, because both pull `<algorithm>` in
transitively through some other header, and it failed on continuous
integration, whose Clang ships a libstdc++ that does not. Two green compilers
locally were not evidence of anything.

Transitive includes are not a contract. A compiler upgrade, a standard-library
upgrade, or a different platform can withdraw one at any time, and the failure
lands wherever the code is next built -- which, for this repository, is an engine
nobody can run here.

This is a heuristic, not a full include-what-you-use pass: it checks the names
this codebase actually uses, by the header that owns them. A name it does not
know about is not checked. Adding one is a line in KNOWN.
"""
import re
import sys
from pathlib import Path

REPO = Path(__file__).resolve().parent.parent
SEARCH_DIRS = ["core", "persistence", "presentation", "hostfs", "cli", "tests"]

# std name -> the header that declares it. Only names this repository uses.
KNOWN = {
    "<algorithm>": [
        "sort", "stable_sort", "reverse", "unique", "find", "find_if", "count",
        "count_if", "any_of", "all_of", "none_of", "copy", "fill", "remove",
        "remove_if", "lower_bound", "upper_bound", "max_element", "min_element",
        "clamp", "swap_ranges", "rotate", "partition", "accumulate_not_here",
    ],
    "<numeric>": ["accumulate", "iota", "reduce"],
    "<cmath>": ["fabs", "llround", "lround", "nan", "isfinite", "isnan", "floor", "ceil", "pow", "sqrt"],
    "<cstdint>": ["int64_t", "uint64_t", "int32_t", "uint32_t", "int8_t", "uint8_t", "size_t_not_here"],
    "<cstddef>": ["size_t", "ptrdiff_t", "byte"],
    "<string>": ["string", "to_string", "stoul", "stoll", "stoi"],
    "<string_view>": ["string_view"],
    "<vector>": ["vector"],
    "<map>": ["map", "multimap"],
    "<set>": ["set", "multiset"],
    "<memory>": ["unique_ptr", "shared_ptr", "make_unique", "make_shared", "weak_ptr"],
    "<optional>": ["optional", "nullopt"],
    "<functional>": ["function", "ref", "cref"],
    "<utility>": ["move", "forward", "pair", "make_pair", "swap", "exchange"],
    "<limits>": ["numeric_limits"],
    "<stdexcept>": ["runtime_error", "logic_error", "out_of_range", "invalid_argument"],
    "<sstream>": ["ostringstream", "istringstream", "stringstream"],
    "<iostream>": ["cout", "cerr", "cin"],
    "<iomanip>": ["setw", "setprecision", "setfill"],
    "<chrono>": ["chrono"],
    "<type_traits>": ["is_enum_v", "is_same_v", "enable_if_t", "decay_t", "is_integral_v"],
    "<filesystem>": ["filesystem"],
    "<fstream>": ["ifstream", "ofstream", "fstream"],
    "<cstdlib>": ["getenv", "strtoll", "strtoull", "abort", "exit"],
    "<cstring>": ["memcpy", "memset", "strlen", "strcmp"],
    "<array>": ["array"],
    "<initializer_list>": ["initializer_list"],
}

# Some names in the table above are deliberately unmatchable placeholders, kept so
# a reader can see the header was considered. Drop them.
for header, names in KNOWN.items():
    KNOWN[header] = [n for n in names if not n.endswith("_not_here")]


# Where a project header can be found. A file that includes a project header may
# rely on the system headers that header includes, transitively: that is an
# ordinary contract between two files in one repository. What it may not rely on
# is a system header arriving through another system header's internals, which is
# the thing that varies between standard libraries.
INCLUDE_ROOTS = [
    "core/include", "persistence/include", "presentation/include", "hostfs/include",
    "core/src", "tests", "cli",
]


def includes_of(text):
    system = set(re.findall(r'^\s*#\s*include\s*(<[^>]+>)', text, re.M))
    project = set(re.findall(r'^\s*#\s*include\s*"([^"]+)"', text, re.M))
    return system, project


def resolve(project_include, from_path):
    """Finds a quoted include on disk, as the compiler would."""
    beside = from_path.parent / project_include
    if beside.is_file():
        return beside
    for root in INCLUDE_ROOTS:
        candidate = REPO / root / project_include
        if candidate.is_file():
            return candidate
    return None


def available_system_headers(path, seen=None):
    """Every system header this file gets, itself or through a project header."""
    if seen is None:
        seen = set()
    resolved = path.resolve()
    if resolved in seen:
        return set()
    seen.add(resolved)
    try:
        text = path.read_text()
    except OSError:
        return set()
    system, project = includes_of(text)
    for include in project:
        found = resolve(include, path)
        if found is not None:
            system |= available_system_headers(found, seen)
    return system


def strip_noise(text):
    """Removes comments and string literals so a mention in prose is not a use."""
    text = re.sub(r'/\*.*?\*/', ' ', text, flags=re.S)
    text = re.sub(r'//[^\n]*', ' ', text)
    text = re.sub(r'"(?:[^"\\\n]|\\.)*"', '""', text)
    return text


def main():
    problems = []
    checked = 0
    for directory in SEARCH_DIRS:
        for path in sorted((REPO / directory).rglob("*")):
            if path.suffix not in (".cpp", ".hpp", ".h"):
                continue
            raw = path.read_text()
            code = strip_noise(raw)
            present = available_system_headers(path)
            checked += 1
            for header, names in KNOWN.items():
                if header in present:
                    continue
                for name in names:
                    if re.search(r'\bstd::' + re.escape(name) + r'\b', code):
                        problems.append(
                            f"{path.relative_to(REPO)}: uses std::{name} without {header}"
                        )
                        break

    if problems:
        sys.stderr.write(
            "A standard-library name is used without any included header declaring "
            "it -- not the file's own includes, and not a project header it pulls "
            "in. It may still compile through one standard header including "
            "another, which is not a contract: it breaks on a different compiler or "
            "standard library, as std::reverse did.\n\n"
            + "".join(f"  {p}\n" for p in sorted(problems))
        )
        return 1
    print(f"{checked} files: every standard-library name this checker knows is declared by an "
          f"included header.")
    return 0


if __name__ == "__main__":
    sys.exit(main())
