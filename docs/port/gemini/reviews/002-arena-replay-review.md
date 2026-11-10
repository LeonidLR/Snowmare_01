The pasted file has no line numbers, so I cite the function and the offending expression instead. I found no external network requests, and none of the other categories turned up anything beyond the seven below.

1. **XSS in the roster and event log (`updateRosterUI`, `buildEventLog`).**
   - Fighter id, stance, order, and kill/tactic text come straight from the track JSON. They are interpolated into `card.innerHTML` (`${f.id}`, `${stance}`, `${order}`, `tag-${side.toLowerCase()}`) and `row.innerHTML` (`${ev.text}`, built from `k.victim`, `k.by`, `tc.who`, `tc.order`).
   - Fix: build these nodes with `createElement` and `textContent`, or pass every interpolated value through an `esc()` helper.

2. **Incomplete `</script>` escaping (`generate_replay_html`).**
   - `json_str.replace("</script>", ...)` is case-sensitive, so `</SCRIPT>` or `<!--` inside a name ends or confuses the data block.
   - Fix: use `json_str.replace("<", "\\u003c")`.

3. **Wrong scale for `units: "m"` tracks (`parse_round_track` return dict).**
   - Bounds, objective, and shot positions are multiplied by `scale_to_cm`, but `"frames": frames` is emitted raw. The JS treats frames as cm, so fighters sit 100× too close to the origin, away from the objective and shots.
   - Layout geometry is also assumed to be cm and never converted.
   - Fix: emit `frames = [[[p[0]*scale_to_cm, p[1]*scale_to_cm] if p else None for p in f] for f in frames]`.

4. **Crashes on null frames or states (`parse_round_track`).**
   - `for p in f` in the bbox loop fails when a frame is `null`.
   - In the shot handler, `cur_frame` and `cur_states` can be `None`. That breaks `len(cur_states)` and `len(cur_frame)`, and `st[2] > 0` breaks when `st[2]` is null.
   - Fix: write `for p in (f or [])`, and use `cur_frame = (frames[i] or [])` and `cur_states = (states[i] or [])`.

5. **Crashes on explicit nulls (`parse_round_track`).**
   - `float(e.get("t", 0.0))`, `float(e.get("dist_m", 0.0))`, and `float(raw_obj.get("x", 0.0))` (also `y` and `radius`) raise `TypeError` when the key is present with value `null`. The `.get` default only applies to missing keys.
   - Fix: use `float(e.get("t") or 0.0)` and the same pattern for the other fields.

6. **File written outside `--out` (`main`, `out_file`).**
   - `run_id = data.get("run")` comes from the track file and goes unsanitised into `os.path.join(default_out_dir, f"{run_id}_replay.html")`. A value like `../../x` writes outside `--out`.
   - Fix: apply `run_id = re.sub(r"[^\w.-]", "_", str(run_id))` before building the path.

7. **Per-frame DOM rebuild (`updateUI` → `updateRosterUI`).**
   - Every `render()` call runs `container.innerHTML = ''` and rebuilds all roster cards, with `render()` firing on every animation frame, scrub move, and drag move. That is 6 cards re-parsed up to 60 times a second. Clicks on a card can also be lost when the node is replaced between mousedown and mouseup.
   - Fix: build the cards once per round and only update hp, width, and text, or skip the rebuild when the `stateRow` values are unchanged.