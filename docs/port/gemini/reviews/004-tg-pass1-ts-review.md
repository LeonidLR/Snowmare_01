Three defects found; everything else in the diff looks consistent with the spec.

1. **`Tools/TrainingGround/src/utils/levelSettings.ts`, `setOperativeKit`: unknown per-operative kit keys are dropped on save.**
   - Offending code: `const cleaned: OperativeKit = {}; if (typeof kit.grenades === 'number') … cleaned.heat_packs …` then `fullKit[cleanKey] = cleaned;`
   - Any other field in an existing `kit.<key>` entry is lost the first time that operative is edited in the panel. The tab buttons and inputs spread `currentOpKit`, but `cleaned` only rebuilds the four known fields.
   - Fix: start from the existing entry, `const cleaned = { ...kit }`, and only overwrite or delete the four known fields and `ammo`.

2. **`Tools/TrainingGround/src/utils/levelSettings.ts` and `LevelOperativesPanel.tsx`: writes a top-level `kit_override_carry` key the game does not read.**
   - Offending code: `next.kit_override_carry = true;` in `setKitOverrideCarry`, plus the checkbox in the panel.
   - The authoritative key list has only `environment.override_carry`. The checkbox does nothing in the game and gives a false sense of effect.
   - Fix: remove `kit_override_carry` (the setter, getter, checkbox, type and test), or map the checkbox to `environment.override_carry`.

3. **`Tools/TrainingGround/vite.config.ts`, run endpoint: command injection through `cmd.exe` arguments.**
   - Offending code: `args.push(String(payload.runs)); args.push(String(payload.profile || 'NORMAL')); args.push(String(payload.speed || '5.0')); args.push(String(payload.loadout || 'COLLECT'))` followed by `spawn('cmd.exe', args, …)`.
   - These values come straight from the HTTP body. A value like `X&calc&` is interpreted by cmd, and `profile` and `loadout` are then expanded unquoted in the `.bat` PowerShell line. `mapPath` and `levelJsonPath` are also passed unvalidated.
   - Fix: whitelist the values (`/^\d+$/` for runs, `/^[A-Z_]+$/` for profile and loadout, `/^[\d.]+$/` for speed). Alternatively, spawn the `.bat` or PowerShell directly with `shell:false`, and reject any path or argument containing `"&|<>^%!`.