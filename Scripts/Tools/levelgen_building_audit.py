"""Static traversal audit of the buildings of accepted level-generator layouts (docs/design/levelgen/BUILDING_GENERATOR.md section 1).

Per building: the storey reachability graph (stairs + ladders, no drops), the room door graph per storey, stair geometry (slope,
storeys crossed without a slab hole), covers / props inside the footprint. Read-only; the TS validator buildingTraversal.ts replaces it.
    python Scripts/Tools/levelgen_building_audit.py <out.json>
"""
import os
import json, math, re, sys, collections
ROOT=os.path.join(os.path.dirname(os.path.abspath(__file__)), "..", "..", "Content", "Data", "LevelGen") + os.sep
LH=300.0
def kind(b):
    sp=b.get("set_piece") or "?"
    m=re.search(r'_(hall|office|shed|gallery|store|facade|block|pump|ruin|annex|chimney|warehouse|depot|tower|garage|silo)', b["id"])
    return "%s/%s"%(sp, m.group(1) if m else b["id"].split("_")[1] if "_" in b["id"] else "?")
def inside(p,b,m=0):
    (x0,y0),(x1,y1)=b["footprint"]; return x0-m<=p[0]<=x1+m and y0-m<=p[1]<=y1+m
def seg_hits_seg(a,b,c,d):
    def cr(o,p,q): return (p[0]-o[0])*(q[1]-o[1])-(p[1]-o[1])*(q[0]-o[0])
    return cr(a,b,c)*cr(a,b,d)<0 and cr(c,d,a)*cr(c,d,b)<0
def audit(name, propsname):
    d=json.load(open(ROOT+"accepted/%s.json"%name,encoding="utf-8"))
    props=[]
    try:
        P=json.load(open(ROOT+"props/%s.props.json"%propsname,encoding="utf-8"))
        for L in P["layers"]:
            for o in L.get("objects",[]): props.append(o)
    except Exception as e: pass
    ladders=d.get("ladders",[])
    rows=[]
    for b in d["buildings"]:
        bid=b["id"]; F=int(b["floors"]); hollow=bool(b.get("hollow")); issues=[]
        ext_doors={o["id"] for o in b.get("openings",[]) if o["kind"]=="door"}
        if not ext_doors: ext_doors={"%s_door"%bid}
        # floor reachability graph: levels 0..F (F = roof)
        edges=collections.defaultdict(set)
        for s in b.get("stairs",[]):
            f,t=int(s["from"]),int(s["to"]); edges[f].add(t); edges[t].add(f)
            run=math.hypot(s["top"][0]-s["pos"][0],s["top"][1]-s["pos"][1]) if s.get("kind")!="external" or not s.get("mid") else math.hypot(s["mid"][0]-s["pos"][0],s["mid"][1]-s["pos"][1])
            slope=math.degrees(math.atan2((t-f)*LH,run or 1))
            if slope>40: issues.append("stair %s slope %.0f deg"%(s["id"],slope))
            if not hollow and t-f>1: issues.append("stair %s %d->%d crosses %d intermediate slab(s) with NO hole"%(s["id"],f,t,t-f-1))
            if s.get("kind")=="stairwell" and t-f>=2: issues.append("stair %s one straight %.0f m ramp, %d m rise, no landing"%(s["id"],run/100,(t-f)*3))
            # ramp vs partitions on the from floor
            for p in b.get("partitions",[]):
                if int(p.get("floor",0))==f and seg_hits_seg(s["pos"],s["top"],p["start"],p["end"]):
                    issues.append("stair %s ramp crosses partition %s"%(s["id"],p["id"]))
        for l in ladders:
            to=l.get("to",""); fr=l.get("from","")
            if bid in to or bid in fr:
                lf,lt=int(l.get("level_from",0)),int(l.get("level_to",0))
                if fr=="ground" or bid in fr: edges[lf].add(lt); edges[lt].add(lf)
        seen={0}; st=[0]
        while st:
            x=st.pop()
            for y in edges[x]:
                if y not in seen: seen.add(y); st.append(y)
        enter=b.get("enterable"); rw=b.get("roof_walkable")
        lv_need=set()
        if enter and not hollow: lv_need|=set(range(0,F))
        if enter and hollow: lv_need|={0}
        if rw: lv_need.add(F)
        unreach=sorted(lv_need-seen) if (enter or rw) else []
        if unreach: issues.append("levels unreachable from ground: %s"%unreach)
        if enter and not b.get("doors"): issues.append("enterable, no door")
        if hollow and F>1: issues.append("hollow: %d 'floors' but no slabs - one %d m empty volume"%(F,F*3))
        upper_open=[o for o in b.get("openings",[]) if int(o.get("floor",0))>0]
        if hollow and F>1 and not upper_open: pass
        # rooms per floor
        rooms=list(b.get("rooms",[]))
        rf=collections.Counter(int(r["floor"]) for r in rooms)
        if enter and not hollow and F>1:
            empty=[f for f in range(1,F) if rf.get(f,0)==0]
            if empty: issues.append("upper floor(s) %s: no rooms/partitions defined"%empty)
        # room door graph per floor
        byfloor=collections.defaultdict(list)
        for r in rooms: byfloor[int(r["floor"])].append(r)
        unr_rooms=[]
        for f,rs in byfloor.items():
            if len(rs)<=1: continue
            # seeds: rooms with an exterior door (floor 0) or containing a stair top/foot (upper)
            seeds=set()
            for i,r in enumerate(rs):
                if f==0 and any(x in ext_doors for x in r["door_ids"]): seeds.add(i)
                for s in b.get("stairs",[]):
                    for pt,lv in ((s["pos"],int(s["from"])),(s["top"],int(s["to"]))):
                        if lv==f and r["bounds"]["min"][0]<=pt[0]<=r["bounds"]["max"][0] and r["bounds"]["min"][1]<=pt[1]<=r["bounds"]["max"][1]: seeds.add(i)
            reach=set(seeds); ch=True
            while ch:
                ch=False
                for i,r in enumerate(rs):
                    if i in reach: continue
                    if any(set(r["door_ids"])&set(rs[j]["door_ids"]) for j in reach): reach.add(i); ch=True
            unr_rooms += ["f%d:%s"%(f,rs[i]["id"].split("_")[-1]) for i in range(len(rs)) if i not in reach]
        if unr_rooms: issues.append("rooms unreachable via door graph: %s"%unr_rooms[:6])
        # interior content
        area=(b["footprint"][1][0]-b["footprint"][0][0])*(b["footprint"][1][1]-b["footprint"][0][1])/1e4
        covers_in=sum(1 for c in d.get("covers",[]) if inside(c["pos"],b,-30))
        props_in=sum(1 for o in props if inside(o["pos"],b,-30))
        if enter and covers_in+props_in==0: issues.append("interior: 0 covers / 0 props on %.0f m2"%area)
        rows.append(dict(id=bid,kind=kind(b),F=F,hollow=hollow,enter=bool(enter),roof=bool(rw),rot=b.get("rotation",0),
            area=round(area),rooms=len(rooms),parts=len(b.get("partitions",[])),stairs=["%s:%d->%d"%(s.get("kind"),s["from"],s["to"]) for s in b.get("stairs",[])],
            hatch=len(b.get("hatches",[])),reach=sorted(seen),covers_in=covers_in,props_in=props_in,issues=issues))
    return rows
out={}
for name,pn in [("gen_ref_01_02_depot_1_x6","L_Gen_Depot_1_X6"),("gen_ref_01_02_depot_1_x4","L_Gen_Depot_X4"),("L_Gen_Train_2","L_Gen_Train_2")]:
    out[name]=audit(name,pn)
json.dump(out,open(sys.argv[1],"w",encoding="utf-8"),indent=1)
# summary per kind for x6
for name in out:
    print("=====",name)
    agg=collections.defaultdict(lambda: collections.Counter())
    n=collections.Counter()
    for r in out[name]:
        n[r["kind"]]+=1
        for i in r["issues"]:
            agg[r["kind"]][re.sub(r'(stair|partition) \S+','\1 X',re.sub(r'\[.*\]|\d+(\.\d+)?','#',i))]+=1
    for k in sorted(n):
        print(" %-34s n=%d"%(k,n[k]))
        for i,c in agg[k].most_common(): print("     %3d x %s"%(c,i))
