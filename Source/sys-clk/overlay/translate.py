#!/usr/bin/env python3
"""
Usage:
    python translate_json.py            # translate to all languages
    python translate_json.py fr de ja   # translate to specific languages only
"""

import json
import os
import re
import ssl
import sys
import time
import urllib.parse
import urllib.request

INPUT_FILE = os.path.join("lang", "en.json")
BATCH_SIZE = 50
DELAY = 0.1

ALL_LANGUAGES = [
    "es", "fr", "de", "ja", "ko", "it", "nl", "pt", "ru", "uk", "pl", "zh-cn", "zh-tw",
]

SSL_CTX = ssl.create_default_context()
SSL_CTX.check_hostname = False
SSL_CTX.verify_mode = ssl.CERT_NONE

SEPARATOR = "\n\n###\n\n"


def load_json(path: str) -> dict:
    with open(path, "r", encoding="utf-8") as f:
        content = f.read()
    content = re.sub(r",\s*}", "}", content)
    return json.loads(content)


def save_json(data: dict, path: str):
    os.makedirs(os.path.dirname(path) or ".", exist_ok=True)
    with open(path, "w", encoding="utf-8") as f:
        f.write("{\n")
        items = list(data.items())
        for i, (key, val) in enumerate(items):
            k = json.dumps(key, ensure_ascii=False)
            v = json.dumps(val, ensure_ascii=False)
            comma = "," if i < len(items) - 1 else ""
            f.write(f"    {k}: {v}{comma}\n")
        f.write("}\n")


def translate_batch(texts: list[str], dest: str) -> list[str]:
    combined = SEPARATOR.join(texts)
    encoded = urllib.parse.quote(combined)
    url = (
        f"https://translate.googleapis.com/translate_a/single"
        f"?client=gtx&sl=en&tl={dest}&dt=t&q={encoded}"
    )

    req = urllib.request.Request(url)
    req.add_header("User-Agent", "Mozilla/5.0")

    with urllib.request.urlopen(req, timeout=30, context=SSL_CTX) as resp:
        data = json.loads(resp.read().decode("utf-8"))
        full = "".join(part[0] for part in data[0] if part[0])

    parts = re.split(r"\s*###\s*", full)

    if len(parts) == len(texts):
        return [p.strip() for p in parts]

    print(f"\n    Split mismatch ({len(parts)} vs {len(texts)}), retrying individually...", end=" ")
    results = []
    for t in texts:
        try:
            enc = urllib.parse.quote(t)
            u = f"https://translate.googleapis.com/translate_a/single?client=gtx&sl=en&tl={dest}&dt=t&q={enc}"
            r = urllib.request.Request(u)
            r.add_header("User-Agent", "Mozilla/5.0")
            with urllib.request.urlopen(r, timeout=10, context=SSL_CTX) as rsp:
                d = json.loads(rsp.read().decode("utf-8"))
                results.append("".join(p[0] for p in d[0] if p[0]))
            time.sleep(0.3)
        except Exception:
            results.append(t)
    return results


def translate_language(keys: list[str], values: list[str], lang: str):
    total = len(values)
    total_batches = (total + BATCH_SIZE - 1) // BATCH_SIZE
    translated_values = []

    for i in range(0, total, BATCH_SIZE):
        batch = values[i:i + BATCH_SIZE]
        batch_num = (i // BATCH_SIZE) + 1
        print(f"    Batch {batch_num}/{total_batches} ({len(batch)} strings)...",
              end=" ", flush=True)

        retries = 3
        for attempt in range(retries):
            try:
                results = translate_batch(batch, lang)
                translated_values.extend(results)
                print("OK")
                break
            except Exception as e:
                if attempt < retries - 1:
                    wait = DELAY * (attempt + 2)
                    print(f"retry in {wait}s ({e})")
                    time.sleep(wait)
                else:
                    print(f"FAILED ({e}), keeping original")
                    translated_values.extend(batch)

        if i + BATCH_SIZE < total:
            time.sleep(DELAY)

    output_file = os.path.join("lang", f"{lang}.json")
    output = dict(zip(keys, translated_values))
    save_json(output, output_file)
    print(f"    -> {output_file}\n")


def main():
    if not os.path.isfile(INPUT_FILE):
        print(f"Error: {INPUT_FILE} not found. Run extract_translations.py first.")
        sys.exit(1)

    languages = sys.argv[1:] if len(sys.argv) > 1 else ALL_LANGUAGES

    data = load_json(INPUT_FILE)
    keys = list(data.keys())
    values = list(data.values())

    print(f"Loaded {len(values)} strings from {INPUT_FILE}")
    print(f"Translating to {len(languages)} languages: {', '.join(languages)}\n")

    for idx, lang in enumerate(languages):
        print(f"[{idx + 1}/{len(languages)}] Translating to '{lang}'...")
        translate_language(keys, values, lang)

    print("All done!")


if __name__ == "__main__":
    main()