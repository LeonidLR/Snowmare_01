"""Adds the `LeftHandGrip` socket to the operatives' rifle mesh for the left-hand IK (user-approved plan 2026-10-07).

The M4 cover pack's clips hold their own M4 differently in the right hand: on our rifle (rigid on hand_r, its offset
tuned for the RifleAnims locomotion) the left hand ended 38-42 cm off the handguard (7 cm with RifleAnims). The socket
marks where the left hand holds OUR handguard; UOperativeAnimInstance reads it (LeftHandIKOffset, in hand_r space) and
the ABP's Two Bone IK on hand_l (added by the user) pulls the hand there.

Transform (weapon-mesh space): the left hand of AS_Rifle_Aim (RifleAnims; the clip the weapon offset was tuned for),
measured with the operative mesh and BP_Operative's WeaponMesh relative transform (Saved/Logs/measure_left_grip_socket.txt):
location (-5.8, 4.4, 54.5) — 39 % of the way from the grip (z 27.9) to the muzzle (z 98) — rotation pitch 58.7,
yaw -85.2, roll -170.8 (the hand bone's orientation there).

2026-10-07: the user moved the socket onto the handguard in the static mesh editor (loc (2.45, 0.04, 73.3), no
rotation) — that is the reference now. This script never overwrites an existing socket unless CODEX_FORCE_LEFT_HAND_SOCKET=1.

Run with the CodexTactics editor closed:
  UnrealEditor-Cmd.exe CodexTactics.uproject -run=pythonscript -script="<abs path to this file>" -unattended -nullrhi
An existing LeftHandGrip socket is kept (the user's placement). Result: Saved/Logs/add_weapon_left_hand_socket.txt
"""
import os
import unreal

MESH = "/Game/Weapons/M16/m16_01/StaticMeshes/m16_01"
SOCKET = "LeftHandGrip"
LOCATION = unreal.Vector(-5.8, 4.4, 54.5)
ROTATION = unreal.Rotator(roll=-170.8, pitch=58.7, yaw=-85.2)

log = []
mesh = unreal.load_asset(MESH)
if not mesh:
    log.append("weapon mesh not found: " + MESH)
else:
    socket = mesh.find_socket(SOCKET)
    created = socket is None
    if not created and os.environ.get("CODEX_FORCE_LEFT_HAND_SOCKET") != "1":
        log.append("%s already on %s at %s: kept (CODEX_FORCE_LEFT_HAND_SOCKET=1 overwrites)" % (SOCKET, MESH, socket.get_editor_property("relative_location")))
        with open(os.path.join(unreal.Paths.project_saved_dir(), "Logs", "add_weapon_left_hand_socket.txt"), "w", encoding="utf-8") as f:
            f.write("\n".join(log))
        raise SystemExit(0)
    if created:
        socket = unreal.StaticMeshSocket(mesh)
        socket.set_editor_property("socket_name", SOCKET)
    socket.set_editor_property("relative_location", LOCATION)
    socket.set_editor_property("relative_rotation", ROTATION)
    socket.set_editor_property("relative_scale", unreal.Vector(1, 1, 1))
    if created:
        try:
            mesh.add_socket(socket)  # UStaticMesh::AddSocket (editor)
        except Exception as error:
            log.append("could not add the socket: %s" % error)
    mesh.modify()
    saved = unreal.EditorAssetLibrary.save_loaded_asset(mesh, only_if_is_dirty=False)
    check = mesh.find_socket(SOCKET)
    log.append("%s %s on %s: %s, saved %s" % ("created" if created else "updated", SOCKET, MESH,
                                              "found %s" % check.get_editor_property("relative_location") if check else "NOT FOUND", saved))

with open(os.path.join(unreal.Paths.project_saved_dir(), "Logs", "add_weapon_left_hand_socket.txt"), "w", encoding="utf-8") as f:
    f.write("\n".join(log))
