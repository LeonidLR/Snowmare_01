"""Audio generation CLI using Gemini TTS and Lyria via Google Interactions API.

Generates WAV assets into Saved/AudioGen/<kind>/<id>.wav from JSON manifests:
  - Content/Data/Audio/voice_lines.json  (`voice` subcommand, gemini-3.8-flash-tts)
  - Content/Data/Audio/music_cues.json   (`music` subcommand, lyria-3-clip-preview / lyria-3.5)

Spend is logged to Saved/AudioGen/spend.jsonl and guarded by a rolling weekly
budget cap (--budget-usd, default 40.0 USD / 7 days). Use --dry-run to inspect
plans and cost estimates without sending API requests.
"""
from __future__ import annotations

import argparse
import base64
import datetime as dt
import json
import os
import re
import shutil
import subprocess
import sys
import tempfile
import time
import urllib.error
import urllib.request
import wave
from pathlib import Path
from typing import Any, Callable

INTERACTIONS_URL = "https://generativelanguage.googleapis.com/v1beta/interactions"
API_REVISION = "2026-05-20"
DEFAULT_WEEKLY_BUDGET_USD = 40.0
DEFAULT_WINDOW_DAYS = 7
DEFAULT_TTS_MIN_INTERVAL_SEC = 6.2
MAX_HTTP_429_RETRIES = 4
DEFAULT_429_RETRY_SEC = 25.0

DEFAULT_VOICE_MANIFEST = Path("Content/Data/Audio/voice_lines.json")
DEFAULT_MUSIC_MANIFEST = Path("Content/Data/Audio/music_cues.json")
DEFAULT_OUT_DIR = Path("Saved/AudioGen")
DEFAULT_LEDGER = DEFAULT_OUT_DIR / "spend.jsonl"
DEFAULT_CATALOG = DEFAULT_OUT_DIR / "catalog.json"

# Pricing verified against https://aistudio.google.com/docs/pricing.md (2026-11).
TTS_PRICING_PER_M_TOKENS: dict[str, tuple[float, float]] = {
    "gemini-3.8-flash-tts": (0.50, 9.00),
    "gemini-3.8-flash-lite-tts": (0.25, 6.00),
}
MUSIC_PRICE_PER_REQUEST: dict[str, float] = {
    "lyria-3-clip-preview": 0.04,
    "lyria-3.5": 0.08,
}
AUDIO_TOKENS_PER_SEC = 25.0


def load_api_key() -> str:
    """Load GEMINI_API_KEY from process env (or Windows HKCU env) without printing/logging it."""
    key = os.environ.get("GEMINI_API_KEY", "").strip()
    if key:
        return key
    if sys.platform == "win32":
        try:
            import winreg  # type: ignore[import-not-found]

            with winreg.OpenKey(winreg.HKEY_CURRENT_USER, r"Environment") as reg_key:
                val, _ = winreg.QueryValueEx(reg_key, "GEMINI_API_KEY")
                if isinstance(val, str) and val.strip():
                    return val.strip()
        except OSError:
            pass
    return ""


def _redact_secret(text: str, secret: str) -> str:
    if not secret:
        return text
    return text.replace(secret, "[REDACTED]")


def load_spend_total(ledger_path: Path, window_days: int = DEFAULT_WINDOW_DAYS, now: dt.datetime | None = None) -> float:
    """Sum recorded USD spend within the rolling window_days window (0 = all time)."""
    if not ledger_path.exists():
        return 0.0
    ref_now = now or dt.datetime.now(dt.timezone.utc)
    cutoff = ref_now - dt.timedelta(days=window_days) if window_days > 0 else None
    total = 0.0
    with ledger_path.open("r", encoding="utf-8") as fh:
        for line in fh:
            line = line.strip()
            if not line:
                continue
            try:
                entry = json.loads(line)
            except json.JSONDecodeError:
                continue
            if cutoff is not None:
                when_raw = str(entry.get("when", ""))
                try:
                    parsed = dt.datetime.fromisoformat(when_raw.replace("Z", "+00:00"))
                    if parsed.tzinfo is None:
                        parsed = parsed.replace(tzinfo=dt.timezone.utc)
                    if parsed < cutoff:
                        continue
                except ValueError:
                    pass
            try:
                total += float(entry.get("usd", 0.0))
            except (TypeError, ValueError):
                pass
    return total


def append_spend_entry(ledger_path: Path, entry: dict[str, Any]) -> None:
    ledger_path.parent.mkdir(parents=True, exist_ok=True)
    with ledger_path.open("a", encoding="utf-8") as fh:
        fh.write(json.dumps(entry, ensure_ascii=False) + "\n")


def load_catalog(catalog_path: Path) -> dict[str, Any]:
    if not catalog_path.exists():
        return {"version": 1, "items": {}}
    try:
        data = json.loads(catalog_path.read_text(encoding="utf-8"))
        if isinstance(data, dict) and isinstance(data.get("items"), dict):
            return data
    except (OSError, json.JSONDecodeError):
        pass
    return {"version": 1, "items": {}}


def save_catalog_atomic(catalog_path: Path, catalog: dict[str, Any]) -> None:
    catalog_path.parent.mkdir(parents=True, exist_ok=True)
    fd, tmp_name = tempfile.mkstemp(prefix=".catalog_", suffix=".tmp", dir=str(catalog_path.parent))
    try:
        with os.fdopen(fd, "w", encoding="utf-8") as fh:
            json.dump(catalog, fh, indent=2, ensure_ascii=False)
            fh.write("\n")
        os.replace(tmp_name, catalog_path)
    except Exception:
        if os.path.exists(tmp_name):
            os.unlink(tmp_name)
        raise


def estimate_tts_cost_usd(text: str, style: str, model: str) -> float:
    """Conservative worst-case USD estimate for pre-flight budget checks."""
    in_price, out_price = TTS_PRICING_PER_M_TOKENS.get(model, (0.50, 9.00))
    est_in_tokens = max(10, int((len(text) + len(style)) / 3.5))
    words = max(1, len(text.split()))
    est_sec = max(2.0, words / 2.2 + 1.5)
    est_out_tokens = est_sec * AUDIO_TOKENS_PER_SEC
    return est_in_tokens / 1_000_000 * in_price + est_out_tokens / 1_000_000 * out_price


def estimate_music_cost_usd(model: str) -> float:
    return MUSIC_PRICE_PER_REQUEST.get(model, 0.08)


def compute_actual_cost_usd(
    kind: str,
    model: str,
    usage: dict[str, Any] | None,
    duration_sec: float,
    text: str = "",
    style: str = "",
) -> float:
    if kind == "music":
        return estimate_music_cost_usd(model)
    in_price, out_price = TTS_PRICING_PER_M_TOKENS.get(model, (0.50, 9.00))
    if usage:
        in_tok = float(usage.get("total_input_tokens") or usage.get("input_tokens") or 0)
        out_tok = float(usage.get("total_output_tokens") or usage.get("output_tokens") or 0)
        if in_tok > 0 or out_tok > 0:
            return in_tok / 1_000_000 * in_price + out_tok / 1_000_000 * out_price
    est_in = max(10, int((len(text) + len(style)) / 3.5))
    est_out = max(1.0, duration_sec) * AUDIO_TOKENS_PER_SEC
    return est_in / 1_000_000 * in_price + est_out / 1_000_000 * out_price


def build_tts_payload(line: dict[str, Any], model: str) -> dict[str, Any]:
    text_block: dict[str, Any] = {"type": "text", "text": str(line["text"])}
    style = str(line.get("style") or "").strip()
    if style:
        text_block["annotations"] = [{"type": "speech_metadata", "style": style}]
    return {
        "model": model,
        "input": [{"type": "user_input", "content": [text_block]}],
        "response_format": {"type": "audio"},
        "generation_config": {
            "speech_config": [{"voice": str(line.get("voice") or "Charon")}],
        },
    }


def build_music_payload(cue: dict[str, Any], model: str) -> dict[str, Any]:
    payload: dict[str, Any] = {
        "model": model,
        "input": str(cue["prompt"]),
    }
    if model == "lyria-3.5":
        payload["response_format"] = {"type": "audio", "mime_type": "audio/wav"}
    return payload


def extract_audio_from_interaction(response: dict[str, Any]) -> tuple[bytes, str, dict[str, Any]]:
    """Extract raw decoded audio bytes, mime_type, and usage dict from Interactions JSON."""
    usage = response.get("usage") if isinstance(response.get("usage"), dict) else {}

    def _scan_blocks(blocks: Any) -> tuple[bytes, str] | None:
        if not isinstance(blocks, list):
            return None
        for block in blocks:
            if not isinstance(block, dict):
                continue
            if block.get("type") == "audio" and block.get("data"):
                raw = base64.b64decode(block["data"])
                mime = str(block.get("mime_type") or "audio/wav")
                return raw, mime
        return None

    for key in ("outputs", "steps"):
        items = response.get(key)
        if not isinstance(items, list):
            continue
        for item in items:
            if not isinstance(item, dict):
                continue
            found = _scan_blocks(item.get("content"))
            if found:
                return found[0], found[1], usage
            if item.get("type") == "audio" and item.get("data"):
                raw = base64.b64decode(item["data"])
                mime = str(item.get("mime_type") or "audio/wav")
                return raw, mime, usage

    top_audio = response.get("output_audio")
    if isinstance(top_audio, dict) and top_audio.get("data"):
        raw = base64.b64decode(top_audio["data"])
        return raw, str(top_audio.get("mime_type") or "audio/wav"), usage

    raise ValueError("No audio output block found in Gemini Interactions response.")


def _find_ffmpeg() -> str | None:
    found = shutil.which("ffmpeg")
    if found:
        return found
    scoop_shim = Path.home() / "scoop" / "shims" / "ffmpeg.exe"
    if scoop_shim.exists():
        return str(scoop_shim)
    return None


def write_wav_file(raw_bytes: bytes, mime_type: str, dest_wav: Path, converter: Callable[[Path, Path], None] | None = None) -> float:
    """Write audio bytes to dest_wav as standard PCM WAV (converting MP3/etc via ffmpeg if needed) and return duration_sec."""
    dest_wav.parent.mkdir(parents=True, exist_ok=True)
    is_wav = raw_bytes[:4] == b"RIFF" and raw_bytes[8:12] == b"WAVE"
    if is_wav and "wav" in mime_type.lower():
        fd, tmp_name = tempfile.mkstemp(prefix=".wav_", suffix=".tmp", dir=str(dest_wav.parent))
        try:
            with os.fdopen(fd, "wb") as fh:
                fh.write(raw_bytes)
            os.replace(tmp_name, dest_wav)
        except Exception:
            if os.path.exists(tmp_name):
                os.unlink(tmp_name)
            raise
    else:
        ext = ".mp3" if "mpeg" in mime_type.lower() or "mp3" in mime_type.lower() else ".bin"
        if raw_bytes[:4] == b"RIFF" and raw_bytes[8:12] == b"WAVE":
            ext = ".wav"
        src_fd, src_tmp = tempfile.mkstemp(prefix=".src_", suffix=ext, dir=str(dest_wav.parent))
        dst_fd, dst_tmp = tempfile.mkstemp(prefix=".dst_", suffix=".wav", dir=str(dest_wav.parent))
        os.close(dst_fd)
        try:
            with os.fdopen(src_fd, "wb") as fh:
                fh.write(raw_bytes)
            if ext == ".wav":
                os.replace(src_tmp, dest_wav)
                if os.path.exists(dst_tmp):
                    os.unlink(dst_tmp)
            else:
                if converter is not None:
                    converter(Path(src_tmp), Path(dst_tmp))
                else:
                    ffmpeg = _find_ffmpeg()
                    if not ffmpeg:
                        raise RuntimeError(f"Cannot convert {mime_type} to WAV: ffmpeg not found on PATH.")
                    proc = subprocess.run(
                        [ffmpeg, "-y", "-i", src_tmp, "-acodec", "pcm_s16le", "-ar", "44100", dst_tmp],
                        capture_output=True,
                        text=True,
                    )
                    if proc.returncode != 0:
                        raise RuntimeError(f"ffmpeg conversion failed ({proc.returncode}): {proc.stderr[-300:]}")
                os.replace(dst_tmp, dest_wav)
        finally:
            for p in (src_tmp, dst_tmp):
                if os.path.exists(p):
                    try:
                        os.unlink(p)
                    except OSError:
                        pass

    with wave.open(str(dest_wav), "rb") as w:
        rate = w.getframerate() or 1
        return round(w.getnframes() / float(rate), 3)


def _parse_retry_after_seconds(detail: str, headers: Any = None) -> float:
    if headers is not None:
        raw_hdr = headers.get("Retry-After") if hasattr(headers, "get") else None
        if raw_hdr:
            try:
                return max(5.0, min(90.0, float(raw_hdr)))
            except ValueError:
                pass
    match = re.search(r"retry\s+in\s+(\d+(?:\.\d+)?)\s*s", detail, re.IGNORECASE)
    if match:
        return max(5.0, min(90.0, float(match.group(1)) + 1.0))
    return DEFAULT_429_RETRY_SEC


def post_interaction(
    payload: dict[str, Any],
    api_key: str,
    url: str = INTERACTIONS_URL,
    max_retries: int = MAX_HTTP_429_RETRIES,
    sleep_fn: Callable[[float], None] = time.sleep,
) -> dict[str, Any]:
    data = json.dumps(payload).encode("utf-8")
    for attempt in range(max_retries + 1):
        req = urllib.request.Request(
            url,
            data=data,
            method="POST",
            headers={
                "Content-Type": "application/json",
                "x-goog-api-key": api_key,
                "Api-Revision": API_REVISION,
            },
        )
        try:
            with urllib.request.urlopen(req, timeout=180) as resp:
                body = resp.read().decode("utf-8")
                return json.loads(body)
        except urllib.error.HTTPError as exc:
            detail = exc.read().decode("utf-8", errors="replace")
            if exc.code == 429 and attempt < max_retries:
                wait_sec = _parse_retry_after_seconds(detail, getattr(exc, "headers", None))
                sleep_fn(wait_sec)
                continue
            raise RuntimeError(f"HTTP {exc.code} from Interactions API: {_redact_secret(detail, api_key)}") from exc
        except urllib.error.URLError as exc:
            raise RuntimeError(f"Network error contacting Interactions API: {_redact_secret(str(exc), api_key)}") from exc
    raise RuntimeError("Exceeded max 429 retries contacting Interactions API")


def _filter_items(items: list[dict[str, Any]], selected_ids: list[str] | None) -> list[dict[str, Any]]:
    if not selected_ids:
        return items
    wanted = set()
    for token in selected_ids:
        for part in token.split(","):
            part = part.strip()
            if part:
                wanted.add(part)
    filtered = [item for item in items if str(item.get("id")) in wanted]
    missing = wanted - {str(item.get("id")) for item in filtered}
    if missing:
        raise ValueError(f"Unknown id(s): {', '.join(sorted(missing))}")
    return filtered


def run_generation(
    kind: str,
    manifest_path: Path,
    out_dir: Path,
    ledger_path: Path,
    catalog_path: Path,
    budget_usd: float,
    window_days: int = DEFAULT_WINDOW_DAYS,
    selected_ids: list[str] | None = None,
    model_override: str | None = None,
    seed: int | None = None,
    dry_run: bool = False,
    overwrite: bool = False,
    transport: Callable[[dict[str, Any], str], dict[str, Any]] | None = None,
    converter: Callable[[Path, Path], None] | None = None,
    api_key: str | None = None,
    min_interval_sec: float | None = None,
    sleep_fn: Callable[[float], None] = time.sleep,
) -> dict[str, Any]:
    manifest = json.loads(manifest_path.read_text(encoding="utf-8"))
    if kind == "voice":
        items = _filter_items(list(manifest.get("lines") or []), selected_ids)
        default_model = str(model_override or manifest.get("model") or "gemini-3.8-flash-tts")
    elif kind == "music":
        items = _filter_items(list(manifest.get("cues") or []), selected_ids)
        default_model = str(model_override or manifest.get("default_model") or "lyria-3-clip-preview")
    else:
        raise ValueError(f"Unsupported kind: {kind}")

    spent_week = load_spend_total(ledger_path, window_days=window_days)
    planned: list[dict[str, Any]] = []
    skipped: list[str] = []

    for item in items:
        item_id = str(item["id"])
        wav_path = out_dir / kind / f"{item_id}.wav"
        if wav_path.exists() and not overwrite:
            skipped.append(item_id)
            continue
        model = str(model_override or item.get("model") or default_model)
        if kind == "voice":
            est_cost = estimate_tts_cost_usd(str(item["text"]), str(item.get("style") or ""), model)
            payload = build_tts_payload(item, model)
        else:
            est_cost = estimate_music_cost_usd(model)
            payload = build_music_payload(item, model)
        planned.append(
            {
                "id": item_id,
                "item": item,
                "model": model,
                "wav_path": wav_path,
                "est_cost_usd": est_cost,
                "payload": payload,
            }
        )

    total_est = sum(entry["est_cost_usd"] for entry in planned)
    if dry_run:
        return {
            "dry_run": True,
            "kind": kind,
            "budget_usd": budget_usd,
            "spent_week_usd": round(spent_week, 4),
            "estimated_usd": round(total_est, 4),
            "skipped_existing": skipped,
            "planned": [
                {
                    "id": entry["id"],
                    "model": entry["model"],
                    "wav_path": str(entry["wav_path"]),
                    "estimated_usd": round(entry["est_cost_usd"], 4),
                }
                for entry in planned
            ],
        }

    if not planned:
        return {
            "dry_run": False,
            "kind": kind,
            "generated": [],
            "skipped_existing": skipped,
            "spent_this_run_usd": 0.0,
            "spent_week_usd": round(spent_week, 4),
        }

    if spent_week + total_est > budget_usd:
        raise RuntimeError(
            f"Budget cap exceeded: weekly spent ${spent_week:.4f} + est ${total_est:.4f} > cap ${budget_usd:.2f}"
        )

    resolved_key = api_key if api_key is not None else load_api_key()
    if not resolved_key:
        raise RuntimeError("GEMINI_API_KEY is not set in environment.")

    call_api = transport or post_interaction
    if min_interval_sec is not None:
        effective_interval = max(0.0, float(min_interval_sec))
    elif transport is None and kind == "voice":
        effective_interval = DEFAULT_TTS_MIN_INTERVAL_SEC
    else:
        effective_interval = 0.0

    catalog = load_catalog(catalog_path)
    generated: list[dict[str, Any]] = []
    run_spend = 0.0

    for idx, entry in enumerate(planned):
        if idx > 0 and effective_interval > 0.0:
            sleep_fn(effective_interval)
        if spent_week + run_spend + entry["est_cost_usd"] > budget_usd:
            raise RuntimeError(
                f"Budget cap reached mid-run: spent ${spent_week + run_spend:.4f} vs cap ${budget_usd:.2f}"
            )
        response = call_api(entry["payload"], resolved_key)
        raw_audio, mime_type, usage = extract_audio_from_interaction(response)
        duration_sec = write_wav_file(raw_audio, mime_type, entry["wav_path"], converter=converter)
        item = entry["item"]
        actual_usd = compute_actual_cost_usd(
            kind,
            entry["model"],
            usage,
            duration_sec,
            text=str(item.get("text") or ""),
            style=str(item.get("style") or ""),
        )
        now_iso = dt.datetime.now(dt.timezone.utc).isoformat(timespec="seconds")
        append_spend_entry(
            ledger_path,
            {
                "when": now_iso,
                "kind": kind,
                "id": entry["id"],
                "model": entry["model"],
                "duration_sec": duration_sec,
                "usage": usage,
                "usd": round(actual_usd, 6),
            },
        )
        run_spend += actual_usd
        rel_wav = str(entry["wav_path"].as_posix())
        meta = {
            "id": entry["id"],
            "kind": kind,
            "wav_path": rel_wav,
            "model": entry["model"],
            "prompt": str(item.get("prompt") or item.get("style") or ""),
            "text": str(item.get("text") or ""),
            "speaker": str(item.get("speaker") or ""),
            "voice": str(item.get("voice") or ""),
            "style": str(item.get("style") or ""),
            "state": str(item.get("state") or ""),
            "cue_group": str(item.get("cue_group") or ""),
            "variant": str(item.get("variant") or ""),
            "loop": bool(item.get("loop", kind == "music")),
            "source": str(item.get("source") or ""),
            "category": str(item.get("category") or ""),
            "seed": seed if seed is not None else item.get("seed"),
            "duration_sec": duration_sec,
            "cost_usd": round(actual_usd, 6),
            "source_mime_type": mime_type,
            "generated_at": now_iso,
        }
        catalog["items"][f"{kind}/{entry['id']}"] = meta
        save_catalog_atomic(catalog_path, catalog)
        generated.append(meta)

    return {
        "dry_run": False,
        "kind": kind,
        "generated": generated,
        "skipped_existing": skipped,
        "spent_this_run_usd": round(run_spend, 6),
        "spent_week_usd": round(spent_week + run_spend, 4),
    }


def _add_common_args(sub: argparse.ArgumentParser, default_manifest: Path) -> None:
    sub.add_argument("--manifest", type=Path, default=default_manifest)
    sub.add_argument("--out-dir", type=Path, default=DEFAULT_OUT_DIR)
    sub.add_argument("--ledger", type=Path, default=DEFAULT_LEDGER)
    sub.add_argument("--catalog", type=Path, default=DEFAULT_CATALOG)
    sub.add_argument("--budget-usd", type=float, default=DEFAULT_WEEKLY_BUDGET_USD)
    sub.add_argument("--window-days", type=int, default=DEFAULT_WINDOW_DAYS)
    sub.add_argument("--id", dest="ids", nargs="*", default=None, help="Filter specific manifest item IDs")
    sub.add_argument("--model", default=None, help="Override model ID")
    sub.add_argument("--seed", type=int, default=None, help="Optional metadata seed tag")
    sub.add_argument("--dry-run", action="store_true", help="Print plan and cost estimate without API calls")
    sub.add_argument("--overwrite", action="store_true", help="Regenerate even if output WAV already exists")


def build_parser() -> argparse.ArgumentParser:
    ap = argparse.ArgumentParser(description="Generate CodexTactics voice barks (Gemini TTS) and music cues (Lyria).")
    sub = ap.add_subparsers(dest="command", required=True)
    _add_common_args(sub.add_parser("voice", help="Generate character voice barks"), DEFAULT_VOICE_MANIFEST)
    _add_common_args(sub.add_parser("music", help="Generate music cues"), DEFAULT_MUSIC_MANIFEST)
    return ap


def main(argv: list[str] | None = None) -> int:
    args = build_parser().parse_args(argv)
    try:
        result = run_generation(
            kind=args.command,
            manifest_path=args.manifest,
            out_dir=args.out_dir,
            ledger_path=args.ledger,
            catalog_path=args.catalog,
            budget_usd=args.budget_usd,
            window_days=args.window_days,
            selected_ids=args.ids,
            model_override=args.model,
            seed=args.seed,
            dry_run=args.dry_run,
            overwrite=args.overwrite,
        )
    except Exception as exc:
        print(f"error: {exc}", file=sys.stderr)
        return 2
    print(json.dumps(result, indent=2, ensure_ascii=False))
    return 0


if __name__ == "__main__":
    sys.exit(main())
