"""Creates the placeholder frontend assets (frontend phase 1, user decision 2026-10-08). The user restyles all of them.

  /Game/UI/Frontend/Input/
      DT_CodexUIActions                 CommonUI action rows: Confirm (Enter / gamepad A), Back (Esc / gamepad B)
      BP_CodexUIInputData               CommonUIInputData (default click / back actions) -> Config/DefaultGame.ini
      BP_ControllerData_KeyboardMouse   CommonInputBaseControllerData (key icons: the user fills InputBrushDataMap)
      BP_ControllerData_Gamepad         same for the generic gamepad
  /Game/UI/Frontend/Styles/
      BP_FrontendButtonStyle            CommonButtonStyle (plain colour brushes) + text styles BP_FrontendText_*
  /Game/UI/Frontend/
      WBP_MenuButton, WBP_SaveSlotEntry, WBP_PrimaryLayout, WBP_TitleScreen, WBP_MainMenu, WBP_SaveSlots, WBP_Options,
      WBP_Credits, WBP_PauseMenu, WBP_ConfirmDialog   (Widget Blueprints of the C++ classes, placeholder trees written by
      UFrontendWidgetGenerator; existing trees are kept, CODEX_FRONTEND_REBUILD=1 rewrites them)
  /Game/Maps/L_MainMenu                 menu map: floor, sun / sky, the operative idling (ACodexMenuCharacterActor) and
                                        one ACodexMenuCameraAnchor per menu entry; World Settings game mode =
                                        ACodexFrontendGameMode. Never overwritten (delete it by hand for a fresh one).

Existing assets are kept (only missing ones are created). Result: Saved/Logs/create_frontend_assets.txt.
Run headless (editor closed, after build.ps1):
  UnrealEditor-Cmd.exe CodexTactics.uproject -run=pythonscript -script="<abs path to this file>" -unattended -nullrhi
"""
import json
import os

import unreal

ROOT = "/Game/UI/Frontend"
INPUT = f"{ROOT}/Input"
STYLES = f"{ROOT}/Styles"
MAP_PATH = "/Game/Maps/L_MainMenu"
REBUILD = os.environ.get("CODEX_FRONTEND_REBUILD", "0") == "1"

library = unreal.EditorAssetLibrary
tools = unreal.AssetToolsHelpers.get_asset_tools()
log = []


def note(text):
    log.append(text)
    unreal.log(text)


def blueprint(path, name, parent):
    """Blueprint subclass of a native class (created once)."""
    full = f"{path}/{name}"
    if library.does_asset_exist(full):
        return library.load_asset(full), False
    factory = unreal.BlueprintFactory()
    factory.set_editor_property("parent_class", parent)
    asset = tools.create_asset(name, path, unreal.Blueprint, factory)
    note(f"created {full}")
    return asset, True


def defaults(bp):
    return unreal.get_default_object(bp.generated_class())


def save(asset):
    library.save_loaded_asset(asset, only_if_is_dirty=False)


def color(r, g, b, a=1.0):
    return unreal.LinearColor(r, g, b, a)


def brush(tint):
    b = unreal.SlateBrush()
    b.set_editor_property("draw_as", unreal.SlateBrushDrawType.BOX)
    b.set_editor_property("tint_color", unreal.SlateColor(tint))
    return b


# ---------------------------------------------------------------------------------------------------------- input data
table_path = f"{INPUT}/DT_CodexUIActions"
if library.does_asset_exist(table_path):
    table = library.load_asset(table_path)
else:
    factory = unreal.DataTableFactory()
    factory.set_editor_property("struct", unreal.CommonInputActionDataBase.static_struct())
    table = tools.create_asset("DT_CodexUIActions", INPUT, unreal.DataTable, factory)
    rows = [
        {"Name": "Confirm", "DisplayName": "Confirm",
         "KeyboardInputTypeInfo": {"Key": "Enter"},
         "DefaultGamepadInputTypeInfo": {"Key": "Gamepad_FaceButton_Bottom"},
         "TouchInputTypeInfo": {"Key": "Touch1"}},
        {"Name": "Back", "DisplayName": "Back",
         "KeyboardInputTypeInfo": {"Key": "Escape"},
         "DefaultGamepadInputTypeInfo": {"Key": "Gamepad_FaceButton_Right"},
         "TouchInputTypeInfo": {"Key": "Android_Back"}},
    ]
    ok = unreal.DataTableFunctionLibrary.fill_data_table_from_json_string(table, json.dumps(rows))
    note(f"created {table_path} (rows filled: {ok})")
    save(table)

input_data, created = blueprint(INPUT, "BP_CodexUIInputData", unreal.CommonUIInputData)
if created:
    cdo = defaults(input_data)
    click = unreal.DataTableRowHandle()
    click.set_editor_property("data_table", table)
    click.set_editor_property("row_name", "Confirm")
    back = unreal.DataTableRowHandle()
    back.set_editor_property("data_table", table)
    back.set_editor_property("row_name", "Back")
    cdo.set_editor_property("default_click_action", click)
    cdo.set_editor_property("default_back_action", back)
    unreal.BlueprintEditorLibrary.compile_blueprint(input_data)
    save(input_data)

for name, input_type in (("BP_ControllerData_KeyboardMouse", unreal.CommonInputType.MOUSE_AND_KEYBOARD),
                         ("BP_ControllerData_Gamepad", unreal.CommonInputType.GAMEPAD)):
    data, created = blueprint(INPUT, name, unreal.CommonInputBaseControllerData)
    if created:
        cdo = defaults(data)
        cdo.set_editor_property("input_type", input_type)
        if input_type == unreal.CommonInputType.GAMEPAD:
            cdo.set_editor_property("gamepad_name", "Generic")
        unreal.BlueprintEditorLibrary.compile_blueprint(data)
        save(data)

# -------------------------------------------------------------------------------------------------------------- styles
roboto = unreal.load_asset("/Engine/EngineFonts/Roboto.Roboto")


def text_style(name, size, tint, typeface="Bold"):
    style, created = blueprint(STYLES, name, unreal.CommonTextStyle)
    if created:
        cdo = defaults(style)
        font = unreal.SlateFontInfo()
        font.set_editor_property("font_object", roboto)
        font.set_editor_property("typeface_font_name", typeface)
        font.set_editor_property("size", size)
        cdo.set_editor_property("font", font)
        cdo.set_editor_property("color", tint)
        unreal.BlueprintEditorLibrary.compile_blueprint(style)
        save(style)
    return style


text_normal = text_style("BP_FrontendText_Normal", 20, color(0.78, 0.86, 0.95))
text_hovered = text_style("BP_FrontendText_Hovered", 20, color(1.0, 1.0, 1.0))
text_disabled = text_style("BP_FrontendText_Disabled", 20, color(0.35, 0.38, 0.42))

button_style, created = blueprint(STYLES, "BP_FrontendButtonStyle", unreal.CommonButtonStyle)
if created:
    cdo = defaults(button_style)
    cdo.set_editor_property("normal_base", brush(color(0.0, 0.0, 0.0, 0.0)))
    cdo.set_editor_property("normal_hovered", brush(color(0.35, 0.55, 0.75, 0.35)))
    cdo.set_editor_property("normal_pressed", brush(color(0.35, 0.55, 0.75, 0.6)))
    cdo.set_editor_property("selected_base", brush(color(0.35, 0.55, 0.75, 0.25)))
    cdo.set_editor_property("selected_hovered", brush(color(0.35, 0.55, 0.75, 0.4)))
    cdo.set_editor_property("selected_pressed", brush(color(0.35, 0.55, 0.75, 0.6)))
    cdo.set_editor_property("disabled", brush(color(0.0, 0.0, 0.0, 0.0)))
    cdo.set_editor_property("normal_text_style", text_normal.generated_class())
    cdo.set_editor_property("normal_hovered_text_style", text_hovered.generated_class())
    cdo.set_editor_property("selected_text_style", text_hovered.generated_class())
    cdo.set_editor_property("selected_hovered_text_style", text_hovered.generated_class())
    cdo.set_editor_property("disabled_text_style", text_disabled.generated_class())
    unreal.BlueprintEditorLibrary.compile_blueprint(button_style)
    save(button_style)

# ----------------------------------------------------------------------------------------------------- widget blueprints
generator = unreal.FrontendWidgetGenerator


def widget(name, parent, button_class=None, slot_class=None):
    result = generator.build_frontend_widget(ROOT, name, parent, button_class, slot_class, REBUILD)
    wbp, report = result if isinstance(result, tuple) else (result, "")
    note(f"{ROOT}/{name}: {report}")
    if wbp:
        save(wbp)
    return wbp


def set_button_style(wbp):
    if wbp:
        defaults(wbp).set_editor_property("style", button_style.generated_class())
        unreal.BlueprintEditorLibrary.compile_blueprint(wbp)
        save(wbp)


menu_button = widget("WBP_MenuButton", unreal.CodexMenuButton)
set_button_style(menu_button)
slot_entry = widget("WBP_SaveSlotEntry", unreal.CodexSaveSlotEntry)
set_button_style(slot_entry)
button_class = menu_button.generated_class() if menu_button else None
slot_class = slot_entry.generated_class() if slot_entry else None
widget("WBP_PrimaryLayout", unreal.CodexPrimaryLayout)
for name, parent in (("WBP_TitleScreen", unreal.CodexTitleScreen), ("WBP_MainMenu", unreal.CodexMainMenuScreen),
                     ("WBP_SaveSlots", unreal.CodexSaveSlotsScreen), ("WBP_Options", unreal.CodexOptionsScreen),
                     ("WBP_Credits", unreal.CodexCreditsScreen), ("WBP_PauseMenu", unreal.CodexPauseMenuScreen),
                     ("WBP_ConfirmDialog", unreal.CodexConfirmDialog)):
    widget(name, parent, button_class, slot_class)

# ------------------------------------------------------------------------------------------------------------ menu map
if library.does_asset_exist(MAP_PATH):
    note(f"{MAP_PATH} exists - kept")
else:
    actors = unreal.get_editor_subsystem(unreal.EditorActorSubsystem)
    unreal.EditorLoadingAndSavingUtils.new_blank_map(False)
    cube = unreal.load_asset("/Engine/BasicShapes/Cube.Cube")

    def spawn(cls, location, rotation=unreal.Rotator(0, 0, 0), label=None, folder=None):
        actor = actors.spawn_actor_from_class(cls, location, rotation)
        if label:
            actor.set_actor_label(label)
        if folder:
            actor.set_folder_path(folder)
        return actor

    floor = spawn(unreal.StaticMeshActor, unreal.Vector(0, 0, -10), label="Floor", folder="Scene")
    floor.static_mesh_component.set_static_mesh(cube)
    floor.set_actor_scale3d(unreal.Vector(40, 40, 0.2))
    sun = spawn(unreal.DirectionalLight, unreal.Vector(0, 0, 800), unreal.Rotator(roll=0, pitch=-35, yaw=-130), "Sun", "Scene")
    sun.light_component.set_editor_property("atmosphere_sun_light", True)
    spawn(unreal.SkyAtmosphere, unreal.Vector(0, 0, 0), label="SkyAtmosphere", folder="Scene")
    sky = spawn(unreal.SkyLight, unreal.Vector(0, 0, 300), label="SkyLight", folder="Scene")
    sky.light_component.set_editor_property("real_time_capture", True)
    spawn(unreal.ExponentialHeightFog, unreal.Vector(0, 0, 0), label="Fog", folder="Scene")

    # The operative idling in front of the cameras (mesh + Anim Blueprint taken from BP_Operative; the user swaps them).
    character = spawn(unreal.CodexMenuCharacterActor, unreal.Vector(0, 0, 0), unreal.Rotator(0, 0, 0), "MenuCharacter", "Scene")
    operative = unreal.load_class(None, "/Game/Characters/Operatives/BP_Operative.BP_Operative_C")
    if operative:
        template = unreal.get_default_object(operative).get_editor_property("mesh")
        mesh = template.get_editor_property("skeletal_mesh_asset") if template else None
        if mesh:
            character.mesh.set_skeletal_mesh_asset(mesh)
            # The character's capsule centre is ~90 cm above its feet; here the mesh is the root at floor height.
            offset = template.get_editor_property("relative_location")
            character.set_actor_location(unreal.Vector(0, 0, offset.z + 90), False, False)
            character.set_actor_rotation(template.get_editor_property("relative_rotation"), False)
            anim_class = template.get_editor_property("anim_class")
            if anim_class:
                character.mesh.set_animation_mode(unreal.AnimationMode.ANIMATION_BLUEPRINT)
                character.mesh.set_anim_instance_class(anim_class)
            note(f"menu character: {mesh.get_path_name()} / {anim_class.get_name() if anim_class else 'no anim class'}")
    target = unreal.Vector(0, 0, 120)

    # One camera anchor per menu entry (AnchorId = the button's EntryId); Title frames the whole scene.
    anchors = {
        "Title": unreal.Vector(520, -260, 200),
        "Continue": unreal.Vector(260, 120, 160),
        "NewGame": unreal.Vector(300, -60, 150),
        "LoadGame": unreal.Vector(220, 200, 190),
        "Options": unreal.Vector(160, -220, 140),
        "Credits": unreal.Vector(420, 260, 260),
        "Quit": unreal.Vector(700, 0, 320),
    }
    for anchor_id, location in anchors.items():
        rotation = unreal.MathLibrary.find_look_at_rotation(location, target)
        anchor = spawn(unreal.CodexMenuCameraAnchor, location, rotation, f"Anchor_{anchor_id}", "CameraAnchors")
        anchor.set_editor_property("anchor_id", anchor_id)
        anchor.camera_component.set_editor_property("field_of_view", 60.0 if anchor_id == "Title" else 45.0)
    world = unreal.get_editor_subsystem(unreal.UnrealEditorSubsystem).get_editor_world()
    world.get_world_settings().set_editor_property("default_game_mode", unreal.CodexFrontendGameMode)
    unreal.EditorLoadingAndSavingUtils.save_map(world, MAP_PATH)
    note(f"created {MAP_PATH} ({len(anchors)} camera anchors)")

out = os.path.join(unreal.Paths.project_saved_dir(), "Logs", "create_frontend_assets.txt")
with open(out, "w", encoding="utf-8") as f:
    f.write("\n".join(log) + "\n")
