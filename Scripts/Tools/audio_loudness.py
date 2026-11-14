"""Measure loudness, true peak, duration, and edge silence for audio WAV sets, and optionally export normalized copies.

Uses FFmpeg's EBU R128 (`ebur128=peak=true`), `silencedetect`, and two-pass `loudnorm`
(no third-party Python audio packages required).

Targets follow `docs/design/audio/SOUND_BIBLE.md` section 4 & 5:
  - VO (`voice/`):     -18.0 LUFS integrated, <= -1.0 dBTP, trim leading/trailing silence, 48 kHz 16-bit WAV
  - Music (`music/`): -23.0 LUFS integrated, <= -1.0 dBTP, preserve loop boundaries, 48 kHz 24-bit WAV
"""
from __future__ import annotations

import argparse
import json
import re
import shutil
import struct
import subprocess
import sys
import wave
from pathlib import Path
from typing import Any, Callable

DEFAULT_IN_DIR_CANDIDATES = (
    Path("Saved/Orchestrator/audio_v1"),
    Path("Saved/AudioGen"),
)

TARGET_LUFS_BY_KIND: dict[str, float] = {
    "voice": -18.0,
    "music": -23.0,
    "other": -18.0,
}
TARGET_PEAK_DBTP = -1.0
LUFS_TOLERANCE = 1.5

SILENCE_THRESHOLD_DB = -45.0
MIN_SILENCE_DETECT_SEC = 0.02
VO_TRIM_PAD_SEC = 0.015
VO_EDGE_SILENCE_WARN_SEC = 0.15

_EBUR128_I_RE = re.compile(r"Integrated\s+loudness:\s*\r?\n\s*I:\s*(-?\d+(?:\.\d+)?|-?inf)\s*LUFS", re.IGNORECASE)
_EBUR128_PEAK_RE = re.compile(r"True\s+peak:\s*\r?\n\s*Peak:\s*(-?\d+(?:\.\d+)?|-?inf)\s*dB(?:FS|TP)", re.IGNORECASE)
_SILENCE_START_RE = re.compile(r"silence_start:\s*(-?\d+(?:\.\d+)?)")
_SILENCE_END_RE = re.compile(r"silence_end:\s*(-?\d+(?:\.\d+)?)\s*\|\s*silence_duration:\s*(-?\d+(?:\.\d+)?)")


def require_ffmpeg(explicit: str | None = None) -> str:
    exe = explicit or shutil.which("ffmpeg")
    if not exe:
        raise RuntimeError("ffmpeg executable was not found on PATH.")
    return exe


def resolve_default_in_dir(cwd: Path | None = None) -> Path:
    base = cwd or Path.cwd()
    for rel in DEFAULT_IN_DIR_CANDIDATES:
        candidate = base / rel
        if candidate.is_dir():
            return candidate
    return base / DEFAULT_IN_DIR_CANDIDATES[0]


def classify_kind(wav_path: Path, root_dir: Path | None = None) -> str:
    parts = [p.lower() for p in (wav_path.relative_to(root_dir).parts if root_dir and _is_relative(wav_path, root_dir) else wav_path.parts)]
    if "voice" in parts or "vo" in parts:
        return "voice"
    if "music" in parts:
        return "music"
    name = wav_path.stem.lower()
    if name.startswith(("cmd_", "eng_", "med_", "pmc", "vo_")):
        return "voice"
    if name.startswith(("explore_", "tension", "combat", "victory", "defeat", "menu", "mus_")):
        return "music"
    return "other"


def _is_relative(path: Path, root: Path) -> bool:
    try:
        path.resolve().relative_to(root.resolve())
        return True
    except ValueError:
        return False


def read_wav_info(wav_path: Path) -> dict[str, Any]:
    try:
        with wave.open(str(wav_path), "rb") as w:
            rate = int(w.getframerate() or 1)
            frames = int(w.getnframes())
            channels = int(w.getnchannels())
            sample_width = int(w.getsampwidth())
        return {
            "duration_sec": round(frames / float(rate), 3),
            "sample_rate": rate,
            "channels": channels,
            "bit_depth": sample_width * 8,
        }
    except wave.Error:
        pass

    # Fallback RIFF parser for WAVE_FORMAT_EXTENSIBLE (0xFFFE / 65534, e.g. ffmpeg 24-bit stereo WAV)
    with wav_path.open("rb") as f:
        riff = f.read(12)
        if len(riff) < 12 or riff[:4] != b"RIFF" or riff[8:12] != b"WAVE":
            raise ValueError(f"Unsupported or corrupt WAV container: {wav_path}")
        channels = 1
        rate = 1
        bits_per_sample = 16
        block_align = 2
        data_size = 0
        while True:
            hdr = f.read(8)
            if len(hdr) < 8:
                break
            chunk_id = hdr[:4]
            chunk_size = struct.unpack_from("<I", hdr, 4)[0]
            if chunk_id == b"fmt " and chunk_size >= 16:
                fmt = f.read(16)
                _fmt_tag, channels, rate, _byte_rate, block_align, bits_per_sample = struct.unpack_from("<HHIIHH", fmt, 0)
                rem = chunk_size - 16 + (chunk_size & 1)
                if rem > 0:
                    f.seek(rem, 1)
            else:
                if chunk_id == b"data":
                    data_size = chunk_size
                f.seek(chunk_size + (chunk_size & 1), 1)
    frames = data_size // max(1, block_align)
    return {
        "duration_sec": round(frames / float(max(1, rate)), 3),
        "sample_rate": int(rate),
        "channels": int(channels),
        "bit_depth": int(bits_per_sample),
    }


def _to_float_or_none(raw: str | None) -> float | None:
    if raw is None:
        return None
    low = raw.strip().lower()
    if low in ("inf", "-inf", "nan", "-nan"):
        return None
    return round(float(low), 2)


def parse_ebur128_summary(stderr_text: str) -> tuple[float | None, float | None]:
    i_matches = _EBUR128_I_RE.findall(stderr_text)
    p_matches = _EBUR128_PEAK_RE.findall(stderr_text)
    integrated = _to_float_or_none(i_matches[-1]) if i_matches else None
    true_peak = _to_float_or_none(p_matches[-1]) if p_matches else None
    return integrated, true_peak


def parse_silencedetect(stderr_text: str, duration_sec: float, edge_eps: float = 0.015) -> tuple[float, float]:
    starts: list[float] = []
    spans: list[tuple[float, float]] = []
    for line in stderr_text.splitlines():
        m_start = _SILENCE_START_RE.search(line)
        if m_start:
            starts.append(float(m_start.group(1)))
            continue
        m_end = _SILENCE_END_RE.search(line)
        if m_end:
            end_t = float(m_end.group(1))
            dur = float(m_end.group(2))
            start_t = starts.pop(0) if starts else max(0.0, end_t - dur)
            spans.append((start_t, end_t))
    for open_start in starts:
        spans.append((open_start, duration_sec))

    leading = 0.0
    trailing = 0.0
    for start_t, end_t in spans:
        if start_t <= edge_eps:
            leading = max(leading, min(duration_sec, end_t) - start_t)
        if duration_sec - end_t <= edge_eps:
            trailing = max(trailing, min(duration_sec, end_t) - start_t)
    return round(leading, 3), round(trailing, 3)


def _run_ffmpeg(cmd: list[str], runner: Callable[..., subprocess.CompletedProcess[str]] = subprocess.run) -> subprocess.CompletedProcess[str]:
    proc = runner(cmd, capture_output=True, text=True, check=False)
    if proc.returncode != 0:
        raise RuntimeError(f"ffmpeg failed ({proc.returncode}): {proc.stderr[-600:]}")
    return proc


def measure_wav(
    wav_path: Path,
    root_dir: Path | None = None,
    ffmpeg_exe: str | None = None,
    silence_db: float = SILENCE_THRESHOLD_DB,
    runner: Callable[..., subprocess.CompletedProcess[str]] = subprocess.run,
) -> dict[str, Any]:
    exe = require_ffmpeg(ffmpeg_exe)
    info = read_wav_info(wav_path)
    kind = classify_kind(wav_path, root_dir)

    ebur_proc = _run_ffmpeg(
        [exe, "-hide_banner", "-nostats", "-i", str(wav_path), "-filter_complex", "ebur128=peak=true", "-f", "null", "-"],
        runner=runner,
    )
    integrated_lufs, true_peak_dbtp = parse_ebur128_summary(ebur_proc.stderr)

    sil_proc = _run_ffmpeg(
        [
            exe,
            "-hide_banner",
            "-nostats",
            "-i",
            str(wav_path),
            "-af",
            f"silencedetect=noise={silence_db}dB:d={MIN_SILENCE_DETECT_SEC}",
            "-f",
            "null",
            "-",
        ],
        runner=runner,
    )
    leading_sil, trailing_sil = parse_silencedetect(sil_proc.stderr, info["duration_sec"])

    target_lufs = TARGET_LUFS_BY_KIND.get(kind, -18.0)
    delta_lufs = round(integrated_lufs - target_lufs, 2) if integrated_lufs is not None else None
    peak_over_db = round(true_peak_dbtp - TARGET_PEAK_DBTP, 2) if true_peak_dbtp is not None else None

    issues: list[str] = []
    if integrated_lufs is None:
        issues.append("silent_or_unmeasurable")
    elif abs(delta_lufs or 0.0) > LUFS_TOLERANCE:
        issues.append("lufs_off_target")
    if true_peak_dbtp is not None and true_peak_dbtp > TARGET_PEAK_DBTP:
        issues.append("peak_over_-1dBTP")
    if kind == "voice" and (leading_sil > VO_EDGE_SILENCE_WARN_SEC or trailing_sil > VO_EDGE_SILENCE_WARN_SEC):
        issues.append("trim_edge_silence")

    rel = wav_path.relative_to(root_dir).as_posix() if root_dir and _is_relative(wav_path, root_dir) else wav_path.name
    return {
        "id": wav_path.stem,
        "kind": kind,
        "rel_path": rel,
        "duration_sec": info["duration_sec"],
        "sample_rate": info["sample_rate"],
        "channels": info["channels"],
        "bit_depth": info["bit_depth"],
        "integrated_lufs": integrated_lufs,
        "true_peak_dbtp": true_peak_dbtp,
        "leading_silence_sec": leading_sil,
        "trailing_silence_sec": trailing_sil,
        "target_lufs": target_lufs,
        "target_peak_dbtp": TARGET_PEAK_DBTP,
        "delta_lufs": delta_lufs,
        "peak_over_db": peak_over_db,
        "status": "OK" if not issues else ",".join(issues),
    }


def collect_wavs(in_dir: Path) -> list[Path]:
    if not in_dir.exists():
        raise FileNotFoundError(f"Input directory does not exist: {in_dir}")
    wavs = sorted(p for p in in_dir.rglob("*.wav") if p.is_file())
    if not wavs:
        raise FileNotFoundError(f"No WAV files found under: {in_dir}")
    return wavs


def compute_trim_bounds(
    duration_sec: float,
    leading_sil: float,
    trailing_sil: float,
    pad_sec: float = VO_TRIM_PAD_SEC,
) -> tuple[float, float] | None:
    trim_start = max(0.0, round(leading_sil - pad_sec, 4)) if leading_sil > pad_sec else 0.0
    trim_end = min(duration_sec, round(duration_sec - max(0.0, trailing_sil - pad_sec), 4)) if trailing_sil > pad_sec else duration_sec
    if trim_end - trim_start < 0.05:
        return None
    if trim_start == 0.0 and trim_end == duration_sec:
        return None
    return trim_start, trim_end


def _extract_loudnorm_json(stderr_text: str) -> dict[str, Any] | None:
    blocks = re.findall(r"\{\s*\"input_i\"\s*:.*?\}", stderr_text, re.DOTALL)
    if not blocks:
        return None
    try:
        return json.loads(blocks[-1])
    except json.JSONDecodeError:
        return None


def _is_finite_str(val: Any) -> bool:
    if val is None:
        return False
    try:
        num = float(val)
        return -100.0 < num < 50.0
    except (TypeError, ValueError):
        return False


def normalize_wav(
    src: Path,
    dst: Path,
    kind: str,
    trim_voice_silence: bool = True,
    target_sample_rate: int = 48000,
    ffmpeg_exe: str | None = None,
    runner: Callable[..., subprocess.CompletedProcess[str]] = subprocess.run,
) -> dict[str, Any]:
    if src.resolve() == dst.resolve():
        raise ValueError(f"Refusing to overwrite original WAV in place: {src}")

    exe = require_ffmpeg(ffmpeg_exe)
    pre = measure_wav(src, root_dir=src.parent.parent if src.parent.name in ("voice", "music") else src.parent, ffmpeg_exe=exe, runner=runner)
    target_lufs = TARGET_LUFS_BY_KIND.get(kind, -18.0)

    filters: list[str] = []
    trim_bounds: tuple[float, float] | None = None
    if trim_voice_silence and kind == "voice":
        trim_bounds = compute_trim_bounds(pre["duration_sec"], pre["leading_silence_sec"], pre["trailing_silence_sec"])
        if trim_bounds is not None:
            start_s, end_s = trim_bounds
            filters.append(f"atrim=start={start_s:.4f}:end={end_s:.4f}")
            filters.append("asetpts=PTS-STARTPTS")

    # Pass 1: measure trimmed audio using loudnorm JSON analysis
    p1_filters = list(filters) + [f"loudnorm=I={target_lufs:.1f}:TP={TARGET_PEAK_DBTP:.1f}:LRA=11:print_format=json"]
    p1_proc = _run_ffmpeg(
        [exe, "-hide_banner", "-nostats", "-i", str(src), "-af", ",".join(p1_filters), "-ar", str(target_sample_rate), "-f", "null", "-"],
        runner=runner,
    )
    stats = _extract_loudnorm_json(p1_proc.stderr)

    dst.parent.mkdir(parents=True, exist_ok=True)
    codec = "pcm_s24le" if kind == "music" else "pcm_s16le"

    can_two_pass_linear = (
        stats is not None
        and _is_finite_str(stats.get("input_i"))
        and _is_finite_str(stats.get("input_tp"))
        and _is_finite_str(stats.get("input_lra"))
        and _is_finite_str(stats.get("input_thresh"))
        and _is_finite_str(stats.get("target_offset"))
    )

    if can_two_pass_linear and stats is not None:
        ln_filter = (
            f"loudnorm=I={target_lufs:.1f}:TP={TARGET_PEAK_DBTP:.1f}:LRA=11:"
            f"measured_I={stats['input_i']}:measured_TP={stats['input_tp']}:"
            f"measured_LRA={stats['input_lra']}:measured_thresh={stats['input_thresh']}:"
            f"offset={stats['target_offset']}:linear=true"
        )
        p2_filters = list(filters) + [ln_filter]
    else:
        gain_db = (target_lufs - pre["integrated_lufs"]) if pre["integrated_lufs"] is not None else 0.0
        # Limit peak to -1.0 dBTP (10 ** (-1 / 20) ~= 0.89125)
        p2_filters = list(filters) + [f"volume={gain_db:.3f}dB", "alimiter=limit=0.89125:level=disabled"]

    # Always apply tiny 4ms edge fades on trimmed VO to eliminate zero-crossing clicks.
    if trim_bounds is not None:
        trimmed_dur = max(0.05, trim_bounds[1] - trim_bounds[0])
        fade_s = min(0.004, trimmed_dur * 0.1)
        p2_filters.append(f"afade=t=in:st=0:d={fade_s:.4f}")
        p2_filters.append(f"afade=t=out:st={max(0.0, trimmed_dur - fade_s):.4f}:d={fade_s:.4f}")

    tmp_dst = dst.with_suffix(".wav.tmp")
    _run_ffmpeg(
        [
            exe,
            "-y",
            "-hide_banner",
            "-nostats",
            "-i",
            str(src),
            "-af",
            ",".join(p2_filters),
            "-ar",
            str(target_sample_rate),
            "-c:a",
            codec,
            "-f",
            "wav",
            str(tmp_dst),
        ],
        runner=runner,
    )
    tmp_dst.replace(dst)

    post = measure_wav(dst, root_dir=dst.parent.parent if dst.parent.name in ("voice", "music") else dst.parent, ffmpeg_exe=exe, runner=runner)
    post["trimmed"] = trim_bounds is not None
    post["source_rel_path"] = pre["rel_path"]
    post["source_lufs"] = pre["integrated_lufs"]
    post["source_peak_dbtp"] = pre["true_peak_dbtp"]
    post["source_duration_sec"] = pre["duration_sec"]
    return post


def format_markdown_report(records: list[dict[str, Any]], source_dir: Path, title: str = "Audio Loudness & Peak Report") -> str:
    vo = [r for r in records if r["kind"] == "voice"]
    mus = [r for r in records if r["kind"] == "music"]

    def _summary(items: list[dict[str, Any]], label: str, tgt: float) -> str:
        if not items:
            return f"- **{label}:** 0 files"
        vals = [r["integrated_lufs"] for r in items if r["integrated_lufs"] is not None]
        peaks = [r["true_peak_dbtp"] for r in items if r["true_peak_dbtp"] is not None]
        ok = sum(1 for r in items if r["status"] == "OK")
        min_l = min(vals) if vals else float("nan")
        max_l = max(vals) if vals else float("nan")
        mean_l = sum(vals) / len(vals) if vals else float("nan")
        max_p = max(peaks) if peaks else float("nan")
        return (
            f"- **{label} ({len(items)} files, target {tgt:.1f} LUFS / <= {TARGET_PEAK_DBTP:.1f} dBTP):** "
            f"LUFS min `{min_l:.1f}` / mean `{mean_l:.1f}` / max `{max_l:.1f}`, "
            f"max true peak `{max_p:.1f} dBTP`, within bible thresholds `{ok}/{len(items)}`"
        )

    lines = [
        f"# {title}",
        "",
        f"- **Tool:** `Scripts/Tools/audio_loudness.py` (FFmpeg `ebur128=peak=true` + `silencedetect`; no extra Python installs)",
        f"- **Source Dir:** `{source_dir.as_posix()}` (`{len(records)}` WAV files)",
        _summary(vo, "Voice (VO)", TARGET_LUFS_BY_KIND["voice"]),
        _summary(mus, "Music", TARGET_LUFS_BY_KIND["music"]),
        "",
        "| id | kind | dur (s) | sample/bit | LUFS (tgt) | delta | peak dBTP | lead sil (s) | trail sil (s) | status |",
        "|---|---|---:|---|---:|---:|---:|---:|---:|---|",
    ]
    for r in records:
        lufs_s = f"{r['integrated_lufs']:.1f}" if r["integrated_lufs"] is not None else "inf"
        delta_s = f"{r['delta_lufs']:+.1f}" if r["delta_lufs"] is not None else "-"
        peak_s = f"{r['true_peak_dbtp']:.1f}" if r["true_peak_dbtp"] is not None else "inf"
        lines.append(
            f"| `{r['id']}` | {r['kind']} | {r['duration_sec']:.2f} | {r['sample_rate']}Hz/{r['bit_depth']}b | "
            f"{lufs_s} ({r['target_lufs']:.0f}) | {delta_s} | {peak_s} | "
            f"{r['leading_silence_sec']:.2f} | {r['trailing_silence_sec']:.2f} | {r['status']} |"
        )
    lines.append("")
    return "\n".join(lines)


def run_loudness_pass(
    in_dir: Path,
    out_dir: Path | None = None,
    normalize: bool = False,
    trim_voice_silence: bool = True,
    ffmpeg_exe: str | None = None,
) -> dict[str, Any]:
    in_resolved = in_dir.resolve()
    if normalize:
        if out_dir is None:
            raise ValueError("--out <dir> is required when --normalize is enabled.")
        out_resolved = out_dir.resolve()
        if out_resolved == in_resolved:
            raise ValueError(f"--out directory must be distinct from input directory ({in_resolved}) to never overwrite originals.")

    wavs = collect_wavs(in_resolved)
    measured = [measure_wav(p, root_dir=in_resolved, ffmpeg_exe=ffmpeg_exe) for p in wavs]

    normalized: list[dict[str, Any]] = []
    if normalize and out_dir is not None:
        out_resolved = out_dir.resolve()
        for src, rec in zip(wavs, measured):
            rel = src.relative_to(in_resolved)
            dst = out_resolved / rel
            norm_rec = normalize_wav(
                src=src,
                dst=dst,
                kind=rec["kind"],
                trim_voice_silence=trim_voice_silence,
                ffmpeg_exe=ffmpeg_exe,
            )
            normalized.append(norm_rec)

    return {
        "method": "ffmpeg_ebur128",
        "in_dir": in_resolved.as_posix(),
        "out_dir": out_dir.resolve().as_posix() if (normalize and out_dir is not None) else None,
        "count": len(measured),
        "measured": measured,
        "normalized": normalized,
        "markdown": format_markdown_report(measured, in_resolved),
        "normalized_markdown": format_markdown_report(normalized, out_dir.resolve(), title="Normalized Audio Report") if normalized and out_dir is not None else None,
    }


def build_parser() -> argparse.ArgumentParser:
    parser = argparse.ArgumentParser(description="Measure WAV loudness/peaks/silence via FFmpeg ebur128 and export normalized copies.")
    parser.add_argument("--in-dir", type=Path, default=None, help="Input directory with voice/*.wav and music/*.wav")
    parser.add_argument("--normalize", action="store_true", help="Write normalized copies into --out (never overwrites originals)")
    parser.add_argument("--out", type=Path, default=None, help="Output directory for normalized WAV copies")
    parser.add_argument("--no-trim-vo", action="store_true", help="Disable leading/trailing silence trimming on VO files")
    parser.add_argument("--report", type=Path, default=None, help="Write Markdown loudness table to this file path")
    parser.add_argument("--json", action="store_true", help="Print JSON output to stdout instead of Markdown table")
    return parser


def main(argv: list[str] | None = None) -> int:
    parser = build_parser()
    args = parser.parse_args(argv)
    in_dir = args.in_dir or resolve_default_in_dir()
    try:
        result = run_loudness_pass(
            in_dir=in_dir,
            out_dir=args.out,
            normalize=args.normalize,
            trim_voice_silence=not args.no_trim_vo,
        )
    except Exception as exc:  # noqa: BLE001
        print(f"error: {exc}", file=sys.stderr)
        return 1

    if args.report:
        args.report.parent.mkdir(parents=True, exist_ok=True)
        report_text = result["markdown"]
        if result["normalized_markdown"]:
            report_text = report_text + "\n---\n\n" + result["normalized_markdown"]
        args.report.write_text(report_text, encoding="utf-8")

    if args.json:
        payload = {k: v for k, v in result.items() if k not in ("markdown", "normalized_markdown")}
        print(json.dumps(payload, indent=2))
    else:
        print(result["markdown"])
        if result["normalized_markdown"]:
            print("\n" + result["normalized_markdown"])
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
