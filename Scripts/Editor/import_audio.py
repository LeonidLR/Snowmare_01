"""Unreal Editor Python Audio Importer (Task 045).

Purpose:
  Scans audio manifests (music_cues.json and voice_lines.json) along with audio source
  files in Saved/Orchestrator/audio_v1, and produces a structured import plan.

Safety & Constraints:
  1. Target paths are STRICTLY restricted to new directories under /Game/Audio/**.
     Any destination outside /Game/Audio/** is rejected by validation.
  2. STRICTLY --dry-run mode only. Live import is disabled per task 045 specifications
     ('НЕ запускать импорт; только режим --dry-run').

Usage:
  python Scripts/Editor/import_audio.py [--dry-run] [--audio-dir <path>]
"""

import argparse
from dataclasses import asdict, dataclass
import json
import os
import posixpath
import re
import sys
from typing import Any, Dict, List, Optional

try:
    import unreal  # noqa: F401
except ImportError:
    unreal = None

# Root paths
SCRIPT_DIR = os.path.dirname(os.path.abspath(__file__))
PROJECT_ROOT = os.path.abspath(os.path.join(SCRIPT_DIR, "..", ".."))

# Canonical package targets (STRICTLY under /Game/Audio/**)
DEST_ROOT = "/Game/Audio"
MUSIC_DEST_ROOT = f"{DEST_ROOT}/Music"
VOICE_DEST_ROOT = f"{DEST_ROOT}/Voice"
SFX_DEST_ROOT = f"{DEST_ROOT}/SFX"

SUPPORTED_EXTENSIONS = [".wav", ".ogg", ".mp3", ".flac"]


@dataclass
class AudioPlanItem:
    id: str
    kind: str  # 'music' or 'voice'
    subfolder: str
    asset_name: str
    destination_path: str
    full_package_path: str
    asset_class: str
    source_candidates: List[str]
    source_file: Optional[str]
    status: str  # 'FOUND' or 'MISSING'
    metadata: Dict[str, Any]


def validate_destination_path(path: str) -> None:
    """Enforces that any destination path is strictly under /Game/Audio/** without traversal."""
    normalized = posixpath.normpath(path)
    segments = [s for s in path.split("/") if s]
    if ".." in segments or ".." in normalized.split("/"):
        raise ValueError(
            f"Security violation: Path traversal '..' is strictly forbidden in destination '{path}'."
        )
    if not (normalized == DEST_ROOT or normalized.startswith(f"{DEST_ROOT}/")):
        raise ValueError(
            f"Security violation: Target destination '{path}' is outside '{DEST_ROOT}/**'. "
            f"All audio assets must strictly target /Game/Audio/**."
        )


def find_source_file(base_dir: str, candidate_rel_paths: List[str]) -> Optional[str]:
    """Checks for matching audio files across candidate relative paths and supported extensions."""
    if not os.path.isdir(base_dir):
        return None

    for rel in candidate_rel_paths:
        for ext in SUPPORTED_EXTENSIONS:
            full_path = os.path.normpath(os.path.join(base_dir, rel + ext))
            if os.path.isfile(full_path):
                return full_path

    # Also search flat by basename inside base_dir if candidates had subdirs
    for rel in candidate_rel_paths:
        base_name = os.path.basename(rel)
        for ext in SUPPORTED_EXTENSIONS:
            flat_path = os.path.normpath(os.path.join(base_dir, base_name + ext))
            if os.path.isfile(flat_path):
                return flat_path

    return None


def sanitize_asset_name(raw_name: str) -> str:
    """Produces a clean Unreal Engine asset identifier."""
    clean = re.sub(r"[^a-zA-Z0-9_]", "_", raw_name)
    if not clean.startswith("SW_"):
        clean = f"SW_{clean}"
    return clean


def get_expected_music_asset_path(cue_group: str, cue_id: str) -> str:
    """Helper returning canonical package path for music cues: /Game/Audio/Music/<cue_group>/SW_<cue_id>."""
    group = cue_group if cue_group else "misc"
    asset_name = sanitize_asset_name(cue_id)
    return f"{MUSIC_DEST_ROOT}/{group}/{asset_name}"


def get_expected_voice_asset_path(speaker: str, line_id: str) -> str:
    """Helper returning canonical package path for voice lines: /Game/Audio/Voice/<speaker>/SW_<line_id>."""
    spk = speaker if speaker else "common"
    asset_name = sanitize_asset_name(line_id)
    return f"{VOICE_DEST_ROOT}/{spk}/{asset_name}"


def build_audio_import_plan(
    audio_dir: str,
    music_manifest_path: str,
    voice_manifest_path: str,
) -> List[AudioPlanItem]:
    """Parses manifests and builds plan items with destination validation."""
    if not os.path.isfile(music_manifest_path):
        raise FileNotFoundError(f"Music manifest not found: {music_manifest_path}")
    if not os.path.isfile(voice_manifest_path):
        raise FileNotFoundError(f"Voice manifest not found: {voice_manifest_path}")

    with open(music_manifest_path, "r", encoding="utf-8") as f:
        music_data = json.load(f)
    with open(voice_manifest_path, "r", encoding="utf-8") as f:
        voice_data = json.load(f)

    plan_items: List[AudioPlanItem] = []

    # 1. Plan Music Cues
    for cue in music_data.get("cues", []):
        cue_id = cue["id"]
        group = cue.get("cue_group", "misc")
        dest_path = f"{MUSIC_DEST_ROOT}/{group}"
        validate_destination_path(dest_path)

        asset_name = sanitize_asset_name(cue_id)
        full_package = get_expected_music_asset_path(group, cue_id)

        candidates = [
            os.path.join("music", group, cue_id),
            os.path.join("music", cue_id),
            os.path.join(group, cue_id),
            cue_id,
        ]

        source_file = find_source_file(audio_dir, candidates)
        status = "FOUND" if source_file else "MISSING"

        plan_items.append(
            AudioPlanItem(
                id=cue_id,
                kind="music",
                subfolder=f"Music/{group}",
                asset_name=asset_name,
                destination_path=dest_path,
                full_package_path=full_package,
                asset_class="SoundWave",
                source_candidates=candidates,
                source_file=source_file,
                status=status,
                metadata={
                    "cue_group": group,
                    "variant": cue.get("variant", ""),
                    "loop": cue.get("loop", False),
                    "target_duration_sec": cue.get("target_duration_sec", 0),
                    "prompt": cue.get("prompt", ""),
                    "model": cue.get("model", ""),
                },
            )
        )

    # 2. Plan Voice Lines
    speakers = voice_data.get("speakers", {})
    for line in voice_data.get("lines", []):
        line_id = line["id"]
        speaker = line.get("speaker", "common")
        dest_path = f"{VOICE_DEST_ROOT}/{speaker}"
        validate_destination_path(dest_path)

        asset_name = sanitize_asset_name(line_id)
        full_package = get_expected_voice_asset_path(speaker, line_id)

        candidates = [
            os.path.join("voice", speaker, line_id),
            os.path.join("voice", line_id),
            os.path.join(speaker, line_id),
            line_id,
        ]

        source_file = find_source_file(audio_dir, candidates)
        status = "FOUND" if source_file else "MISSING"

        speaker_meta = speakers.get(speaker, {})

        plan_items.append(
            AudioPlanItem(
                id=line_id,
                kind="voice",
                subfolder=f"Voice/{speaker}",
                asset_name=asset_name,
                destination_path=dest_path,
                full_package_path=full_package,
                asset_class="SoundWave",
                source_candidates=candidates,
                source_file=source_file,
                status=status,
                metadata={
                    "speaker": speaker,
                    "speaker_name": speaker_meta.get("display_name", speaker),
                    "voice": line.get("voice", speaker_meta.get("voice", "")),
                    "text": line.get("text", ""),
                    "source": line.get("source", ""),
                },
            )
        )

    return plan_items


def validate_plan(plan_items: List[AudioPlanItem]) -> List[str]:
    """Verifies that all plan items strictly conform to project architectural rules."""
    errors = []
    seen_packages = set()

    for item in plan_items:
        # Check prefix
        if not item.destination_path.startswith(f"{DEST_ROOT}/"):
            errors.append(f"Item '{item.id}' destination '{item.destination_path}' does not start with '{DEST_ROOT}/'")

        # Check uniqueness
        if item.full_package_path in seen_packages:
            errors.append(f"Duplicate destination package: '{item.full_package_path}'")
        seen_packages.add(item.full_package_path)

        # Check valid asset identifier
        if not re.match(r"^[a-zA-Z0-9_]+$", item.asset_name):
            errors.append(f"Invalid asset name '{item.asset_name}' for item '{item.id}'")

    return errors


def generate_plan_summary(plan_items: List[AudioPlanItem], audio_dir: str) -> Dict[str, Any]:
    """Generates structured report dictionary for JSON output."""
    music_items = [i for i in plan_items if i.kind == "music"]
    voice_items = [i for i in plan_items if i.kind == "voice"]
    found_items = [i for i in plan_items if i.status == "FOUND"]
    missing_items = [i for i in plan_items if i.status == "MISSING"]

    dest_folders = sorted(list(set(i.destination_path for i in plan_items)))

    return {
        "status": "DRY_RUN_SUCCESS",
        "audio_dir": audio_dir,
        "destination_root": DEST_ROOT,
        "counts": {
            "total_items": len(plan_items),
            "music_cues": len(music_items),
            "voice_lines": len(voice_items),
            "found_files": len(found_items),
            "missing_files": len(missing_items),
            "destination_folders": len(dest_folders),
        },
        "destination_folders": dest_folders,
        "items": [asdict(i) for i in plan_items],
    }


def format_cli_table(plan_items: List[AudioPlanItem]) -> str:
    """Formats a concise table of the dry-run import plan."""
    lines = [
        "=" * 105,
        f"{'ID':<24} | {'KIND':<6} | {'SUBFOLDER':<20} | {'STATUS':<7} | {'DESTINATION PACKAGE':<40}",
        "-" * 105,
    ]
    for it in plan_items:
        lines.append(
            f"{it.id:<24} | {it.kind:<6} | {it.subfolder:<20} | {it.status:<7} | {it.full_package_path:<40}"
        )
    lines.append("=" * 105)
    return "\n".join(lines)


def main(argv: Optional[List[str]] = None) -> int:
    parser = argparse.ArgumentParser(description="Unreal Editor Audio Importer (Dry-Run)")
    parser.add_argument(
        "--dry-run",
        action="store_true",
        default=True,
        help="Dry-run mode: prints import plan without modifying assets (default: True).",
    )
    parser.add_argument(
        "--live",
        action="store_true",
        default=False,
        help="Live import execution (STRICTLY DISABLED per task 045).",
    )
    parser.add_argument(
        "--audio-dir",
        default=os.path.join(PROJECT_ROOT, "Saved", "Orchestrator", "audio_v1"),
        help="Source directory containing generated audio files.",
    )
    parser.add_argument(
        "--music-manifest",
        default=os.path.join(PROJECT_ROOT, "Content", "Data", "Audio", "music_cues.json"),
        help="Path to music_cues.json manifest.",
    )
    parser.add_argument(
        "--voice-manifest",
        default=os.path.join(PROJECT_ROOT, "Content", "Data", "Audio", "voice_lines.json"),
        help="Path to voice_lines.json manifest.",
    )
    parser.add_argument(
        "--output-json",
        default=os.path.join(PROJECT_ROOT, "Saved", "Logs", "audio_import_plan.json"),
        help="Path to write JSON plan summary.",
    )

    args = parser.parse_args(argv)

    if args.live:
        sys.stderr.write(
            "ERROR: Live import is strictly disabled per task 045 directive ('НЕ запускать импорт; только режим --dry-run').\n"
        )
        return 1

    print(f"=== Audio Importer Dry-Run ===")
    print(f"Source Directory   : {args.audio_dir}")
    print(f"Music Manifest     : {args.music_manifest}")
    print(f"Voice Manifest     : {args.voice_manifest}")
    print(f"Destination Root   : {DEST_ROOT}/**")
    print(f"Mode               : DRY-RUN ONLY\n")

    try:
        plan_items = build_audio_import_plan(
            audio_dir=args.audio_dir,
            music_manifest_path=args.music_manifest,
            voice_manifest_path=args.voice_manifest,
        )
    except Exception as e:
        sys.stderr.write(f"ERROR: Failed to build import plan: {e}\n")
        return 1

    validation_errors = validate_plan(plan_items)
    if validation_errors:
        sys.stderr.write("ERROR: Import plan validation failed:\n")
        for err in validation_errors:
            sys.stderr.write(f"  - {err}\n")
        return 1

    summary = generate_plan_summary(plan_items, args.audio_dir)

    print(format_cli_table(plan_items))
    print(f"\nTotal Items Planned   : {summary['counts']['total_items']}")
    print(f"  - Music Cues        : {summary['counts']['music_cues']}")
    print(f"  - Voice Lines       : {summary['counts']['voice_lines']}")
    print(f"  - Files Found       : {summary['counts']['found_files']}")
    print(f"  - Files Pending Gen : {summary['counts']['missing_files']}")
    print(f"Destination Folders   : {summary['counts']['destination_folders']}")
    for folder in summary["destination_folders"]:
        print(f"  -> {folder}")

    # Write output JSON log
    try:
        os.makedirs(os.path.dirname(args.output_json), exist_ok=True)
        with open(args.output_json, "w", encoding="utf-8") as f:
            json.dump(summary, f, indent=2, ensure_ascii=False)
        print(f"\nDetailed plan written to: {args.output_json}")
    except Exception as e:
        sys.stderr.write(f"WARNING: Could not write JSON plan to {args.output_json}: {e}\n")

    print("\nResult: DRY-RUN PASS (0 assets created, 0 live imports performed)")
    return 0


if __name__ == "__main__":
    sys.exit(main())
