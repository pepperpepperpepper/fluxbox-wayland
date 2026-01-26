#!/usr/bin/env python3

from __future__ import annotations

import argparse
import fnmatch
import os
import subprocess
import sys
from dataclasses import dataclass
from typing import Iterable


DEFAULT_EXTS = (
    ".c",
    ".cc",
    ".cpp",
    ".cxx",
    ".h",
    ".hh",
    ".hpp",
    ".hxx",
)


@dataclass(frozen=True)
class Offender:
    path: str
    code_lines: int


def is_in_worktree() -> bool:
    try:
        proc = subprocess.run(
            ["git", "rev-parse", "--is-inside-work-tree"],
            check=False,
            stdout=subprocess.PIPE,
            stderr=subprocess.DEVNULL,
            text=True,
        )
        return proc.returncode == 0 and proc.stdout.strip() == "true"
    except FileNotFoundError:
        return False


def list_tracked_files() -> list[str]:
    proc = subprocess.run(
        ["git", "ls-files", "-z"],
        check=True,
        stdout=subprocess.PIPE,
        text=False,
    )
    return [p.decode("utf-8", errors="surrogateescape") for p in proc.stdout.split(b"\0") if p]


def list_files_fallback(root: str) -> list[str]:
    out: list[str] = []
    for dirpath, _, filenames in os.walk(root):
        for name in filenames:
            out.append(os.path.relpath(os.path.join(dirpath, name), root))
    return out


def load_allowlist_patterns(path: str | None) -> list[str]:
    if not path:
        return []
    patterns: list[str] = []
    try:
        with open(path, "r", encoding="utf-8", errors="replace") as f:
            for raw in f:
                line = raw.strip()
                if not line or line.startswith("#"):
                    continue
                patterns.append(line)
    except FileNotFoundError:
        return []
    return patterns


def is_allowed(path: str, patterns: list[str]) -> bool:
    for pat in patterns:
        if fnmatch.fnmatch(path, pat):
            return True
    return False


def should_consider(path: str, exts: tuple[str, ...], exclude_prefixes: list[str]) -> bool:
    if not path.endswith(exts):
        return False
    for prefix in exclude_prefixes:
        if path.startswith(prefix):
            return False
    return True


def count_code_lines(path: str) -> int:
    """
    Very small C/C++ lexer to count "code lines":
    - ignore blank lines
    - ignore comment-only lines (// and /* ... */)
    - count a line if any token remains after comment stripping
    """
    code = 0
    in_block_comment = False
    in_string = False
    in_char = False
    escape = False

    with open(path, "r", errors="ignore") as fp:
        for raw_line in fp:
            line = raw_line.rstrip("\n")
            i = 0
            has_code = False
            while i < len(line):
                ch = line[i]
                nxt = line[i + 1] if i + 1 < len(line) else ""

                if in_block_comment:
                    if ch == "*" and nxt == "/":
                        in_block_comment = False
                        i += 2
                        continue
                    i += 1
                    continue

                if in_string:
                    if escape:
                        escape = False
                        i += 1
                        continue
                    if ch == "\\":
                        escape = True
                        i += 1
                        continue
                    if ch == '"':
                        in_string = False
                    has_code = True
                    i += 1
                    continue

                if in_char:
                    if escape:
                        escape = False
                        i += 1
                        continue
                    if ch == "\\":
                        escape = True
                        i += 1
                        continue
                    if ch == "'":
                        in_char = False
                    has_code = True
                    i += 1
                    continue

                # Not in a comment/string/char.
                if ch.isspace():
                    i += 1
                    continue

                # line comment begins
                if ch == "/" and nxt == "/":
                    break

                # block comment begins
                if ch == "/" and nxt == "*":
                    in_block_comment = True
                    i += 2
                    continue

                if ch == '"':
                    in_string = True
                    has_code = True
                    i += 1
                    continue

                if ch == "'":
                    in_char = True
                    has_code = True
                    i += 1
                    continue

                has_code = True
                i += 1

            if has_code:
                code += 1

    return code


def collect_offenders(
    files: Iterable[str],
    max_code_lines: int,
    exts: tuple[str, ...],
    exclude_prefixes: list[str],
) -> list[Offender]:
    offenders: list[Offender] = []
    for f in files:
        if not should_consider(f, exts=exts, exclude_prefixes=exclude_prefixes):
            continue
        try:
            n = count_code_lines(f)
        except FileNotFoundError:
            # ignore vanished files (race with build/cleanup)
            continue
        if n > max_code_lines:
            offenders.append(Offender(path=f, code_lines=n))
    offenders.sort(key=lambda o: (-o.code_lines, o.path))
    return offenders


def parse_args(argv: list[str]) -> argparse.Namespace:
    p = argparse.ArgumentParser(description="Fail if any code file exceeds a code-SLOC limit.")
    p.add_argument("--max", type=int, default=1100, help="Maximum allowed code lines (default: 1100)")
    p.add_argument(
        "--exclude-prefix",
        action="append",
        default=[],
        help="Exclude files under this prefix (repeatable)",
    )
    p.add_argument("--allowlist", default=None, help="Optional allowlist file (glob patterns)")
    p.add_argument(
        "--root",
        default=".",
        help="Repo root (default: .). Used for fallback scanning when git is unavailable.",
    )
    return p.parse_args(argv)


def main(argv: list[str]) -> int:
    args = parse_args(argv)

    root = os.path.abspath(args.root)
    os.chdir(root)

    exclude_prefixes = list(args.exclude_prefix) + ["src/wayland/protocol/"]

    allow_patterns = load_allowlist_patterns(args.allowlist)

    if is_in_worktree():
        files = list_tracked_files()
    else:
        files = list_files_fallback(root)

    offenders = collect_offenders(
        files=files,
        max_code_lines=args.max,
        exts=DEFAULT_EXTS,
        exclude_prefixes=exclude_prefixes,
    )

    hard: list[Offender] = []
    allowed: list[Offender] = []
    for o in offenders:
        if is_allowed(o.path, allow_patterns):
            allowed.append(o)
        else:
            hard.append(o)

    if hard:
        print(
            f"error: code files must stay <= {args.max} code lines "
            f"(excluding src/wayland/protocol/**)",
            file=sys.stderr,
        )
        print("error: offenders:", file=sys.stderr)
        for o in hard:
            print(f"{o.code_lines:6d} {o.path}", file=sys.stderr)
        if allowed:
            print("", file=sys.stderr)
            print("note: allowlisted offenders (remove as you refactor):", file=sys.stderr)
            for o in allowed:
                print(f"{o.code_lines:6d} {o.path}", file=sys.stderr)
        return 1

    if allowed:
        print(
            f"ok: no new monoliths (max={args.max} code lines); "
            f"{len(allowed)} allowlisted offender(s) remain"
        )
    else:
        print(f"ok: all code files are <= {args.max} code lines")
    return 0


if __name__ == "__main__":
    raise SystemExit(main(sys.argv[1:]))

