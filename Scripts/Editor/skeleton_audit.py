"""READ-ONLY skeleton audit (no saves, no asset changes) -> Saved/Logs/skeleton_audit.json.

Lists every Character Blueprint (mesh, anim class), every SkeletalMesh / Skeleton in /Game (bone count, hierarchy,
subset-of-UE5-Manny check, compatible-skeleton links, sockets, physics asset) and the AnimSequence / Montage / BlendSpace
count per skeleton. Feeds docs/port/skeleton_audit.md. Run (the editor may stay open):
  UnrealEditor-Cmd.exe CodexTactics.uproject -run=pythonscript -script="<abs path>" -unattended -nullrhi
"""
import json
import unreal

OUT = unreal.Paths.project_saved_dir() + 'Logs/skeleton_audit.json'
ar = unreal.AssetRegistryHelpers.get_asset_registry()
res = {'errors': []}


def pkg(obj_or_path):
    s = obj_or_path if isinstance(obj_or_path, str) else obj_or_path.get_path_name()
    return s.split('.')[0]


def skel_bones(skel):
    try:
        return [str(n) for n in skel.get_reference_pose().get_bone_names()]
    except Exception as e:
        res['errors'].append('bones %s: %s' % (skel.get_path_name(), e))
        return []


def compat_of(skel):
    out = []
    try:
        for c in skel.get_editor_property('compatible_skeletons'):
            out.append(pkg(c.to_tuple()[0] if hasattr(c, 'to_tuple') else c) if c else None)
    except Exception as e:
        res['errors'].append('compat %s: %s' % (skel.get_path_name(), e))
    return out


skeletons = {}
meshes_by_skel = {}
mesh_info = {}
for ad in ar.get_assets_by_class(unreal.TopLevelAssetPath('/Script/Engine', 'SkeletalMesh'), True):
    p = str(ad.package_name)
    if not p.startswith('/Game/'):
        continue
    m = unreal.load_asset(p)
    if not m or not m.skeleton:
        continue
    sk = m.skeleton
    sp = pkg(sk)
    if sp not in skeletons:
        skeletons[sp] = {'bones': skel_bones(sk), 'compat': compat_of(sk), 'obj': sk}
    parent = {}
    for b in skeletons[sp]['bones']:
        try:
            par = m.get_bone_parent(b)
        except Exception:
            par = None
        parent[b] = str(par) if par is not None else None
    sockets = []
    for i in range(m.num_sockets()):
        s = m.get_socket_by_index(i)
        sockets.append('%s@%s' % (s.get_editor_property('socket_name'), s.get_editor_property('bone_name')))
    pa = m.physics_asset
    mesh_info[p] = {'skeleton': sp, 'parents': parent, 'sockets': sockets, 'physics': pkg(pa) if pa else None}
    meshes_by_skel.setdefault(sp, []).append(p)

for ad in ar.get_assets_by_class(unreal.TopLevelAssetPath('/Script/Engine', 'Skeleton'), True):
    p = str(ad.package_name)
    if p.startswith('/Game/') and p not in skeletons:
        sk = unreal.load_asset(p)
        skeletons[p] = {'bones': skel_bones(sk), 'compat': compat_of(sk), 'obj': sk}

anim_count = {}
anim_names = {}
for cls in ('AnimSequence', 'AnimMontage', 'BlendSpace', 'AnimComposite'):
    for ad in ar.get_assets_by_class(unreal.TopLevelAssetPath('/Script/Engine', cls), True):
        p = str(ad.package_name)
        if not p.startswith('/Game/'):
            continue
        tag = str(ad.get_tag_value('Skeleton') or '')
        key = '?'
        if tag:
            key = tag.split("'")[-2].split('.')[0] if "'" in tag else tag.split('.')[0]
        anim_count.setdefault(key, {}).setdefault(cls, 0)
        anim_count[key][cls] += 1
        low = p.lower()
        if any(k in low for k in ('knock', 'revive', 'death', 'cover')):
            anim_names.setdefault(key, []).append(p)

abps = []
for ad in ar.get_assets_by_class(unreal.TopLevelAssetPath('/Script/Engine', 'AnimBlueprint'), True):
    p = str(ad.package_name)
    if not p.startswith('/Game/'):
        continue
    a = unreal.load_asset(p)
    ts = a.get_editor_property('target_skeleton') if a else None
    abps.append({'abp': p, 'skeleton': pkg(ts) if ts else None})

chars = []
for ad in ar.get_assets_by_class(unreal.TopLevelAssetPath('/Script/Engine', 'Blueprint'), True):
    p = str(ad.package_name)
    if not p.startswith('/Game/'):
        continue
    bp = unreal.load_asset(p)
    try:
        cdo = unreal.get_default_object(bp.generated_class())
    except Exception:
        continue
    if not isinstance(cdo, unreal.Character):
        continue
    entry = {'bp': p, 'cdo_class': type(cdo).__name__}
    try:
        comp = cdo.get_editor_property('mesh')
        mesh = comp.get_editor_property('skeletal_mesh_asset')
        entry['mesh'] = pkg(mesh) if mesh else None
        ac = comp.get_editor_property('anim_class')
        entry['anim_class'] = str(ac.get_path_name()) if ac else None
        entry['rel_rot'] = str(comp.get_editor_property('relative_rotation'))
    except Exception as e:
        entry['error'] = str(e)
    chars.append(entry)

manny_like = [sp for sp, d in skeletons.items() if 'spine_05' in d['bones'] and 'ik_foot_root' in d['bones']]
res['manny_candidates'] = manny_like
manny = None
for sp in manny_like:
    if 'mannequin' in sp.lower():
        manny = sp
        break
manny = manny or (manny_like[0] if manny_like else None)
res['manny'] = manny
mset = set(skeletons[manny]['bones']) if manny else set()
mpar = mesh_info[meshes_by_skel[manny][0]]['parents'] if manny and meshes_by_skel.get(manny) else {}

out_skel = {}
for sp, d in skeletons.items():
    bones = d['bones']
    ms = meshes_by_skel.get(sp, [])
    info = {'bone_count': len(bones), 'root': bones[0] if bones else None, 'compatible': d['compat'], 'meshes': ms,
            'anims': anim_count.get(sp, {}), 'special_anims': sorted(anim_names.get(sp, []))[:80]}
    missing = [b for b in bones if b not in mset]
    info['bones_not_in_manny'] = missing[:100]
    info['bones_not_in_manny_count'] = len(missing)
    info['manny_bones_not_here_count'] = len([b for b in mset if b not in set(bones)])
    if ms and mpar:
        pp = mesh_info[ms[0]]['parents']
        info['parent_mismatch'] = [(b, pp.get(b), mpar.get(b)) for b in bones if b in mset and pp.get(b) != mpar.get(b)][:40]
    info['is_manny_like'] = sp in manny_like
    info['bones'] = bones
    try:
        info['skeleton_sockets'] = ['%s@%s' % (x.get_editor_property('socket_name'), x.get_editor_property('bone_name')) for x in skeletons[sp].get('obj').get_editor_property('sockets')]
    except Exception as e:
        info['skeleton_sockets'] = ['n/a: %s' % e]
    out_skel[sp] = info
res['skeletons'] = out_skel
res['meshes'] = {p: {k: v for k, v in i.items() if k != 'parents'} for p, i in mesh_info.items()}
res['mesh_parents'] = {p: i['parents'] for p, i in mesh_info.items()}
res['characters'] = chars
res['abps'] = abps
res['unknown_skeleton_anims'] = anim_count.get('?', {})
with open(OUT, 'w') as f:
    json.dump(res, f, indent=1, default=str)
unreal.log('skeleton_audit: %d skeletons, %d meshes, %d character BPs -> %s' % (len(out_skel), len(mesh_info), len(chars), OUT))
