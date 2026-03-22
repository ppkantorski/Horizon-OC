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

UNTRANSLATABLE = {
    # Developers
    "Souldbminer",
    "Lightos_",
    # Contributors
    "Blaise25",
    # Testers
    "Samybigio2011",
    "Delta",
    "Miki1305",
    "Happy",
    "Flopsider",
    "Winnerboi77",
    "WE1ZARD",
    "Alvise",
    "agjeococh",
    "Xenshen",
    "Frost",
    # Special Thanks
    "ScriesM - Atmosphere CFW",
    "KazushiMe - Switch OC Suite",
    "hanai3bi - Switch OC Suite & EOS",
    "NaGaa95 - L4T-OC-Kernel",
    "B3711 - EOS",
    "RetroNX - sys-clk",
    "b0rd2death - Ultrahand",
    "MasaGratoR - Status Monitor",
    # RAM modules
    "HB-MGCH 4GB",
    "HM-MGCH 6GB",
    "HM-MGXX 8GB",
    "AM-MGCJ 4GB",
    "AM-MGCJ 8GB",
    "AA-MGCL 4GB",
    "AA-MGCL 8GB",
    "AB-MGCL 4GB",
    "x267 4GB",
    "NLE 4GB",
    "NEE 4GB",
    "NME 4GB",
    "WT:C 4GB",
    "WT:E 4GB",
    "WT:F 4GB",
    "WT:B 4GB",
    # Technical labels that must not be translated
    "NV Service",
    "Governor",
    "Speedo:",
    "%u.%u%u mV",
    "1333 RL",
    "1600 RL",
    "1866 RL",
    "2133 RL",
    "VDD2 + VDDQ",
    "VDD2 + Usage",
    "VDDQ + Usage",
    "SoC DVB Shift",
    "PCV Hijack",
    "Horizon OC Zeus",
    # Timing labels
    "t1 tRCD",
    "t2 tRP",
    "t3 tRAS",
    "t4 tRRD",
    "t5 tRFC",
    "t6 tRTW",
    "t7 tWTR",
    "t8 tREFI",
    "JEDEC".

    # MHz warning strings with mixed technical terms
    "1581MHz Tbreak",
    "1683MHz Tbreak",
}


def extract_strings_from_file(filepath: str) -> list[str]:
    with open(filepath, "r", encoding="utf-8", errors="ignore") as f:
        content = f.read()
    pattern = r'"((?:[^"\\]|\\.)*)"'
    return re.findall(pattern, content)


def should_include(s: str) -> bool:
    if not s or s.isspace():
        return False

    stripped = s.strip()

    # --- Skip strings 4 characters or less ---
    if len(stripped) <= 4:
        return False

    # --- Prefix filters ---
    for prefix in IGNORED_PREFIXES:
        if s.startswith(prefix):
            return False

    # Skip raw unicode escape sequences
    if re.match(r"^\\u[0-9a-fA-F]", s):
        return False

    # --- File paths / includes ---
    if re.fullmatch(r"[a-zA-Z0-9_./\\-]+\.(h|hpp|cpp|c)", stripped):
        return False

    # --- Format specifiers ---
    if re.fullmatch(r"[%\d.*\-+lfdsuxXpLh ]*", stripped) and "%" in stripped:
        return False

    # --- Whitespace / escape sequences only ---
    if re.fullmatch(r"[\\nt ]*", stripped):
        return False

    # --- Pure numeric values with units ---
    if re.fullmatch(r"[+\- ]*\d+\.?\d*\s*(MHz|mV|mA|mW|Hz|ms|°C|%%|p)?", stripped):
        return False

    # --- Mixed format/unit junk ---
    if re.fullmatch(r"[%\d./*+\-ufdsxXlLhp ,°CM:HzWmVA\\n]+", stripped):
        return False

    # --- IPC function names ---
    if re.match(r"^hocClkIpc", stripped):
        return False

    # --- Escape sequences only ---
    if re.fullmatch(r"(\\[nt])+", stripped):
        return False

    # --- Unicode escape sequences only ---
    if re.fullmatch(r"[\s]*(\\u[0-9a-fA-F]{4}[\s]*)+", stripped):
        return False

    # --- Untranslatable set ---
    if stripped in UNTRANSLATABLE:
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