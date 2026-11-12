# 029 - Start the audio pipeline early: the generation tool (docs/design/AUDIO_PIPELINE.md section 1) - branch from main

Python only. Report to claude. Read docs/design/AUDIO_PIPELINE.md first.
1. Find the CURRENT Google model ids, endpoints and prices for (a) Gemini TTS and (b) Lyria music in Google's own docs (web), and
   whether each works with the plain Gemini API key (GEMINI_API_KEY) or needs Vertex AI. Write it in the report with links.
2. `Scripts/Tools/audio_gen.py` + unit tests: `voice` and `music` subcommands from manifests (Content/Data/Audio/voice_lines.json,
   music_cues.json - create small v1 manifests: 5 barks for Commander / Engineer / Medic-Sapper from the existing HUD bark texts
   (grep the bark tables in Source/ - read-only), 3 music cues), WAV out to Saved/AudioGen/, spend ledger Saved/AudioGen/spend.jsonl
   with `--budget-usd` hard cap (default 40 / week), `--dry-run` (prints the request plan + estimated cost, sends nothing). Key loaded
   from the user environment into the process only, never printed or logged.
3. ONE real test only if the key works: 3 TTS lines (one per our character) + 1 short music cue (if Lyria works with the key),
   total cost < 1 $; files in Saved/AudioGen/; say the exact spend. Then STOP - the designer listens first.
4. A Training Ground "Audio" panel to play / approve / reject the generated files (writes an approvals JSON next to the manifest)
   - TS, same security rules as the props / commit APIs (fixed folder, no client paths, same-origin).
