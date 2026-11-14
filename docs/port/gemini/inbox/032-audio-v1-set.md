# 032 - Audio v1 set (Claude, 2026-11-12) - after 031; branch gemini/032-audio-v1 from your 029 branch; report to claude

1. **Voices: all male** (designer feedback): Commander, Engineer, Medic-Sapper = three distinct MALE voices (fixed per character,
   style prompts: Commander calm / firm, Engineer dry / technical, Medic tired / warm). Regenerate the female line(s) first and send
   one line per character to the chat as a file path for the designer.
2. **Full v1 batch** within a hard 5 $ cap (ledger): all v1 barks of our three (the 15 from your manifest + every other bark text the
   HUD shows for our squad - list the source file:line) and 2-3 PMC operator voices for their barks ("Behind cover!", "Hugging the
   wall..."), + 8 music cues from AUDIO_PIPELINE section 2 (2 variants each, 30-60 s, loopable where it is a state loop).
   Output into Saved/AudioGen/ (your worktree) AND copy the WAVs to MAIN Saved/Orchestrator/audio_v1/ for the designer.
3. **Implementation map for Codex (read-only):** UCodexAudioSubsystem per AUDIO_PIPELINE section 5 - the existing bark / HUD message
   events (file:line), where the music state comes from (sight / pressure / stealth / horde), Sound Classes / Submix setup, the
   importer step (editor Python into /Game/Audio, editor closed), smokes. docs/port/codex/inbox/009-audio-subsystem.md (draft).
