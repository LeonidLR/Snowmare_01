"""Check that every smoke named in Scripts/test_map.json has a registered console command.

A smoke name with no matching "CodexTactics.<Name>" console command silently times out in
Scripts/smoke_parallel.ps1 (which runs "CodexTactics.<Name>"). This tool catches that before a run.

  python Scripts/Tools/check_smoke_names.py [--repo <path>] [--reverse]

Default: exit 1 for unknown map names (printed with their JSON section).
Reverse: list registered CodexTactics.*Smoke commands missing from smokeRegistry and,
separately, impact-map smokes lists; exit 1 only for missing registry entries.
"""
from __future__ import annotations

import argparse
import json
from pathlib import Path
import re
import sys

# FAutoConsoleCommand*(TEXT("CodexTactics.X") ...) / FAutoConsoleCommand* Var(TEXT("..."), ...) / RegisterConsoleCommand(TEXT("..."), ...)
REGISTRATION = re.compile(
    r'(?:FAutoConsoleCommand\w*|RegisterConsoleCommand)\s*(?:\w+\s*)?\(\s*TEXT\(\s*"([^"]+)"\s*\)')
PREFIX = "CodexTactics."


def registered_commands(repo: Path, *, smokes_only: bool = False) -> set[str]:
    """Short names (without the CodexTactics. prefix) of commands registered in Source/**/Debug/*.cpp."""
    names: set[str] = set()
    for cpp in (repo / "Source").glob("**/Debug/*.cpp"):
        text = cpp.read_text(encoding="utf-8", errors="replace")
        for match in REGISTRATION.finditer(text):
            name = match.group(1)
            if smokes_only and not (name.startswith(PREFIX) and name.endswith("Smoke")):
                continue
            names.add(name[len(PREFIX):] if name.startswith(PREFIX) else name)
    return names


def _strip(name: str) -> str:
    return name[len(PREFIX):] if name.startswith(PREFIX) else name


def _walk_smokes(node, path: str, out: list[tuple[str, str]]) -> None:
    if isinstance(node, dict):
        for key, value in node.items():
            if key == "smokes" and isinstance(value, list):
                out.extend((_strip(v), f"{path}.smokes") for v in value if isinstance(v, str))
            else:
                _walk_smokes(value, f"{path}.{key}" if path else key, out)
    elif isinstance(node, list):
        for index, value in enumerate(node):
            _walk_smokes(value, f"{path}[{index}]", out)


def archived_map_smokes(test_map: dict) -> set[str]:
    return {_strip(name) for names in test_map.get("archivedMapSmokes", {}).values() for name in names}


def map_smokes(test_map: dict) -> list[tuple[str, str]]:
    """(smoke name, JSON section) for every smoke referenced in the impact map."""
    out: list[tuple[str, str]] = []
    out.extend((_strip(n), "smokeRegistry") for n in test_map.get("smokeRegistry", []) if isinstance(n, str))
    out.extend((name, "archivedMapSmokes") for name in archived_map_smokes(test_map))
    out.extend((_strip(k), "smokeOptions") for k in test_map.get("smokeOptions", {}) if not k.startswith("_"))
    for section in ("knownFlaky", "knownFail"):
        value = test_map.get(section, {})
        keys = value.keys() if isinstance(value, dict) else value
        out.extend((_strip(k), section) for k in keys if isinstance(k, str) and not k.startswith("_"))
    for key, value in test_map.items():
        if key not in ("smokeRegistry", "smokeOptions", "knownFlaky", "knownFail", "nonSmokeDebugCommands"):
            _walk_smokes(value, key, out)
    return out


def check(repo: Path) -> list[tuple[str, str]]:
    test_map = json.loads((repo / "Scripts" / "test_map.json").read_text(encoding="utf-8"))
    known = registered_commands(repo)
    seen: set[tuple[str, str]] = set()
    unknown: list[tuple[str, str]] = []
    for name, section in map_smokes(test_map):
        if name not in known and (name, section) not in seen:
            seen.add((name, section))
            unknown.append((name, section))
    return unknown


def check_reverse(repo: Path) -> tuple[list[str], list[str]]:
    """Registered smoke commands absent from the registry and impact lists, respectively."""
    test_map = json.loads((repo / "Scripts" / "test_map.json").read_text(encoding="utf-8"))
    known = registered_commands(repo, smokes_only=True)
    registry = {_strip(n) for n in test_map.get("smokeRegistry", []) if isinstance(n, str)}
    impact: list[tuple[str, str]] = []
    _walk_smokes(test_map, "", impact)
    known -= archived_map_smokes(test_map)
    return sorted(known - registry), sorted(known - {name for name, _ in impact})


def main(argv: list[str] | None = None) -> int:
    parser = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    parser.add_argument("--repo", type=Path, default=Path(__file__).resolve().parents[2])
    parser.add_argument("--reverse", action="store_true", help="Find registered smokes absent from the map")
    args = parser.parse_args(argv)
    if args.reverse:
        missing_registry, missing_impact = check_reverse(args.repo.resolve())
        for name in missing_registry:
            print(f"MISSING smokeRegistry: {name}")
        for name in missing_impact:
            print(f"MISSING impact-map smokes: {name}")
        status = "FAIL" if missing_registry else "OK"
        print(f"{status}: {len(missing_registry)} registered smoke(s) missing from smokeRegistry; "
              f"{len(missing_impact)} missing from impact-map smokes lists")
        return int(bool(missing_registry))
    unknown = check(args.repo.resolve())
    if not unknown:
        print("OK: every smoke name in Scripts/test_map.json has a registered console command")
        return 0
    for name, section in sorted(unknown):
        print(f"UNKNOWN smoke: {name}  (in {section})")
    print(f"FAIL: {len(unknown)} smoke name(s) without a registered CodexTactics.<Name> command")
    return 1


if __name__ == "__main__":
    sys.exit(main())
