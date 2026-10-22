# Migrates the reusable sky / snow assets of the old project WeatherSystem_01 into /Game/Environment/Weather/
# (docs/systems/environment-weather.md section 10). Rain, lightning, examples, footprints, landscape automaterial,
# Houdini / Gaea / maps and "(1)" duplicates are NOT migrated. The old project is READ-ONLY: this script only copies from it.
#
# Two stages, one file (re-runnable: a finished migration is detected and skipped):
#
#   1. STAGE  (plain Python, no editor):  python Scripts/Editor/migrate_weather_assets.py stage
#      Copies the .uasset files of the manifest below into <project>/Content under their ORIGINAL relative paths
#      (temporary), after checking that no target exists. Aborts on any collision. The old project path can be overridden
#      with the environment variable CODEX_WEATHER_SRC (its Content folder's parent).
#
#   2. MOVE   (editor commandlet, take the build lock first: Scripts/agent_lock.ps1, CODEX_AGENT=claude):
#      UnrealEditor-Cmd.exe <uproject> -run=pythonscript -script=<abs path to this file> -unattended -nullrhi
#      Renames the staged assets to their new /Game/Environment/Weather/ paths WITH reference fixup (limited to the manifest),
#      fixes up the redirectors of the temporary folders only, deletes those folders, saves only the moved assets, and verifies
#      that every asset loads and that nothing references a package outside /Game/Environment/Weather (except engine content).
#      Result: Saved/Logs/migrate_weather_assets.txt
#
# The manifest was produced by a read-only closure scan of the old project (every /Game/ path in the package name tables,
# followed transitively from the KEEP set); see the docs for the method.
import json
import os
import shutil
import sys

OLD = "/Game/"
NEW = "/Game/Environment/Weather/"

# (old package, new subfolder under /Game/Environment/Weather, new name)
MANIFEST = [
    # Sky sphere (BP_DynamivSky's SM_SkySphere + dynamic material and the textures it uses)
    ("_BP/SM_SkySphere", "Sky", "SM_SkySphere"),
    ("Materials/Sky/M_DynSkySphere", "Sky", "M_DynSkySphere"),
    ("Materials/Sky/MI_DynSkySphere", "Sky", "MI_DynSkySphere"),
    ("Textures/Clouds/2D/T_Sky_Clouds_M", "Sky/Textures", "T_Sky_Clouds_M"),
    ("Textures/Moon/T_MoonNASA_1K", "Sky/Textures", "T_MoonNASA_1K"),
    ("Textures/Stars/T_Sky_Stars", "Sky/Textures", "T_Sky_Stars"),
    # Volumetric clouds
    # dropped 2026-10-22 (user: volumetric clouds are not visible): ("Materials/VolumetricClouds/M_VolumetricClouds_Master", "Clouds", "M_VolumetricClouds_Master"),
    # dropped 2026-10-22 (user: volumetric clouds are not visible): ("Materials/VolumetricClouds/MI_VolumetricClouds_", "Clouds", "MI_VolumetricClouds_"),
    # dropped 2026-10-22 (user: volumetric clouds are not visible): ("Textures/Clouds/2D/T_PackedCloudMasks", "Clouds/Textures", "T_PackedCloudMasks"),
    # dropped 2026-10-22 (user: volumetric clouds are not visible): ("Textures/Clouds/3D/T_VolumeNoiseShape128", "Clouds/Textures", "T_VolumeNoiseShape128"),
    # dropped 2026-10-22 (user: volumetric clouds are not visible): ("Textures/Clouds/3D/VT_PerlinWorley_Balanced", "Clouds/Textures", "VT_PerlinWorley_Balanced"),
    # Snow cover on surfaces
    ("Materials/MaterialFunction/Weather/MF_SnowyWeatherBlend", "Snow/Materials", "MF_SnowyWeatherBlend"),
    ("Materials/MaterialFunction/MF_LandscapeLayerProcessed", "Snow/Materials", "MF_LandscapeLayerProcessed"),
    ("Materials/MaterialFunction/MF_ComputeLandscapeUV", "Snow/Materials", "MF_ComputeLandscapeUV"),
    ("Materials/MaterialFunction/MF_TextureBombingVoronoi", "Snow/Materials", "MF_TextureBombingVoronoi"),
    ("Megascans/Surfaces/Fresh_Snow_ugkieimdy/T_Fresh_Snow_ugkieimdy_2K_D", "Snow/Textures", "T_Fresh_Snow_ugkieimdy_2K_D"),
    ("Megascans/Surfaces/Fresh_Snow_ugkieimdy/T_Fresh_Snow_ugkieimdy_2K_N", "Snow/Textures", "T_Fresh_Snow_ugkieimdy_2K_N"),
    ("Megascans/Surfaces/Fresh_Snow_ugkieimdy/T_FreshSnow_ugkieimdy_2K_ORDp", "Snow/Textures", "T_FreshSnow_ugkieimdy_2K_ORDp"),
    # Falling snow / blizzard (Niagara)
    ("_Niagara/_Snowdrop/NS_SnowStorm", "Snow/Niagara", "NS_SnowStorm"),
    ("_Niagara/_Snowdrop/M_SnowSmokeMaster_Add", "Snow/Niagara", "M_SnowSmokeMaster_Add"),
    ("_Niagara/_Snowdrop/M_SnowSmokeMaster_Add_Inst", "Snow/Niagara", "M_SnowSmokeMaster_Add_Inst"),
    ("_Niagara/_Snowdrop/T_Smoke_SubUV", "Snow/Niagara", "T_Smoke_SubUV"),
    ("_Niagara/_Templates/FollowCameraTemplate", "Snow/Niagara", "FollowCameraTemplate"),
    # Shared
    ("Materials/MaterialParameterCollection/MPC_WeatherSettings", "MPC", "MPC_WeatherSettings"),
    # dropped 2026-10-22 (user: volumetric clouds are not visible): ("Materials/MaterialFunction/MF_MavroVar", "Common/Materials", "MF_MavroVar"),
    # dropped 2026-10-22 (user: volumetric clouds are not visible): ("StarterContent/Textures/T_MacroVariation", "Common/Textures", "T_MacroVariation"),
    ("Textures/Noise/T_TilingNoisePacked", "Common/Textures", "T_TilingNoisePacked"),
    ("Textures/Noise/T_VoronoiNoise_Packed", "Common/Textures", "T_VoronoiNoise_Packed"),
    # Reference presets of the old BP_DynamivSky (the C++ presenter replaces them; kept as a look reference, no rain assets)
    ("_DataAssets/BP_WeatheDataAsset_Base", "Reference", "BP_WeatheDataAsset_Base"),
    ("_DataAssets/DA_Snowy", "Reference", "DA_Snowy"),
    ("_DataAssets/DA_Sunny", "Reference", "DA_Sunny"),
    ("_DataAssets/S_SkyAtmosphereSettings", "Reference", "S_SkyAtmosphereSettings"),
    ("_DataAssets/S_DirectionalLightSettings", "Reference", "S_DirectionalLightSettings"),
    ("_DataAssets/S_SkyLightSettings", "Reference", "S_SkyLightSettings"),
    ("_DataAssets/S_ExponentialHeightFogSettings", "Reference", "S_ExponentialHeightFogSettings"),
    ("_DataAssets/S_SnowyWeatherSettings", "Reference", "S_SnowyWeatherSettings"),
    ("Enums/EWeatherType", "Reference", "EWeatherType"),
]

OLD_SOURCE_PROJECT = r"C:\Users\Zephyrus15Duo\Documents\Unreal Projects\WeatherSystem_01 5.4"
SIDECAR_EXTS = (".uasset", ".uexp", ".ubulk", ".uptnl", ".umap")


def old_pkg(entry):
    return OLD + entry[0]


def new_pkg(entry):
    return NEW + entry[1] + "/" + entry[2]


def temp_top_dirs():
    return sorted({e[0].split("/")[0] for e in MANIFEST})


# --------------------------------------------------------------------------------------------------------- stage
def stage(content_dir):
    src_content = os.path.join(os.environ.get("CODEX_WEATHER_SRC", OLD_SOURCE_PROJECT), "Content")
    problems = []
    final_existing = [new_pkg(e) for e in MANIFEST if os.path.exists(os.path.join(content_dir, new_pkg(e)[len(OLD):].replace("/", os.sep) + ".uasset"))]
    if len(final_existing) == len(MANIFEST):
        print("[stage] all %d assets already exist under /Game/Environment/Weather - migration already done, nothing to stage." % len(MANIFEST))
        return True
    if final_existing:
        problems.append("partially migrated: %d of %d target assets already exist, e.g. %s" % (len(final_existing), len(MANIFEST), final_existing[0]))
    # The temporary original-path top folders must not exist in this project (they are deleted after the move).
    for top in temp_top_dirs():
        if os.path.exists(os.path.join(content_dir, top)):
            problems.append("temporary top folder already exists in the project (would be deleted by the move): Content/" + top)
    for e in MANIFEST:
        rel = e[0].replace("/", os.sep)
        if not os.path.isfile(os.path.join(src_content, rel + ".uasset")):
            problems.append("source missing: " + os.path.join(src_content, rel + ".uasset"))
    if problems:
        print("[stage] ABORT, nothing copied:")
        for p in problems:
            print("  - " + p)
        return False
    total = 0
    for e in MANIFEST:
        rel = e[0].replace("/", os.sep)
        for ext in SIDECAR_EXTS:
            s = os.path.join(src_content, rel + ext)
            if os.path.isfile(s):
                d = os.path.join(content_dir, rel + ext)
                os.makedirs(os.path.dirname(d), exist_ok=True)
                shutil.copyfile(s, d)  # copy only; the source is never touched
                total += os.path.getsize(d)
    print("[stage] copied %d assets, %.1f MB into %s" % (len(MANIFEST), total / 1048576.0, content_dir))
    return True


# --------------------------------------------------------------------------------------------------------- editor
def run_editor():
    import unreal

    project_dir = unreal.Paths.project_dir()
    content_dir = os.path.join(project_dir, "Content")
    log_path = os.path.join(project_dir, "Saved", "Logs", "migrate_weather_assets.txt")
    lines = []

    def log(msg):
        lines.append(msg)
        unreal.log("[WeatherMigrate] " + msg)

    def flush():
        os.makedirs(os.path.dirname(log_path), exist_ok=True)
        with open(log_path, "w", encoding="utf-8") as f:
            f.write("\n".join(lines) + "\n")

    eal = unreal.EditorAssetLibrary
    tools = unreal.AssetToolsHelpers.get_asset_tools()
    reg = unreal.AssetRegistryHelpers.get_asset_registry()

    # Already migrated?
    todo = [e for e in MANIFEST if eal.does_asset_exist(old_pkg(e))]
    if not todo:
        log("no staged assets found (migration already done or stage not run); going straight to verification.")
    else:
        # Only ever touch the staged packages: every old asset must exist, every new target must be free.
        for e in MANIFEST:
            if not eal.does_asset_exist(old_pkg(e)):
                log("ERROR: staged asset missing: " + old_pkg(e))
                flush()
                return
            if eal.does_asset_exist(new_pkg(e)):
                log("ERROR: target already exists: " + new_pkg(e))
                flush()
                return
        rename_data = []
        for e in MANIFEST:
            asset = eal.load_asset(old_pkg(e))
            if not asset:
                log("ERROR: cannot load " + old_pkg(e))
                flush()
                return
            rename_data.append(unreal.AssetRenameData(asset, NEW + e[1], e[2]))
        ok = tools.rename_assets(rename_data)
        log("rename_assets(%d) -> %s" % (len(rename_data), ok))

        # Redirectors left behind in the temporary folders (only those folders).
        for top in temp_top_dirs():
            folder = "/Game/" + top
            redirs = []
            for ad in reg.get_assets_by_path(folder, recursive=True):
                if str(ad.asset_class_path.asset_name) == "ObjectRedirector":
                    o = ad.get_asset()
                    if o:
                        redirs.append(o)
            if redirs:
                tools.fix_up_redirectors(redirs)
                log("fixed up %d redirector(s) in %s" % (len(redirs), folder))

    # Save only the moved assets (they are in the manifest; their referencers are too).
    saved = 0
    for e in MANIFEST:
        if eal.save_asset(new_pkg(e), only_if_is_dirty=False):
            saved += 1
        else:
            log("WARNING: save failed: " + new_pkg(e))
    log("saved %d / %d assets" % (saved, len(MANIFEST)))

    # Delete the temporary original-path folders (asset registry + disk) once they hold no asset.
    for top in temp_top_dirs():
        folder = "/Game/" + top
        left = [str(a.package_name) for a in reg.get_assets_by_path(folder, recursive=True)]
        if left:
            log("WARNING: %s still holds %d asset(s), not deleted: %s" % (folder, len(left), left[:5]))
            continue
        eal.delete_directory(folder)
        disk = os.path.join(content_dir, top)
        if os.path.isdir(disk):
            files = [os.path.join(r, f) for r, _, fs in os.walk(disk) for f in fs]
            if files:
                log("WARNING: %s has %d file(s) on disk, not deleted: %s" % (disk, len(files), files[:3]))
            else:
                shutil.rmtree(disk)  # only empty folders are left
                log("removed empty temporary folder Content/" + top)

    # Verify: everything loads, nothing references a package outside Environment/Weather (engine content is fine).
    bad = 0
    total_bytes = 0
    options = unreal.AssetRegistryDependencyOptions()
    options.include_hard_package_references = True
    options.include_soft_package_references = True
    options.include_searchable_names = False
    options.include_soft_management_references = False
    options.include_hard_management_references = False
    for e in MANIFEST:
        pkg = new_pkg(e)
        obj = eal.load_asset(pkg)
        disk = os.path.join(content_dir, pkg[len(OLD):].replace("/", os.sep) + ".uasset")
        size = os.path.getsize(disk) if os.path.isfile(disk) else -1
        total_bytes += max(size, 0)
        if not obj:
            log("FAIL load: " + pkg)
            bad += 1
            continue
        refs = []
        for d in reg.get_dependencies(pkg, options) or []:
            d = str(d)
            if d.startswith("/Game/") and not d.startswith(NEW):
                refs.append(d)
            elif d.startswith("/Game/") and not eal.does_asset_exist(d):
                refs.append(d + " (missing)")
        if refs:
            bad += 1
            log("BAD REFS %s -> %s" % (pkg, refs))
        log("OK %-95s %-22s %9.2f MB" % (pkg, obj.get_class().get_name(), size / 1048576.0))
    log("verified %d assets, %.1f MB, problems: %d" % (len(MANIFEST), total_bytes / 1048576.0, bad))
    flush()


try:
    import unreal  # noqa: F401  (inside the editor)
    _in_editor = True
except ImportError:
    _in_editor = False
if _in_editor:
    run_editor()
elif len(sys.argv) > 1 and sys.argv[1] == "stage":
    content = os.path.join(os.path.dirname(os.path.dirname(os.path.dirname(os.path.abspath(__file__)))), "Content")
    sys.exit(0 if stage(content) else 1)
else:
    print(__doc__)
