"""Binds ABP_Operative's Aim Offset nodes to the corner-look aim offset (G025).

Does not rebuild the AnimGraph. UOperativeAnimGraphLibrary.bind_cover_look_aim_offset shows an unlinked Blend Space pin and
binds it to ActiveAimOffset, and binds unlinked X / Y to AimYaw / AimPitch. A pin that already has a link is left as the
designer set it (main 74f1d128 stays). Alpha is not touched. Idempotent. Saves only when a pin was bound and the compile
succeeded.

Run with this checkout's editor closed:
  UnrealEditor-Cmd.exe CodexTactics.uproject -run=pythonscript -script="<abs path>" -unattended -nullrhi
Result: Saved/Logs/bind_cover_look_aim_offset.txt
"""
import os
import unreal

library = unreal.EditorAssetLibrary
ABP_PATH = "/Game/Characters/Operatives/ABP_Operative"
log = []

try:
    abp = library.load_asset(ABP_PATH)
    if not abp:
        log.append(f"MISSING {ABP_PATH}")
    else:
        graph = unreal.OperativeAnimGraphLibrary
        log.append("--- AnimGraph before ---")
        log.append(graph.describe_anim_graph(abp))
        result = graph.bind_cover_look_aim_offset(abp)
        if isinstance(result, tuple) and len(result) >= 2 and isinstance(result[0], str):
            report, ok = str(result[0]), bool(result[1])
        elif isinstance(result, tuple) and len(result) >= 2:
            ok, report = bool(result[0]), str(result[1])
        else:
            ok, report = bool(result), str(result)
        log.append("--- bind_cover_look_aim_offset ---")
        log.append(f"ok {ok}")
        log.append(report)
        log.append("--- AnimGraph after ---")
        log.append(graph.describe_anim_graph(abp))
        if ok and "bound " in report:
            saved = library.save_loaded_asset(abp, False)
            log.append(f"saved {ABP_PATH}: {saved}")
        else:
            log.append("NOT saved")
except Exception as error:
    log.append(f"EXCEPTION {error}")

with open(os.path.join(unreal.Paths.project_saved_dir(), "Logs", "bind_cover_look_aim_offset.txt"), "w", encoding="utf-8") as handle:
    handle.write("\n".join(log) + "\n")
