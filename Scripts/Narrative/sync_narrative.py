"""Narrative Sync Bridge: Imports Google Sheets / Remote CSV into Codex Tactics DataAssets and JSON cache.

Architecture & Workflow:
  1. Downloads live dialogues and glossary from a configured Google Sheets export link (or local fallback).
  2. Parses and validates schema: supports bilingual RU and EN columns.
  3. Outputs clean JSON manifest into Content/Data/Narrative/narrative_manifest.json.

Usage:
  python Scripts/Narrative/sync_narrative.py [--url <google_sheets_csv_url>] [--local]
"""

import csv
import io
import json
import os
import sys
import urllib.request
import urllib.error

ROOT = os.path.abspath(os.path.join(os.path.dirname(__file__), "..", ".."))
CONFIG_PATH = os.path.join(ROOT, "Saved", "Config", "narrative_config.json")
LOCAL_CSV_DIR = os.path.join(ROOT, "Content", "Data", "Narrative")
MANIFEST_OUT = os.path.join(LOCAL_CSV_DIR, "narrative_manifest.json")

DEFAULT_CONFIG = {
    "dialogues_url": "",
    "glossary_url": "",
    "max_line_length": 600,
    "default_language": "en"
}


def load_config():
    if os.path.exists(CONFIG_PATH):
        try:
            with open(CONFIG_PATH, "r", encoding="utf-8") as f:
                return {**DEFAULT_CONFIG, **json.load(f)}
        except Exception:
            pass
    return DEFAULT_CONFIG.copy()


def fetch_csv_content(url: str, local_fallback_file: str) -> str:
    """Fetches CSV string from remote URL or falls back to local file on disk."""
    if url and url.startswith("http"):
        print(f"[NarrativeSync] Fetching from Google Sheets: {url[:70]}...")
        req = urllib.request.Request(url, headers={"User-Agent": "Mozilla/5.0 (CodexNarrativeBridge/1.0)"})
        try:
            with urllib.request.urlopen(req, timeout=15) as resp:
                data = resp.read().decode("utf-8")
                if len(data.strip()) > 0:
                    print(f"[NarrativeSync] Downloaded {len(data)} bytes successfully.")
                    return data
                else:
                    print(f"[NarrativeSync] Remote returned empty sheet. Using local fallback: {local_fallback_file}")
        except Exception as err:
            print(f"[NarrativeSync] Warning: Failed to fetch remote ({err}). Falling back to local disk.")

    local_path = os.path.join(LOCAL_CSV_DIR, local_fallback_file)
    if os.path.exists(local_path):
        print(f"[NarrativeSync] Reading local fallback: {local_fallback_file}")
        with open(local_path, "r", encoding="utf-8") as f:
            return f.read()

    raise FileNotFoundError(f"Neither remote URL nor local file exists: {local_fallback_file}")


def parse_dialogues_csv(csv_text: str, cfg: dict):
    reader = csv.DictReader(io.StringIO(csv_text, newline=""))
    sequences = {}
    warnings = []

    for row_idx, row in enumerate(reader, start=2):
        seq_id = row.get("SequenceID", "").strip()
        if not seq_id:
            continue

        speaker_en = row.get("Speaker_EN", "").strip() or row.get("Speaker", "").strip()
        speaker_ru = row.get("Speaker_RU", "").strip() or row.get("Speaker", "").strip()
        text_en = row.get("Text_EN", "").strip() or row.get("Text", "").strip()
        text_ru = row.get("Text_RU", "").strip() or row.get("Text", "").strip()
        delay_raw = row.get("DelayAfter", "4.0").strip()
        mood = row.get("Mood", "").strip()
        notes = row.get("ContextNotes", "").strip()

        try:
            delay = float(delay_raw)
        except ValueError:
            delay = 4.0
            warnings.append(f"Row {row_idx} ({seq_id}): Invalid delay '{delay_raw}', defaulted to 4.0s")

        if len(text_en) > cfg["max_line_length"]:
            warnings.append(f"Row {row_idx} ({seq_id}): EN text length {len(text_en)} exceeds {cfg['max_line_length']} chars.")

        if seq_id not in sequences:
            sequences[seq_id] = {
                "sequence_id": seq_id,
                "lines": []
            }

        sequences[seq_id]["lines"].append({
            "speaker_en": speaker_en,
            "speaker_ru": speaker_ru,
            "text_en": text_en,
            "text_ru": text_ru,
            "delay": delay,
            "mood": mood,
            "notes": notes
        })

    return sequences, warnings


def parse_glossary_csv(csv_text: str):
    reader = csv.DictReader(io.StringIO(csv_text, newline=""))
    glossary = {}
    for row in reader:
        key = row.get("Key", "").strip()
        if not key:
            continue
        glossary[key] = {
            "category": row.get("Category", "").strip(),
            "name_en": row.get("Name_EN", "").strip() or row.get("Name_RU", "").strip(),
            "name_ru": row.get("Name_RU", "").strip(),
            "description_en": row.get("Description_EN", "").strip() or row.get("Description_RU", "").strip(),
            "description_ru": row.get("Description_RU", "").strip()
        }
    return glossary


def main():
    cfg = load_config()
    print("=== Codex Tactics: Narrative Bridge ===")

    # 1. Dialogues
    dialogues_raw = fetch_csv_content(cfg.get("dialogues_url"), "dialogues.csv")
    sequences, warnings = parse_dialogues_csv(dialogues_raw, cfg)

    # 2. Glossary
    glossary_raw = fetch_csv_content(cfg.get("glossary_url"), "glossary.csv")
    glossary = parse_glossary_csv(glossary_raw)

    manifest = {
        "sequences": sequences,
        "glossary": glossary,
        "total_sequences": len(sequences),
        "total_lines": sum(len(s["lines"]) for s in sequences.values()),
        "total_glossary_entries": len(glossary)
    }

    with open(MANIFEST_OUT, "w", encoding="utf-8") as f:
        json.dump(manifest, f, indent=2, ensure_ascii=False)

    print(f"\n[OK] Manifest compiled: {MANIFEST_OUT}")
    print(f"  * Sequences: {manifest['total_sequences']}")
    print(f"  * Dialogue Lines: {manifest['total_lines']}")
    print(f"  * Glossary Entries: {manifest['total_glossary_entries']}")

    if warnings:
        print(f"\n[!] Narrative Editor Warnings ({len(warnings)}):")
        for w in warnings[:5]:
            print(f"  - {w}")
        if len(warnings) > 5:
            print(f"  - ...and {len(warnings) - 5} more.")


if __name__ == "__main__":
    main()
