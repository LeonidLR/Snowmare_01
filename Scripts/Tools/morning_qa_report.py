#!/usr/bin/env python3
"""
Autonomous Unattended QA — Morning Report Generator (#41)
docs/design/AUTO_QA.md

Aggregates test, smoke, bot, trainer, and orchestrator execution logs across
Saved/Automation, Saved/Orchestrator, Saved/Trainer, and Saved/Telemetry.
Deduplicates errors and hitches into clustered issues and generates an actionable
morning markdown report with proposed QA rows for the project task board.

Usage:
    python Scripts/Tools/morning_qa_report.py [--root <repo_root>] [--date YYYY-MM-DD] [--out <output_path>]
"""

import os
import sys
import re
import json
import hashlib
import datetime
import argparse
from typing import Dict, List, Optional, Any, Tuple

# Ensure UTF-8 output on Windows consoles
if hasattr(sys.stdout, "reconfigure"):
    sys.stdout.reconfigure(encoding="utf-8", errors="replace")
if hasattr(sys.stderr, "reconfigure"):
    sys.stderr.reconfigure(encoding="utf-8", errors="replace")


class IssueCluster:
    def __init__(self, category: str, title: str, fingerprint: str, severity: str = "MEDIUM"):
        self.category = category
        self.title = title
        self.fingerprint = fingerprint
        self.severity = severity
        self.count = 0
        self.sources: List[str] = []
        self.samples: List[str] = []
        self.timestamps: List[str] = []
        self.details: Dict[str, Any] = {}

    def add_occurrence(self, source: str, snippet: str, timestamp: str = ""):
        self.count += 1
        if source not in self.sources:
            self.sources.append(source)
        if len(self.samples) < 3 and snippet and snippet not in self.samples:
            self.samples.append(snippet.strip())
        if timestamp and timestamp not in self.timestamps:
            self.timestamps.append(timestamp)

    def to_dict(self) -> Dict[str, Any]:
        return {
            "fingerprint": self.fingerprint,
            "category": self.category,
            "title": self.title,
            "severity": self.severity,
            "count": self.count,
            "sources": self.sources,
            "samples": self.samples,
            "details": self.details,
        }


class MorningQAReporter:
    def __init__(self, root_dir: str, report_date: Optional[str] = None):
        self.root = os.path.abspath(root_dir)
        self.date_str = report_date or datetime.date.today().strftime("%Y-%m-%d")
        self.clusters: Dict[str, IssueCluster] = {}
        self.total_logs_scanned = 0
        self.total_lines_scanned = 0

    def make_fingerprint(self, category: str, key_string: str) -> str:
        norm = re.sub(r"\d+", "N", key_string.strip().lower())
        norm = re.sub(r"\s+", " ", norm)
        raw = f"{category}:{norm}"
        return hashlib.md5(raw.encode("utf-8")).hexdigest()[:10]

    def record_issue(self, category: str, title: str, snippet: str, source: str, severity: str = "MEDIUM", timestamp: str = ""):
        fp = self.make_fingerprint(category, title)
        if fp not in self.clusters:
            self.clusters[fp] = IssueCluster(category, title, fp, severity)
        self.clusters[fp].add_occurrence(source, snippet, timestamp)

    def parse_unreal_log(self, file_path: str):
        self.total_logs_scanned += 1
        rel_path = os.path.relpath(file_path, self.root)
        try:
            with open(file_path, "r", encoding="utf-8", errors="replace") as f:
                lines = f.readlines()
        except Exception as e:
            self.record_issue("IO_ERROR", f"Failed to read log {os.path.basename(file_path)}: {e}", str(e), rel_path, "LOW")
            return

        self.total_lines_scanned += len(lines)
        for i, line in enumerate(lines):
            # 1. Fatal Errors & Assertion Failures
            if "Assertion failed:" in line or "Fatal error:" in line or "Ensure condition failed:" in line:
                snippet = "".join(lines[max(0, i - 1):min(len(lines), i + 4)])
                title = line.strip()
                self.record_issue("CRASH_ASSERT", title, snippet, rel_path, "BLOCKER")

            # 2. FlushAsyncLoading / Long Hitch Stalls
            m_flush = re.search(r"FlushAsyncLoading\((\d+)\):\s*(\d+)\s*QueuedPackages.*took\s*([\d\.]+)\s*s", line)
            if m_flush:
                duration_s = float(m_flush.group(3))
                if duration_s >= 0.5:
                    title = f"FlushAsyncLoading stall ({duration_s:.2f}s)"
                    snippet = line.strip()
                    sev = "HIGH" if duration_s >= 2.0 else "MEDIUM"
                    self.record_issue("PERF_HITCH", title, snippet, rel_path, sev)

            # 3. Skinned Asset Wait (compile / init stall)
            m_skin = re.search(r"Waiting for skinned assets to be ready.*?\((\w+)\)", line)
            if m_skin:
                asset_name = m_skin.group(1)
                title = f"Skinned asset compilation stall: {asset_name}"
                snippet = line.strip()
                self.record_issue("PERF_HITCH", title, snippet, rel_path, "HIGH")

            # 4. Smoke / Automation Test Failures
            if "[FAILED]" in line or "Error: [Automation]" in line or "TestFalse failed" in line or "TestTrue failed" in line:
                snippet = "".join(lines[max(0, i - 1):min(len(lines), i + 3)])
                title = line.strip()
                self.record_issue("TEST_FAILURE", title, snippet, rel_path, "HIGH")

            # 5. Bot Stalls & Navigation Errors
            if "[Bot]" in line and ("stuck" in line.lower() or "stalled" in line.lower() or "failed" in line.lower()):
                snippet = line.strip()
                title = f"Bot traversal stall: {line.strip()}"
                self.record_issue("BOT_STALL", title, snippet, rel_path, "MEDIUM")

            # 6. Runtime Sensor Alarms (MON_*)
            m_sensor = re.search(r"\b(MON_[A-Z_]+)\b[:\s]+(.*)", line)
            if m_sensor:
                sensor_id = m_sensor.group(1)
                desc = m_sensor.group(2).strip()
                title = f"Sensor {sensor_id}: {desc}"
                sev = "BLOCKER" if sensor_id in ["MON_STUCK", "MON_FALL_THROUGH"] else "HIGH"
                self.record_issue(sensor_id, title, line.strip(), rel_path, sev)

    def parse_telemetry_jsonl(self, file_path: str, max_lines: int = 500):
        self.total_logs_scanned += 1
        rel_path = os.path.relpath(file_path, self.root)
        try:
            with open(file_path, "r", encoding="utf-8", errors="replace") as f:
                lines = f.readlines()
        except Exception as e:
            self.record_issue("IO_ERROR", f"Failed to read telemetry: {e}", str(e), rel_path, "LOW")
            return

        self.total_lines_scanned += len(lines)
        for line in lines[-max_lines:]:
            line = line.strip()
            if not line:
                continue
            try:
                rec = json.loads(line)
            except Exception:
                continue

            # Check outcome
            outcome = rec.get("outcome") or rec.get("result") or {}
            if isinstance(outcome, dict):
                status = outcome.get("status") or outcome.get("result")
                if status in ["FAILED", "STUCK", "TIMEOUT", "CRASH"]:
                    title = f"Telemetry run failure: {status} in {rec.get('map', 'unknown')}"
                    self.record_issue("BOT_TELEMETRY", title, json.dumps(rec, ensure_ascii=False), rel_path, "HIGH")
            elif isinstance(outcome, str) and outcome.upper() in ["FAIL", "FAILED", "CRASH", "TIMEOUT"]:
                title = f"Telemetry run failure: {outcome}"
                self.record_issue("BOT_TELEMETRY", title, line, rel_path, "HIGH")

    def parse_trainer_summary(self, file_path: str):
        self.total_logs_scanned += 1
        rel_path = os.path.relpath(file_path, self.root)
        try:
            with open(file_path, "r", encoding="utf-8", errors="replace") as f:
                data = json.load(f)
        except Exception as e:
            return

        # Check for high stalemate or timeout rates in trainer runs
        stalemates = data.get("stalemate_rate") or data.get("stalemates", 0)
        rounds = data.get("total_rounds") or data.get("rounds", 1)
        if isinstance(stalemates, (int, float)) and rounds > 0:
            rate = stalemates / rounds if stalemates > 1 else stalemates
            if rate > 0.40:
                title = f"Night trainer elevated stalemate rate: {rate * 100:.1f}% ({stalemates}/{rounds} rounds)"
                self.record_issue("TRAINER_STALEMATE", title, json.dumps(data), rel_path, "MEDIUM")

    def scan_directory(self, dir_path: str, max_age_hours: Optional[float] = 48.0):
        if not os.path.exists(dir_path):
            return
        cutoff_time = (datetime.datetime.now() - datetime.timedelta(hours=max_age_hours)).timestamp() if max_age_hours else 0.0

        for root_dir, dirs, files in os.walk(dir_path):
            # Prune directory search to avoid backup copies, genetic trainer iterations, and deep caches
            dirs[:] = [d for d in dirs if not d.lower().startswith("backup") and not d.startswith("gen_") and d not in ["candidates", "Intermediate", "SourceArt", "Cooked", "Collections", ".git", "seeds", "pmc-main", "pmc-v2", "pmc-v3", "pmc-v4"]]
            for file in files:
                full_path = os.path.join(root_dir, file)
                try:
                    mtime = os.path.getmtime(full_path)
                    if mtime < cutoff_time:
                        continue
                    size = os.path.getsize(full_path)
                    if size > 25 * 1024 * 1024:  # skip files > 25MB
                        continue
                except OSError:
                    continue

                ext = os.path.splitext(file)[1].lower()
                if ext in [".log", ".txt"]:
                    self.parse_unreal_log(full_path)
                elif ext == ".jsonl":
                    self.parse_telemetry_jsonl(full_path)
                elif ext == ".json" and ("summary" in file or "report" in file or "result" in file):
                    self.parse_trainer_summary(full_path)

    def scan_all(self, max_age_hours: Optional[float] = 48.0):
        # Scan targeted automated output directories (Saved/Automation, Saved/Orchestrator, Saved/Trainer)
        dirs_to_scan = [
            os.path.join(self.root, "Saved", "Automation"),
            os.path.join(self.root, "Saved", "Orchestrator"),
            os.path.join(self.root, "Saved", "Trainer"),
        ]
        for d in dirs_to_scan:
            self.scan_directory(d, max_age_hours=max_age_hours)

    def generate_report_markdown(self) -> str:
        date_heading = self.date_str
        total_clusters = len(self.clusters)
        blockers = sum(1 for c in self.clusters.values() if c.severity == "BLOCKER")
        highs = sum(1 for c in self.clusters.values() if c.severity == "HIGH")
        mediums = sum(1 for c in self.clusters.values() if c.severity == "MEDIUM")
        lows = sum(1 for c in self.clusters.values() if c.severity == "LOW")

        md = []
        md.append(f"# Automated Morning QA Digest — {date_heading}")
        md.append("")
        md.append(f"**Generated:** {datetime.datetime.now().strftime('%Y-%m-%d %H:%M:%S')}  ")
        md.append(f"**Logs Scanned:** {self.total_logs_scanned} files ({self.total_lines_scanned:,} lines)  ")
        md.append(f"**Total Clustered Issues:** {total_clusters} (🔴 {blockers} Blocker, 🟠 {highs} High, 🟡 {mediums} Medium, ⚪ {lows} Low)")
        md.append("")
        md.append("---")
        md.append("")
        md.append("## 1. Proposed QA Tasks for TASK_BOARD.md")
        md.append("")

        if not self.clusters:
            md.append("🎉 **No automated defects or hitches detected in scanned sessions.** All runs green.")
            md.append("")
        else:
            md.append("| QA-ID | Category | Severity | Issue Summary | Hits | Proposed Assignee |")
            md.append("| :--- | :--- | :---: | :--- | :---: | :--- |")
            
            # Sort clusters: BLOCKER -> HIGH -> MEDIUM -> LOW, then count
            sev_rank = {"BLOCKER": 0, "HIGH": 1, "MEDIUM": 2, "LOW": 3}
            sorted_clusters = sorted(self.clusters.values(), key=lambda c: (sev_rank.get(c.severity, 4), -c.count))

            for idx, c in enumerate(sorted_clusters, start=1):
                qa_id = f"QA-{self.date_str.replace('-', '')[-4:]}-{idx:02d}"
                assignee = "Codex" if c.category in ["CRASH_ASSERT", "PERF_HITCH", "MON_STUCK", "MON_FALL_THROUGH"] else "Claude / Gemini"
                clean_title = c.title.replace("|", "/")[:65]
                md.append(f"| **{qa_id}** | `{c.category}` | **{c.severity}** | {clean_title} | {c.count} | {assignee} |")
            md.append("")

        md.append("---")
        md.append("")
        md.append("## 2. Clustered Defect Details")
        md.append("")

        if self.clusters:
            for idx, c in enumerate(sorted_clusters, start=1):
                qa_id = f"QA-{self.date_str.replace('-', '')[-4:]}-{idx:02d}"
                md.append(f"### {qa_id}: {c.title}")
                md.append(f"- **Severity:** `{c.severity}` | **Category:** `{c.category}` | **Fingerprint:** `{c.fingerprint}`")
                md.append(f"- **Occurrences:** {c.count} in {len(c.sources)} file(s)")
                md.append(f"- **Sources:** `{', '.join(c.sources[:4])}`")
                if c.samples:
                    md.append("- **Log Excerpt:**")
                    md.append("  ```text")
                    for sample in c.samples:
                        for line in sample.splitlines()[:5]:
                            md.append(f"  {line}")
                    md.append("  ```")
                md.append("")

        md.append("---")
        md.append("")
        md.append("## 3. C++ Passive Runtime Sensors Backlog (Codex Follow-Ups)")
        md.append("")
        md.append("The following passive runtime watchdog sensors from `docs/design/AUTO_QA.md` §2 are recommended for C++ implementation in `Source/CodexTactics/`:")
        md.append("")
        md.append("| Sensor ID | Condition Monitored | Trigger Threshold | Target Component |")
        md.append("| :--- | :--- | :--- | :--- |")
        md.append("| `MON_STUCK` | Pawn ordered to move but $\\Delta P < 5\\text{ cm}$ | Duration $> 4.0\\text{ s}$ on NavMesh | `AOperativeAIController` / `EnemyAIController` |")
        md.append("| `MON_OFF_NAVMESH` | Pawn center of mass outside valid bounds | Distance to nav $> 60\\text{ cm}$ | `AOperativeCharacter` |")
        md.append("| `MON_FIRE_ARC` | Weapon fired outside valid barrel angle (#33) | Residual angle $> 30.0^\\circ$ (prone $> 15.0^\\circ$) | `AOperativeCharacterCombat` |")
        md.append("| `MON_WEAPON_REACH`| Shot attempted past max weapon distance (#39) | Distance $> \\text{attack\\_range\\_m} \\times 100$ | `WeaponRangeRules` |")
        md.append("| `MON_COVER_EXPOSURE`| Operative in cover taking unmitigated hits | Shot ray hits center without wall trace hit | `AOperativeCharacterCover` |")
        md.append("| `MON_FALL_THROUGH`| Pawn drops below lowest floor geometry | Pawn $Z < (\\text{Map Floor Min } Z - 200\\text{ cm})$ | `LedgeFallComponent` |")
        md.append("| `MON_T_POSE` | AnimInstance default pose while moving | Velocity $> 50\\text{ cm/s}$ and AnimSpeed $= 0$ for $> 1.5\\text{ s}$ | `UOperativeAnimInstance` |")
        md.append("")
        return "\n".join(md) + "\n"

    def write_report(self, output_path: Optional[str] = None) -> str:
        if not output_path:
            output_path = os.path.join(self.root, "Saved", "Orchestrator", "qa", f"morning-{self.date_str}.md")
        os.makedirs(os.path.dirname(os.path.abspath(output_path)), exist_ok=True)
        content = self.generate_report_markdown()
        with open(output_path, "w", encoding="utf-8") as f:
            f.write(content)
        return output_path


def main():
    parser = argparse.ArgumentParser(description="Auto-QA Morning Report Generator (#41)")
    parser.add_argument("--root", type=str, default=".", help="Root directory of the repository")
    parser.add_argument("--date", type=str, default="", help="Report date (YYYY-MM-DD)")
    parser.add_argument("--out", type=str, default="", help="Explicit report output path")
    parser.add_argument("--telemetry", type=str, default="", help="Direct telemetry file to parse")
    parser.add_argument("--hours", type=float, default=48.0, help="Max file age in hours to scan (default: 48)")
    parser.add_argument("--json", action="store_true", help="Print json clusters summary to stdout")
    args = parser.parse_args()

    reporter = MorningQAReporter(root_dir=args.root, report_date=args.date)
    if args.telemetry and os.path.exists(args.telemetry):
        reporter.parse_telemetry_jsonl(args.telemetry)
    reporter.scan_all(max_age_hours=args.hours)
    out_file = reporter.write_report(args.out if args.out else None)
    print(f"[AutoQA] Morning report written to: {out_file}")
    print(f"[AutoQA] Scanned {reporter.total_logs_scanned} files, clustered {len(reporter.clusters)} issues.")

    if args.json:
        data = {fp: c.to_dict() for fp, c in reporter.clusters.items()}
        print(json.dumps(data, indent=2))


if __name__ == "__main__":
    main()
