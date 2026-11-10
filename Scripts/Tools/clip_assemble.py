"""Turns the in-engine clip recorder's frames (CodexTactics.ClipRecorder, Saved/Screenshots/Clips/<name>/f_NNNNN.jpg, or .png with its
format=png) into phone-sized
mp4s for the level generator pipeline (Scripts/levelgen_record_run.ps1, docs/design/levelgen/LEVEL_GENERATOR.md section 8). Never a
desktop capture: the frames are the game's own view.

  watch    --frames DIR --segments DIR --pid N [--fps 30] [--seg-frames 900] [--status FILE]
           while the game (pid N) records: every full block of frames -> a real-speed segment mp4 (crf 18), the frames deleted at once
           (a 10 min run is 18000 frames, ~25 GB as PNG, a few GB as JPEG - the disk never holds more than ~2 blocks); the rest when the
           game has exited.
  assemble --segments DIR --log GAME_LOG --name NAME --out DIR [--fps 30] [--speedup 4] [--highlights 3] [--report RUN_JSON]
           [--status FILE] [--keep]
           -> <out>/<name>_full_x<K>.mp4 (the whole run sped up, a small "xK" overlay, H.264 crf 28, <= 1280x720, <= 40 MB),
              <out>/<name>_fight_<n>.mp4 (real-speed clips <= 20 s cut at the bot's logged fights: "[Bot] Route: fight at ..." placed on the
              video by the recorder's "ClipRecorder <name> f=N" lines), <out>/clips.json (the manifest). Every mp4 is checked: duration
              and the mean brightness of its middle frame (YAVG > 8 = not black / empty). Exit 0 = all good, 1 = no video, 2 = a clip failed.
  python Scripts/Tools/clip_assemble.py --test     (the pure helpers' self-test)

The H.264 encoder (2026-11-10, cheaper clips): h264_nvenc when `ffmpeg -encoders` lists it AND a one-frame probe encode works (the GPU;
the game's CPU stays free), else libx264; CODEX_CLIP_ENCODER=libx264|h264_nvenc forces one. The quality numbers (crf 18 / 26 / 28..32)
map onto nvenc's constant quality (-rc vbr -cq N -b:v 0); the size caps and the re-encode ladder are the same for both.

Manifest clips.json (schema codex.levelgen_clips/1): {name, created_utc, fps, speedup, frames, video_sec, game_log, run_report,
  files: [{kind: "full" | "fight", file (abs), name, duration_sec, size_bytes, yavg, ok, start_sec?, end_sec?, event?}],
  events: [{kind: "fight" | "cover" | "drop" | "scripted_drop" | "stuck", frame, video_sec, game_sec, text}], ok}
UE-only, no Godot reference.
"""
import datetime
import glob
import json
import os
import re
import subprocess
import sys
import time

FONT = "C\\:/Windows/Fonts/arial.ttf"
MAX_MB = 40.0
CLIP_BEFORE_SEC = 3.0
CLIP_AFTER_SEC = 16.0
MIN_YAVG = 8.0

FRAME_FILE_RE = re.compile(r"f_(\d+)\.(png|jpg)$")
X264_TO_NVENC_PRESET = {"veryfast": "p2", "fast": "p3", "medium": "p5", "slow": "p6"}
_ENCODER = None

FRAME_RE = re.compile(r"ClipRecorder (\S+) f=(\d+) t=([\d.]+)")
EVENT_RES = [
    ("fight", re.compile(r"\[Bot\] Route: fight at ([\d.]+) s (.*)")),
    ("cover", re.compile(r"\[Bot\] Route: COVER - (.*)()")),
    ("drop", re.compile(r"\[Bot\] Route: the leader drops through (\S+) at ([\d.]+) s")),
    ("scripted_drop", re.compile(r"\[Bot\] Route: SCRIPTED DROP (.*)()")),
    ("stuck", re.compile(r"\[Bot\] Route: STUCK at ([\d.]+) s (.*)")),
]


def now_utc():
    return datetime.datetime.now(datetime.timezone.utc).isoformat()


def update_status(path, **result):
    """Read-modify-write of a codex.job_status/1 file's "result" (Scripts/job_status.ps1); "message" goes to the top level."""
    if not path:
        return
    try:
        with open(path, encoding="utf-8-sig") as f:
            status = json.load(f)
    except (OSError, ValueError):
        return
    message = result.pop("message", None)
    status.setdefault("result", {})
    if not isinstance(status["result"], dict):
        status["result"] = {}
    status["result"].update(result)
    if message:
        status["message"] = message
    status["updated_utc"] = now_utc()
    tmp = path + ".py.tmp"
    with open(tmp, "w", encoding="utf-8") as f:
        json.dump(status, f, indent=2)
    os.replace(tmp, path)


def run(cmd):
    p = subprocess.run(cmd, stdout=subprocess.PIPE, stderr=subprocess.PIPE, text=True, encoding="utf-8", errors="replace")
    return p.returncode, p.stdout + p.stderr


def pid_alive(pid):
    if os.name == "nt":
        out = subprocess.run(["tasklist", "/FI", "PID eq %d" % pid, "/NH"], stdout=subprocess.PIPE, text=True, errors="replace").stdout
        return str(pid) in out
    try:
        os.kill(pid, 0)
        return True
    except OSError:
        return False


def h264_encoder():
    """h264_nvenc when ffmpeg lists it and a tiny probe encode succeeds (cached), else libx264; CODEX_CLIP_ENCODER overrides."""
    global _ENCODER
    if _ENCODER:
        return _ENCODER
    forced = os.environ.get("CODEX_CLIP_ENCODER", "").strip()
    if forced in ("libx264", "h264_nvenc"):
        _ENCODER = forced
        return _ENCODER
    _ENCODER = "libx264"
    code, out = run(["ffmpeg", "-hide_banner", "-encoders"])
    if code == 0 and re.search(r"^\s*V\S*\s+h264_nvenc\b", out, re.M):
        code, _ = run(["ffmpeg", "-hide_banner", "-loglevel", "error", "-f", "lavfi", "-i", "color=c=gray:s=256x256:d=0.1", "-frames:v", "1",
                       "-c:v", "h264_nvenc", "-f", "null", "-"])
        if code == 0:
            _ENCODER = "h264_nvenc"
    return _ENCODER


def h264_args(crf, preset, encoder=None):
    """The video codec arguments for an x264-style quality / preset on the chosen encoder (h264_encoder)."""
    encoder = encoder or h264_encoder()
    if encoder == "h264_nvenc":
        return ["-c:v", "h264_nvenc", "-preset", X264_TO_NVENC_PRESET.get(preset, "p4"), "-rc", "vbr", "-cq", str(crf), "-b:v", "0"]
    return ["-c:v", "libx264", "-preset", preset, "-crf", str(crf)]


def frame_files(frames_dir):
    """{frame number: file name} of the recorder's frames (f_NNNNN.jpg / .png)."""
    out = {}
    for f in os.listdir(frames_dir) if os.path.isdir(frames_dir) else []:
        m = FRAME_FILE_RE.match(f)
        if m:
            out[int(m.group(1))] = f
    return out


def frame_numbers(frames_dir):
    return sorted(frame_files(frames_dir))


def frame_ext(frames_dir, number):
    """The extension of frame `number` (jpg first: the recorder's default), "png" when neither exists."""
    for ext in ("jpg", "png"):
        if os.path.exists(os.path.join(frames_dir, "f_%05d.%s" % (number, ext))):
            return ext
    return "png"


def encode_segment(frames_dir, seg_path, start, count, fps):
    ext = frame_ext(frames_dir, start)
    code, out = run(["ffmpeg", "-y", "-loglevel", "error", "-framerate", str(fps), "-start_number", str(start), "-i", os.path.join(frames_dir, "f_%05d." + ext),
                     "-frames:v", str(count)] + h264_args(18, "veryfast") + ["-pix_fmt", "yuv420p", seg_path])
    if code != 0:
        raise RuntimeError("ffmpeg segment %s: %s" % (seg_path, out[-800:]))
    for n in range(start, start + count):
        try:
            os.remove(os.path.join(frames_dir, "f_%05d.%s" % (n, ext)))
        except OSError:
            pass


def contiguous_from(numbers, start):
    """How many frames start, start+1, ... exist (a missing frame ends the run)."""
    have = set(numbers)
    n = start
    while n in have:
        n += 1
    return n - start


def cmd_watch(a):
    os.makedirs(a.segments, exist_ok=True)
    start = 0
    seg = len(glob.glob(os.path.join(a.segments, "seg_*.mp4")))
    margin = a.fps  # a second of frames behind the newest: never read a frame still being written
    while True:
        alive = pid_alive(a.pid)
        nums = [n for n in frame_numbers(a.frames) if n >= start]
        if nums:
            start = max(start, nums[0]) if start not in nums else start
        ready = contiguous_from(nums, start)
        while ready - (0 if not alive else margin) >= a.seg_frames:
            encode_segment(a.frames, os.path.join(a.segments, "seg_%04d.mp4" % seg), start, a.seg_frames, a.fps)
            seg += 1
            start += a.seg_frames
            ready -= a.seg_frames
            update_status(a.status, frames_encoded=start, segments=seg, message="recording: %d frames encoded (%d segments)" % (start, seg))
        if not alive:
            if ready > 0:
                encode_segment(a.frames, os.path.join(a.segments, "seg_%04d.mp4" % seg), start, ready, a.fps)
                seg += 1
                start += ready
            update_status(a.status, frames_encoded=start, segments=seg, message="the game has exited: %d frames in %d segments" % (start, seg))
            print(json.dumps({"frames": start, "segments": seg}))
            return 0 if start > 0 else 1
        time.sleep(2.0)


def parse_log(lines, name):
    """The recorder's frame lines and the bot's events in log order -> (frames [(f, t)], events [{kind, frame, game_sec, text}])."""
    frames = []
    events = []
    last_frame = None
    for line in lines:
        m = FRAME_RE.search(line)
        if m and m.group(1) == name:
            last_frame = int(m.group(2))
            frames.append((last_frame, float(m.group(3))))
            continue
        if last_frame is None:
            continue
        for kind, rx in EVENT_RES:
            e = rx.search(line)
            if e:
                text = line[line.find("[Bot]"):].strip()
                game = None
                try:
                    game = float(e.group(1)) if kind in ("fight", "stuck") else (float(e.group(2)) if kind == "drop" else None)
                except (ValueError, IndexError):
                    game = None
                events.append({"kind": kind, "frame": last_frame, "game_sec": game, "text": text[:300]})
                break
    return frames, events


def pick_windows(events, fps, video_sec, count, before=CLIP_BEFORE_SEC, after=CLIP_AFTER_SEC):
    """Up to `count` real-speed windows [start, end] (<= before + after s) round the fight events, never overlapping, in time order."""
    out = []
    for e in events:
        if e["kind"] != "fight":
            continue
        t = e["frame"] / float(fps)
        start = max(0.0, t - before)
        end = min(video_sec, t + after)
        if end - start < 4.0:
            continue
        if out and start < out[-1]["end"] + 1.0:
            continue
        out.append({"start": round(start, 2), "end": round(end, 2), "event": e})
        if len(out) >= count:
            break
    return out


def probe(path):
    code, out = run(["ffprobe", "-v", "error", "-show_entries", "format=duration", "-of", "default=nw=1:nk=1", path])
    try:
        duration = float(out.strip().splitlines()[0]) if code == 0 else 0.0
    except (ValueError, IndexError):
        duration = 0.0
    code, out = run(["ffmpeg", "-v", "info", "-ss", "%.2f" % max(0.0, duration / 2.0), "-i", path, "-frames:v", "1",
                     "-vf", "signalstats,metadata=print:key=lavfi.signalstats.YAVG", "-f", "null", "-"])
    m = re.findall(r"lavfi\.signalstats\.YAVG=([\d.]+)", out)
    yavg = float(m[-1]) if m else 0.0
    size = os.path.getsize(path) if os.path.exists(path) else 0
    return {"duration_sec": round(duration, 2), "size_bytes": size, "yavg": round(yavg, 1),
            "ok": duration > 0.5 and yavg > MIN_YAVG and size <= MAX_MB * 1024 * 1024}


def encode_full(src, dst, speedup, crf=28, scale=None):
    vf = "setpts=PTS/%g,fps=30" % speedup
    if scale:
        vf += ",scale=%s" % scale
    vf += (",drawtext=fontfile='%s':text='x%g speed':x=w-tw-18:y=16:fontsize=26:fontcolor=white:box=1:boxcolor=black@0.55:boxborderw=6"
           % (FONT, speedup))
    code, out = run(["ffmpeg", "-y", "-loglevel", "error", "-i", src, "-vf", vf, "-an"] + h264_args(crf, "medium") +
                    ["-pix_fmt", "yuv420p", "-movflags", "+faststart", dst])
    if code != 0:
        raise RuntimeError("ffmpeg full: %s" % out[-800:])


def encode_clip(src, dst, start, end, crf=26):
    code, out = run(["ffmpeg", "-y", "-loglevel", "error", "-ss", "%.2f" % start, "-i", src, "-t", "%.2f" % (end - start), "-an"] +
                    h264_args(crf, "medium") + ["-pix_fmt", "yuv420p", "-movflags", "+faststart", dst])
    if code != 0:
        raise RuntimeError("ffmpeg clip: %s" % out[-800:])


def cmd_assemble(a):
    os.makedirs(a.out, exist_ok=True)
    segs = sorted(glob.glob(os.path.join(a.segments, "seg_*.mp4")))
    manifest = {"schema": "codex.levelgen_clips/1", "name": a.name, "created_utc": now_utc(), "fps": a.fps, "speedup": a.speedup, "frames": 0,
                "video_sec": 0.0, "game_log": os.path.abspath(a.log) if a.log else "", "run_report": os.path.abspath(a.report) if a.report else "",
                "files": [], "events": [], "ok": False}
    man_path = os.path.join(a.out, "clips.json")
    if not segs:
        manifest["error"] = "no recorded segments in %s" % a.segments
        json.dump(manifest, open(man_path, "w", encoding="utf-8"), indent=2)
        print(manifest["error"])
        return 1
    update_status(a.status, message="assembling %d segments" % len(segs))
    concat = os.path.join(a.segments, "concat.txt")
    with open(concat, "w", encoding="utf-8") as f:
        for s in segs:
            f.write("file '%s'\n" % os.path.abspath(s).replace("\\", "/"))
    realtime = os.path.join(a.segments, "realtime.mp4")
    code, out = run(["ffmpeg", "-y", "-loglevel", "error", "-f", "concat", "-safe", "0", "-i", concat, "-c", "copy", realtime])
    if code != 0:
        manifest["error"] = "concat: " + out[-500:]
        json.dump(manifest, open(man_path, "w", encoding="utf-8"), indent=2)
        return 1
    rt = probe(realtime)
    manifest["video_sec"] = rt["duration_sec"]
    manifest["frames"] = int(round(rt["duration_sec"] * a.fps))
    lines = open(a.log, encoding="utf-8", errors="replace").read().splitlines() if a.log and os.path.exists(a.log) else []
    _, events = parse_log(lines, a.name)
    for e in events:
        e["video_sec"] = round(e["frame"] / float(a.fps), 2)
    manifest["events"] = events
    ok = True
    # The whole run, sped up; re-encoded smaller while over the phone budget.
    full = os.path.join(a.out, "%s_full_x%g.mp4" % (a.name, a.speedup))
    for crf, scale in ((28, None), (31, None), (32, "960:540")):
        encode_full(realtime, full, a.speedup, crf, scale)
        info = probe(full)
        if info["size_bytes"] <= MAX_MB * 1024 * 1024:
            break
    entry = {"kind": "full", "file": os.path.abspath(full), "name": os.path.basename(full)}
    entry.update(info)
    manifest["files"].append(entry)
    ok &= info["ok"]
    for n, w in enumerate(pick_windows(events, a.fps, rt["duration_sec"], a.highlights), start=1):
        clip = os.path.join(a.out, "%s_fight_%d.mp4" % (a.name, n))
        encode_clip(realtime, clip, w["start"], w["end"])
        info = probe(clip)
        entry = {"kind": "fight", "file": os.path.abspath(clip), "name": os.path.basename(clip), "start_sec": w["start"], "end_sec": w["end"], "event": w["event"]}
        entry.update(info)
        manifest["files"].append(entry)
        ok &= info["ok"]
    manifest["ok"] = ok
    with open(man_path, "w", encoding="utf-8") as f:
        json.dump(manifest, f, indent=2)
    if not a.keep:
        for s in segs + [realtime, concat]:
            try:
                os.remove(s)
            except OSError:
                pass
    update_status(a.status, manifest=os.path.abspath(man_path), files=[f["file"] for f in manifest["files"]], video_sec=manifest["video_sec"],
                  message="%d mp4 files, %s" % (len(manifest["files"]), "all checked" if ok else "SOME FAILED the check"))
    print(json.dumps({"manifest": os.path.abspath(man_path), "files": [(f["name"], f["duration_sec"], f["size_bytes"], f["yavg"], f["ok"]) for f in manifest["files"]]}, indent=1))
    return 0 if ok else 2


def self_test():
    lines = [
        "x LogCodexTactics: Display: [Bot] Route: fight at 1.0 s on the way to #1 (a), formation column",  # before any frame: ignored
        "x LogCodexTactics: Display: ClipRecorder lg f=0 t=0.03",
        "x LogCodexTactics: Display: ClipRecorder other f=5 t=0.03",
        "x LogCodexTactics: Display: ClipRecorder lg f=300 t=10.00",
        "x LogCodexTactics: Display: [Bot] Route: fight at 40.0 s on the way to #3 (b), formation wedge",
        "x LogCodexTactics: Display: [Bot] Route: COVER - the squad to the wall at (1, 2)",
        "x LogCodexTactics: Display: ClipRecorder lg f=450 t=15.00",
        "x LogCodexTactics: Display: [Bot] Route: fight at 45.0 s on the way to #3 (b), formation wedge",
        "x LogCodexTactics: Display: ClipRecorder lg f=3000 t=100.00",
        "x LogCodexTactics: Display: [Bot] Route: the leader drops through hole_roof_0 at 90.5 s (feet z 567)",
        "x LogCodexTactics: Display: [Bot] Route: fight at 95.0 s on the way to #9 (c), formation column",
    ]
    frames, events = parse_log(lines, "lg")
    assert [f for f, _ in frames] == [0, 300, 450, 3000], frames
    assert [(e["kind"], e["frame"]) for e in events] == [("fight", 300), ("cover", 300), ("fight", 450), ("drop", 3000), ("fight", 3000)], events
    assert events[0]["game_sec"] == 40.0 and events[3]["game_sec"] == 90.5
    w = pick_windows(events, 30, 200.0, 3)
    assert [(x["start"], x["end"]) for x in w] == [(7.0, 26.0), (97.0, 116.0)], w   # the 2nd fight overlaps the 1st window: skipped
    assert all(x["end"] - x["start"] <= 20.0 for x in w)
    assert contiguous_from([3, 4, 5, 7], 3) == 3 and contiguous_from([], 0) == 0
    # The frame glob takes the recorder's JPEG frames and the PNG ones (format=png); other files are ignored.
    import tempfile
    with tempfile.TemporaryDirectory() as d:
        for name in ("f_00000.jpg", "f_00001.jpg", "f_00002.png", "f_3.jpg", "g_00004.jpg", "f_00005.jpeg"):
            open(os.path.join(d, name), "w").close()
        assert frame_numbers(d) == [0, 1, 2, 3], frame_numbers(d)
        assert frame_ext(d, 0) == "jpg" and frame_ext(d, 2) == "png" and frame_ext(d, 9) == "png"
    assert h264_args(18, "veryfast", "libx264") == ["-c:v", "libx264", "-preset", "veryfast", "-crf", "18"]
    assert h264_args(28, "medium", "h264_nvenc") == ["-c:v", "h264_nvenc", "-preset", "p5", "-rc", "vbr", "-cq", "28", "-b:v", "0"]
    print("clip_assemble self-test: ok")
    return 0


def main(argv):
    import argparse
    if len(argv) > 1 and argv[1] == "--test":
        return self_test()
    p = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    sub = p.add_subparsers(dest="cmd", required=True)
    w = sub.add_parser("watch")
    w.add_argument("--frames", required=True)
    w.add_argument("--segments", required=True)
    w.add_argument("--pid", type=int, required=True)
    w.add_argument("--fps", type=int, default=30)
    w.add_argument("--seg-frames", type=int, default=900)
    w.add_argument("--status", default="")
    s = sub.add_parser("assemble")
    s.add_argument("--segments", required=True)
    s.add_argument("--log", default="")
    s.add_argument("--name", required=True)
    s.add_argument("--out", required=True)
    s.add_argument("--fps", type=int, default=30)
    s.add_argument("--speedup", type=float, default=4.0)
    s.add_argument("--highlights", type=int, default=3)
    s.add_argument("--report", default="")
    s.add_argument("--status", default="")
    s.add_argument("--keep", action="store_true")
    a = p.parse_args(argv[1:])
    return cmd_watch(a) if a.cmd == "watch" else cmd_assemble(a)


if __name__ == "__main__":
    sys.exit(main(sys.argv))
