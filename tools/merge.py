#!/usr/bin/env python3
"""
Single-file merger: resolves local #include "..." directives recursively,
topologically sorts, and outputs a single .cpp file.
Also copies to clipboard (xclip or wl-copy).
"""

import os
import re
import sys
import subprocess
import shutil
from pathlib import Path
from collections import defaultdict

SRC_DIR = Path(__file__).parent.parent / "src"
OUTPUT = Path(__file__).parent.parent / "merged" / "merged.cpp"

INCLUDE_RE = re.compile(r'^\s*#include\s+"([^"]+)"')
PRAGMA_ONCE_RE = re.compile(r'^\s*#pragma\s+once')


def find_file(name, base_dir):
    """Find an include file relative to base_dir or SRC_DIR."""
    candidate = base_dir / name
    if candidate.exists():
        return candidate.resolve()
    candidate = SRC_DIR / name
    if candidate.exists():
        return candidate.resolve()
    return None


def parse_file(filepath, visited, order, contents, base_dir=None):
    """Recursively parse a file, resolving local includes."""
    filepath = filepath.resolve()
    if filepath in visited:
        return
    visited.add(filepath)

    if base_dir is None:
        base_dir = filepath.parent

    lines = filepath.read_text().splitlines()
    file_lines = []

    for line in lines:
        if PRAGMA_ONCE_RE.match(line):
            continue

        m = INCLUDE_RE.match(line)
        if m:
            inc_name = m.group(1)
            inc_path = find_file(inc_name, filepath.parent)
            if inc_path:
                parse_file(inc_path, visited, order, contents, base_dir)
                # Also pull in the corresponding .cpp if this is a .hpp/.h
                if inc_path.suffix in ('.hpp', '.h'):
                    for ext in ('.cpp', '.cc', '.cxx'):
                        cpp_path = inc_path.with_suffix(ext)
                        if cpp_path.exists():
                            parse_file(cpp_path, visited, order, contents, base_dir)
                            break
                continue
            # If not found locally, keep the include as-is (might be system-like)

        file_lines.append(line)

    contents[filepath] = file_lines
    order.append(filepath)


def copy_to_clipboard(text):
    """Copy text to clipboard, auto-detecting X11 vs Wayland."""
    session = os.environ.get("XDG_SESSION_TYPE", "")
    wayland = os.environ.get("WAYLAND_DISPLAY", "")

    if wayland or session == "wayland":
        cmd = "wl-copy"
    else:
        cmd = "xclip"

    if not shutil.which(cmd):
        if cmd == "xclip" and shutil.which("xsel"):
            cmd = "xsel"
        else:
            print(f"Warning: {cmd} not found, skipping clipboard copy")
            return False

    try:
        if cmd == "xclip":
            proc = subprocess.run(
                ["xclip", "-selection", "clipboard"],
                input=text, text=True, timeout=5
            )
        elif cmd == "xsel":
            proc = subprocess.run(
                ["xsel", "--clipboard", "--input"],
                input=text, text=True, timeout=5
            )
        else:
            proc = subprocess.run(
                ["wl-copy"], input=text, text=True, timeout=5
            )
        return proc.returncode == 0
    except Exception as e:
        print(f"Warning: clipboard copy failed: {e}")
        return False


def merge():
    entry = SRC_DIR / "main.cpp"
    if not entry.exists():
        print(f"Error: {entry} not found", file=sys.stderr)
        sys.exit(1)

    visited = set()
    order = []
    contents = {}

    parse_file(entry, visited, order, contents)

    # Build merged output
    merged_lines = []
    merged_lines.append("// Auto-generated merged file — do not edit directly")
    merged_lines.append('#pragma GCC optimize("-O3")')
    merged_lines.append('#pragma GCC optimize("inline")')
    merged_lines.append('#pragma GCC optimize("omit-frame-pointer")')
    merged_lines.append('#pragma GCC optimize("unroll-loops")')
    merged_lines.append("")

    for filepath in order:
        merged_lines.append(f"// ===== {filepath.relative_to(SRC_DIR)} =====")
        merged_lines.extend(contents[filepath])
        merged_lines.append("")

    merged_text = "\n".join(merged_lines)

    OUTPUT.parent.mkdir(parents=True, exist_ok=True)
    OUTPUT.write_text(merged_text)
    print(f"Merged to {OUTPUT}")

    if copy_to_clipboard(merged_text):
        print("Copied to clipboard")


if __name__ == "__main__":
    merge()
