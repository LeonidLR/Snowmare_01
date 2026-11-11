# 024 - Make the local model smarter without training (designer 2026-11-11) - after 023, branch `gemini/024-local-context` from main

Python / Ollama config only. Model: qwen3-coder:30b (the only local model). Report to claude.

1. **Modelfile `codex-local`** (Scripts/Tools/local_model/Modelfile, FROM qwen3-coder:30b, num_ctx 16384, temperature 0.2) with a
   compact system prompt: UE 5.8 C++ + Epic coding standard (prefixes, TObjectPtr, no `const` mutation, IWYU), our module / folder
   names (CodexTactics / CodexTacticsTests, Public / Private by system), test naming CodexTactics.<System>.<Feature>.<Case>, data in
   Content/Data/**.json, "answer with the code / command only unless asked", "say UNSURE instead of guessing". A setup script
   `ollama create codex-local -f ...` (idempotent). local_task.py default -> codex-local (fallback qwen3-coder:30b if absent).
2. **Auto-context in local_task.py:** `--auto-context` finds the relevant project files for the prompt (keywords / identifiers from
   the prompt -> `rg` over Source/, Content/Data, docs/, Scripts/; rank by hits; take the best snippets with +-30 lines around hits;
   cap ~10k tokens) and adds them like --files. Also `--neighbour <file>` = add the closest sibling file (e.g. a test next to the
   target) as a style example. Unit tests for the keyword / ranking logic on a fixture tree.
3. **Examples store:** Saved/Orchestrator/local_examples/*.json (task kind, prompt, good answer, who verified). Seed it from the
   CORRECT answers of your 021 grades + Claude's corrections (the PowerShell needs -Recurse -File; the C++ test must use the real
   scan flag via FormationShapes / FFormationConfig, not a const mutation). `--examples` adds the 2 most similar examples.
4. Re-run the 7 trial prompts (Saved/Orchestrator/local_trial/run_trial.sh) with codex-local + --auto-context + --examples into
   local_trial/ctx-*.md and grade them like 021 item 4: before / after table. Report docs/port/gemini/outbox/024-local-context.report.md.
