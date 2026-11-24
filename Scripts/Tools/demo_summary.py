"""Summarize local CodexTactics JSONL v1 recordings without modifying them."""

import argparse
from dataclasses import dataclass, field
import json
import math
from pathlib import Path


# Serialized names of EPmcOrder in Source/CodexTactics/Public/AI/PmcCommanderRules.h.
PMC_ORDERS = frozenset(
    "Hold TakeCover Advance Flank Suppress FallBack Heal Relocate Ambush Reload Sidearm Crouch Objective RpgStrike".split()
)
MIN_SESSION_SECONDS = 60
DEFAULT_DEMOS = Path(__file__).resolve().parents[2] / "Saved" / "Demos"


@dataclass
class Summary:
    map_name: str = "?"
    duration: float | None = None
    orders: int = 0
    accepted: int = 0
    mapped: int = 0
    snapshots: int = 0
    lab: bool = False
    reasons: list[str] = field(default_factory=list)

    @property
    def valid(self) -> bool:
        return not self.reasons

    def render(self, filename: str) -> str:
        duration = "?" if self.duration is None else f"{self.duration:.1f}s"
        share = self.mapped / self.orders if self.orders else 0
        verdict = "yes" if self.valid else f"no ({'; '.join(self.reasons)})"
        return (
            f"{filename} | map={self.map_name} | duration={duration} | "
            f"orders={self.orders} accepted={self.accepted} | "
            f"PMC mapped={self.mapped}/{self.orders} ({share:.1%}) | "
            f"snapshots={self.snapshots} | lab={'yes' if self.lab else 'no'} | valid={verdict}"
        )


def summarize(path: Path) -> Summary:
    summary = Summary()
    header = None
    footer = None
    row_error = None
    last_line = 0

    try:
        with path.open("rb") as stream:
            for line_number, line in enumerate(stream, 1):
                last_line = line_number
                try:
                    row = json.loads(line.decode("utf-8"))
                except (UnicodeError, ValueError):
                    row_error = row_error or f"line {line_number}: invalid UTF-8 JSON"
                    continue
                if not isinstance(row, dict):
                    row_error = row_error or f"line {line_number}: expected an object"
                    continue

                kind = row.get("type")
                if footer is not None:
                    row_error = row_error or f"line {line_number}: data after footer"
                    continue
                if kind == "header":
                    if line_number != 1:
                        row_error = row_error or f"line {line_number}: misplaced header"
                    else:
                        header = row
                elif kind == "footer":
                    footer = row
                elif kind == "order":
                    summary.orders += 1
                    accepted = row.get("ok")
                    label = row.get("pmc_order")
                    if type(accepted) is not bool or "pmc_order" not in row or not (
                        label is None or isinstance(label, str)
                    ):
                        row_error = row_error or f"line {line_number}: invalid order fields"
                    summary.accepted += accepted is True
                    summary.mapped += isinstance(label, str) and label in PMC_ORDERS
                elif kind == "snap":
                    summary.snapshots += 1
                elif kind != "mode":
                    row_error = row_error or f"line {line_number}: unknown record type"
    except OSError as error:
        summary.reasons.append(f"cannot read file: {error.strerror or error}")
        return summary

    if row_error:
        summary.reasons.append(row_error)
    if header is None:
        summary.reasons.append("missing first-line header")
    else:
        if type(header.get("v")) is not int or header["v"] != 1:
            summary.reasons.append("unsupported recording version")
        map_name = header.get("map")
        if isinstance(map_name, str) and map_name.strip():
            summary.map_name = map_name
        else:
            summary.reasons.append("missing map")
        flags = header.get("flags", {})
        if not isinstance(flags, dict) or any(
            type(flags.get(key, False)) is not bool for key in ("immortal", "infinite_ammo")
        ):
            summary.reasons.append("invalid lab flags")
        else:
            summary.lab = flags.get("immortal", False) or flags.get("infinite_ammo", False)

    if footer is None:
        summary.reasons.append("missing footer" if last_line else "empty file (missing footer)")
    else:
        duration = footer.get("duration_s")
        try:
            if type(duration) in (int, float) and math.isfinite(duration) and duration >= 0:
                summary.duration = float(duration)
        except OverflowError:
            pass
        if summary.duration is None:
            summary.reasons.append("invalid duration")
        elif summary.duration < MIN_SESSION_SECONDS:
            summary.reasons.append(f"duration under {MIN_SESSION_SECONDS}s")

        for key, count in (
            ("orders", summary.orders), ("orders_ok", summary.accepted), ("snaps", summary.snapshots)
        ):
            if type(footer.get(key)) is not int or footer[key] != count:
                summary.reasons.append(f"footer {key} does not match {count} observed")
    if not summary.accepted:
        summary.reasons.append("no accepted orders")
    return summary


def main(argv: list[str] | None = None) -> int:
    parser = argparse.ArgumentParser(
        description="Summarize JSONL v1 recordings. PMC share is mapped order rows divided by all order rows."
    )
    parser.add_argument("demos_dir", nargs="?", type=Path, default=DEFAULT_DEMOS, help="defaults to Saved/Demos")
    args = parser.parse_args(argv)
    if args.demos_dir.exists() and not args.demos_dir.is_dir():
        parser.error("demos_dir must be a directory")
    paths = sorted(args.demos_dir.glob("*.jsonl"))
    if not paths:
        print(f"No .jsonl recordings in {args.demos_dir}")
        return 0
    has_invalid = False
    for path in paths:
        summary = summarize(path)
        print(summary.render(path.name))
        has_invalid |= not summary.valid
    return int(has_invalid)


if __name__ == "__main__":
    raise SystemExit(main())
