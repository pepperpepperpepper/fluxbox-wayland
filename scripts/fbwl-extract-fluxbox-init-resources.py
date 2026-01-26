#!/usr/bin/env python3
from __future__ import annotations

import argparse
import csv
import re
from dataclasses import dataclass
from pathlib import Path
from typing import Dict, Iterable, List, Optional, Set, Tuple


@dataclass(frozen=True)
class ResourceRow:
    key: str
    rtype: str
    scope: str
    source: str


def _split_top_level(s: str, sep: str) -> List[str]:
    parts: List[str] = []
    depth = 0
    start = 0
    for i, ch in enumerate(s):
        if ch == "{":
            depth += 1
        elif ch == "}":
            depth = max(0, depth - 1)
        elif ch == sep and depth == 0:
            parts.append(s[start:i])
            start = i + 1
    parts.append(s[start:])
    return parts


def _brace_expand(pattern: str) -> List[str]:
    start = pattern.find("{")
    if start < 0:
        return [pattern]

    depth = 0
    end = -1
    for i in range(start, len(pattern)):
        ch = pattern[i]
        if ch == "{":
            depth += 1
        elif ch == "}":
            depth -= 1
            if depth == 0:
                end = i
                break

    if end < 0:
        return [pattern]

    prefix = pattern[:start]
    inner = pattern[start + 1 : end]
    suffix = pattern[end + 1 :]

    expanded: List[str] = []
    for option in _split_top_level(inner, "|"):
        for tail in _brace_expand(suffix):
            expanded.extend(_brace_expand(prefix + option + tail))
    return expanded


def _clean_manpage_token(s: str) -> str:
    # In the generated manpage, resource names are written with \& between tokens.
    return s.replace("\\&", "").strip()


def _normalize_key(key: str) -> str:
    key = key.strip()
    key = key.replace("session.screen0.", "session.screenN.")
    key = re.sub(r"^session\.screen0$", "session.screenN", key)
    return key


def _scope_for_key(key: str) -> str:
    return "screen" if key.startswith("session.screenN.") else "global"


def _extract_from_manpage(manpage_text: str) -> Iterable[Tuple[str, str]]:
    # Extract from the RESOURCES section only, and keep parsing strictly line-based
    # so we don't accidentally span multiple entries.
    start = manpage_text.find('\n.SH "RESOURCES"')
    if start < 0:
        start = 0
    end = manpage_text.find("\n.SH", start + 1)
    resources_text = manpage_text[start:end] if end > 0 else manpage_text[start:]

    for line in resources_text.splitlines():
        if not line.startswith(r"\fBsession"):
            continue
        m = re.match(r"\\fB(session[^\n]*?)\\fR:\s*(.*)$", line)
        if not m:
            continue

        raw_key = m.group(1)
        rest = m.group(2)

        rtype = "unknown"
        m_type = re.search(r"\\fI(.*?)\\fR", rest)
        if m_type:
            rtype = _clean_manpage_token(m_type.group(1))
        elif r"\fB" in rest:
            # Typically used for enums, e.g. focusModel / rowPlacementDirection.
            rtype = "enum"

        key = _clean_manpage_token(raw_key).strip('"')
        for expanded in _brace_expand(key):
            yield _normalize_key(expanded), rtype


def _extract_literal_session_keys(src_root: Path) -> Set[str]:
    # Find literal "session.foo" occurrences in C/C++ sources (excluding tests and generated protocol code).
    keys: Set[str] = set()
    file_globs = ["*.c", "*.cc", "*.cpp", "*.h", "*.hh", "*.hpp"]
    exclude_parts = {
        str(Path("src") / "tests"),
        str(Path("src") / "wayland" / "protocol"),
    }

    for glob in file_globs:
        for path in src_root.rglob(glob):
            p = str(path)
            if any(part in p for part in exclude_parts):
                continue
            try:
                text = path.read_text(encoding="utf-8", errors="replace")
            except OSError:
                continue
            for m in re.finditer(r"\"(session\.[A-Za-z0-9_.]+)\"", text):
                key = m.group(1)
                # Ignore alt-resource prefixes/variants and screen-name prefixes.
                if re.match(r"^session\.[A-Z]", key):
                    continue
                if key in {"session.screen"}:
                    continue
                keys.add(key)
    return keys


def _extract_screen_concat_keys(src_root: Path) -> Set[str]:
    # Extract screen-scoped init resources that are typically constructed as:
    #   screen.name() + ".foo"
    #   scrn.name() + ".bar.baz"
    #   scrname + ".opaqueMove"        (ScreenResource.cc)
    # and (some) dynamic keys like:
    #   screen.name() + ".struts." + area
    #   screen.name() + ".toolbar." + name + ".label"
    keys: Set[str] = set()

    file_globs = ["*.c", "*.cc", "*.cpp", "*.h", "*.hh", "*.hpp"]
    exclude_parts = {
        str(Path("src") / "tests"),
        str(Path("src") / "wayland" / "protocol"),
    }

    rx_scrname_suffix = re.compile(r"\bscrname\s*\+\s*\"(\.[A-Za-z0-9_.-]+)\"")
    rx_name_suffix = re.compile(r"\bname\(\)\s*\+\s*\"(\.[A-Za-z0-9_.-]+)\"")
    rx_name_prefix_var_suffix = re.compile(
        r"\bname\(\)\s*\+\s*\"(\.[A-Za-z0-9_.-]*\.)\"\s*\+\s*([A-Za-z_][A-Za-z0-9_]*)\s*\+\s*\"(\.[A-Za-z0-9_.-]+)\""
    )
    rx_name_prefix_var = re.compile(
        r"\bname\(\)\s*\+\s*\"(\.[A-Za-z0-9_.-]*\.)\"\s*\+\s*([A-Za-z_][A-Za-z0-9_]*)\s*(?!\+\s*\")"
    )

    for glob in file_globs:
        for path in src_root.rglob(glob):
            p = str(path)
            if any(part in p for part in exclude_parts):
                continue
            try:
                text = path.read_text(encoding="utf-8", errors="replace")
            except OSError:
                continue

            for m in rx_scrname_suffix.finditer(text):
                suffix = m.group(1)
                if suffix.endswith("."):
                    continue
                keys.add("session.screenN" + suffix)
            for m in rx_name_suffix.finditer(text):
                suffix = m.group(1)
                # Skip prefixes used only for further concatenation, e.g. ".struts.".
                if suffix.endswith("."):
                    continue
                key = "session.screenN" + suffix
                if re.match(r"^session\.screenN\.[A-Z]", key):
                    continue
                keys.add(key)
            for m in rx_name_prefix_var_suffix.finditer(text):
                prefix, var, suffix = m.groups()
                if prefix == ".toolbar." and var == "name" and suffix in {".label", ".commands"}:
                    keys.add(f"session.screenN.toolbar.button.<name>{suffix}")
                    continue
                key = f"session.screenN{prefix}<{var}>{suffix}"
                if re.match(r"^session\.screenN\.[A-Z]", key):
                    continue
                keys.add(key)
            for m in rx_name_prefix_var.finditer(text):
                prefix, var = m.groups()
                if prefix == ".toolbar." and var == "name":
                    continue
                key = f"session.screenN{prefix}<{var}>"
                if re.match(r"^session\.screenN\.[A-Z]", key):
                    continue
                keys.add(key)

    return keys


def _merge_resources(
    doc_rows: Dict[str, str],
    code_keys: Set[str],
    extra_rows: Dict[str, str],
) -> List[ResourceRow]:
    # Prefer doc types when available.
    normalized_code_keys = {_normalize_key(k) for k in code_keys}
    all_keys = set(doc_rows.keys()) | normalized_code_keys | set(extra_rows.keys())

    merged: List[ResourceRow] = []
    for key in sorted(all_keys):
        rtype = doc_rows.get(key) or extra_rows.get(key) or "unknown"

        in_doc = key in doc_rows
        in_code = key in normalized_code_keys or key in extra_rows
        if in_doc and in_code:
            source = "doc+code"
        elif in_doc:
            source = "doc"
        else:
            source = "code"

        merged.append(ResourceRow(key=key, rtype=rtype, scope=_scope_for_key(key), source=source))
    return merged


def _write_tsv(rows: List[ResourceRow], out_path: Optional[Path]) -> None:
    out_file = out_path.open("w", encoding="utf-8", newline="") if out_path else None
    try:
        writer = csv.writer(out_file or __import__("sys").stdout, delimiter="\t", lineterminator="\n")
        writer.writerow(["key", "type", "scope", "source"])
        for row in rows:
            writer.writerow([row.key, row.rtype, row.scope, row.source])
    finally:
        if out_file:
            out_file.close()


def main() -> int:
    parser = argparse.ArgumentParser(
        description="Extract classic Fluxbox init resources into a machine-readable list (TSV)."
    )
    parser.add_argument(
        "--manpage",
        default=str(Path("doc") / "fluxbox.1"),
        help="Path to generated fluxbox(1) man page (default: doc/fluxbox.1)",
    )
    parser.add_argument(
        "--src-root",
        default="src",
        help="Source root to scan for literal session.* keys (default: src)",
    )
    parser.add_argument(
        "--out",
        default=str(Path("doc") / "fluxbox-init-resources.tsv"),
        help="Output TSV path (default: doc/fluxbox-init-resources.tsv). Use '-' for stdout.",
    )

    args = parser.parse_args()

    manpage_path = Path(args.manpage)
    src_root = Path(args.src_root)

    manpage_text = manpage_path.read_text(encoding="utf-8", errors="replace")
    doc_rows: Dict[str, str] = {}
    for key, rtype in _extract_from_manpage(manpage_text):
        # Keep first-seen doc type to avoid accidental type churn.
        doc_rows.setdefault(key, rtype)

    code_keys = _extract_literal_session_keys(src_root) | _extract_screen_concat_keys(src_root)

    # Known "code-only" or dynamic keys not documented in fluxbox(1).
    extra_rows: Dict[str, str] = {
        "session.configVersion": "integer",
        "session.screenN.toolbar.button.<name>.label": "string",
        "session.screenN.toolbar.button.<name>.commands": "string",
    }

    rows = _merge_resources(doc_rows=doc_rows, code_keys=code_keys, extra_rows=extra_rows)

    out_path = None if args.out == "-" else Path(args.out)
    _write_tsv(rows, out_path=out_path)
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
