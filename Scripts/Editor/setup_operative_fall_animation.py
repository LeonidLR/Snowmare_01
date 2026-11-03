"""Gives the operatives' AnimBP a fall state (user decision 2026-11-03; docs/systems/stances-and-movement.md "Falls and drops"):

  - ABP_Operative's AnimGraph is NOT rebuilt (the user's polish stays): UOperativeAnimGraphLibrary.add_fall_state_machine caches the pose
    that feeds the "FullBody" slot as "PreFallPose" and puts a state machine "FallStates" in its place - Grounded (that pose), Falling
    (MM_Rifle_Jump_Fall_Loop, looping), Landing (MM_Rifle_Jump_Fall_Land, once) - driven by UOperativeAnimInstance bFallAirborne /
    bFallLanding / bFallGrounded (ULedgeFallComponent::GetAnimPhase). A knockdown / death montage on the FullBody slot still wins.
  - Idempotent: an AnimGraph that already has "FallStates" is left alone.
  - The C++ slot clips of ULedgeFallComponent are then off (Codex.Fall.SlotClips 2 = only for an ABP without "FallStates").

Back the asset up first (Saved/Backups/...). Run with the project's editor closed:
  UnrealEditor-Cmd.exe CodexTactics.uproject -run=pythonscript -script="<abs path to this file>" -unattended -nullrhi
Result: Saved/Logs/setup_operative_fall_animation.txt (the graph before / after, the compile report, saved or not).
"""
import os
import unreal

library = unreal.EditorAssetLibrary
ABP_PATH = "/Game/Characters/Operatives/ABP_Operative"
JUMP = "/Game/Characters/Mannequins/Anims/Rifle/Jump"
FALL_LOOP = f"{JUMP}/MM_Rifle_Jump_Fall_Loop"
LAND = f"{JUMP}/MM_Rifle_Jump_Fall_Land"
log = []

try:
    abp = library.load_asset(ABP_PATH)
    loop = library.load_asset(FALL_LOOP)
    land = library.load_asset(LAND)
    if not abp or not loop or not land:
        log.append(f"MISSING asset: abp {abp is not None}, loop {loop is not None}, land {land is not None}")
    else:
        graph = unreal.OperativeAnimGraphLibrary
        log.append("--- AnimGraph before ---")
        log.append(graph.describe_anim_graph(abp))
        result = graph.add_fall_state_machine(abp, "FullBody", loop, land, 0.15, 0.1, 0.25)
        ok, report = (result[0], result[1]) if isinstance(result, tuple) else (result, "")
        log.append("--- add_fall_state_machine ---")
        log.append(f"ok {ok}")
        log.append(report)
        log.append("--- AnimGraph after ---")
        log.append(graph.describe_anim_graph(abp))
        if ok:
            saved = library.save_loaded_asset(abp, False)
            log.append(f"saved {ABP_PATH}: {saved}")
        else:
            log.append("NOT saved")
except Exception as error:  # the result file must say what went wrong
    log.append(f"EXCEPTION {error}")

with open(os.path.join(unreal.Paths.project_saved_dir(), "Logs", "setup_operative_fall_animation.txt"), "w", encoding="utf-8") as f:
    f.write("\n".join(log) + "\n")
