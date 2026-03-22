#!/usr/bin/env python3
import os
import re
import json

SOURCE_DIR = os.path.join("src", "ui", "gui")
OUTPUT_FILE = os.path.join("lang", "en.json")

IGNORED_PREFIXES = (
    "/", "\\",
    "sysclk", "hocclk", "horizonoc",
    "\\u",
)


def extract_strings_from_file(filepath: str) -> list[str]:
    with open(filepath, "r", encoding="utf-8", errors="ignore") as f:
        content = f.read()

    pattern = r'"((?:[^"\\]|\\.)*)"'
    return re.findall(pattern, content)


def should_include(s: str) -> bool:
    if not s or s.isspace():
        return False

    stripped = s.strip()
    lower = stripped.lower()

    for prefix in IGNORED_PREFIXES:
        if s.startswith(prefix):
            return False

    if re.match(r"^\\u[0-9a-fA-F]", s):
        return False

    if re.fullmatch(r"[a-zA-Z0-9_./\\-]+\.(h|hpp|cpp|c)", stripped):
        return False

    if re.fullmatch(r"[%\d.*\-+lfdsuxXpLh ]*", stripped) and "%" in stripped:
        return False

    if re.fullmatch(r"[\\nt ]*", stripped):
        return False

    if re.fullmatch(r"[+\- ]*\d+\.?\d*\s*(MHz|mV|mA|mW|Hz|ms|°C|%%|p)?", stripped):
        return False

    if re.fullmatch(r"[%\d./*+\-ufdsxXlLhp ,°CM:HzWmVA\\n]+", stripped):
        return False

    if re.match(r"^hocClkIpc", stripped):
        return False

    if len(stripped) <= 2 and not stripped.isalpha():
        return False

    if re.fullmatch(r"(\\[nt])+", stripped):
        return False

    if re.fullmatch(r"[\s]*(\\u[0-9a-fA-F]{4}[\s]*)+", stripped):
        return False

    return True


def main():
    seen: set[str] = set()
    strings: list[str] = []

    if not os.path.isdir(SOURCE_DIR):
        print(f"Error: directory '{SOURCE_DIR}' not found.")
        return

    for filename in sorted(os.listdir(SOURCE_DIR)):
        if not filename.endswith((".cpp", ".h")):
            continue

        filepath = os.path.join(SOURCE_DIR, filename)
        for s in extract_strings_from_file(filepath):
            if s not in seen and should_include(s):
                seen.add(s)
                strings.append(s)

    translations = {s: s for s in strings}

    os.makedirs(os.path.dirname(OUTPUT_FILE), exist_ok=True)

    with open(OUTPUT_FILE, "w", encoding="utf-8") as f:
        f.write("{\n")
        items = list(translations.items())
        for i, (key, val) in enumerate(items):
            k = json.dumps(key, ensure_ascii=False)
            v = json.dumps(val, ensure_ascii=False)
            comma = "," if i < len(items) - 1 else ""
            f.write(f"    {k}: {v}{comma}\n")
        f.write("}\n")

    print(f"Extracted {len(translations)} unique strings from {SOURCE_DIR}")
    print(f"Written to {OUTPUT_FILE}")


if __name__ == "__main__":
    main()