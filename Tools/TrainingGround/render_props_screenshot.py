"""
Renders a visual screenshot of the Training Ground Props Panel for L_Gen_Depot_1.
Outputs to docs/port/gemini/outbox/screenshots/props_panel_depot.png.
"""
import json
import math
import os
import sys
from PIL import Image, ImageDraw, ImageFont

ROOT = os.path.abspath(os.path.join(os.path.dirname(__file__), "../.."))
DEPOT_LAYOUT_PATH = os.path.join(ROOT, "Content/Data/LevelGen/accepted/gen_ref_01_02_depot_1.json")
DEPOT_PROPS_PATH = os.path.join(ROOT, "Content/Data/LevelGen/props/L_Gen_Depot_1.props.json")
OUT_PNG = os.path.join(ROOT, "docs/port/gemini/outbox/screenshots/props_panel_depot.png")

ZONE_TINT = {
    "street": (120, 120, 130), "facade": (90, 70, 60), "compound": (150, 110, 60), "t_junction": (140, 140, 90),
    "industrial_hall": (110, 80, 50), "block": (110, 90, 70), "warehouse": (120, 100, 80), "junkyard": (170, 90, 40),
    "cut": (40, 60, 110), "tank_farm": (130, 90, 70), "open": (60, 130, 70), "ruin_two_level": (150, 60, 50),
    "yard": (120, 110, 80), "corridor": (160, 140, 60), "wall_run": (100, 100, 170), "gap_run": (170, 100, 140),
    "corner": (60, 160, 130), "wall_end": (180, 120, 60), "alley": (150, 150, 100), "square": (60, 130, 70),
    "dead_end": (170, 80, 80), "pipe_yard": (130, 100, 60)
}

FONT = ImageFont.load_default()

def render():
    with open(DEPOT_LAYOUT_PATH, "r", encoding="utf-8") as f:
        layout = json.load(f)
    with open(DEPOT_PROPS_PATH, "r", encoding="utf-8") as f:
        props = json.load(f)

    # Canvas dimensions for Training Ground app mockup
    total_w = 1600
    total_h = 920

    img = Image.new("RGB", (total_w, total_h), (11, 15, 25))
    d = ImageDraw.Draw(img)

    # 1. Top Navbar
    d.rectangle([0, 0, total_w, 48], fill=(15, 23, 42))
    d.line([(0, 48), (total_w, 48)], fill=(30, 41, 59), width=1)
    d.text((24, 18), "TRAINING GROUND", fill=(226, 232, 240), font=FONT)

    # Navbar tabs
    tabs = [("Волны", False), ("Перемещение", False), ("Левел-дизайн", False), ("Боевая лаборатория", False), ("Генератор уровней", True)]
    tx = 160
    for name, active in tabs:
        tw = len(name) * 8 + 24
        fill_box = (8, 145, 178) if active else (30, 41, 59)
        text_col = (11, 15, 25) if active else (203, 213, 225)
        d.rectangle([tx, 10, tx + tw, 38], fill=fill_box)
        d.text((tx + 12, 18), name, fill=text_col, font=FONT)
        tx += tw + 10

    # 2. Sub-tab bar (LevelGen Department sub-tabs)
    d.rectangle([0, 49, total_w, 88], fill=(15, 23, 42))
    d.line([(0, 88), (total_w, 88)], fill=(30, 41, 59), width=1)

    subtabs = [("[ Пропсы уровней (T5) ]", True), ("[ Генератор раскладок ]", False)]
    stx = 24
    for sname, sactive in subtabs:
        stw = len(sname) * 8 + 20
        sfill = (8, 145, 178) if sactive else (30, 41, 59)
        stext = (11, 15, 25) if sactive else (203, 213, 225)
        d.rectangle([stx, 56, stx + stw, 80], fill=sfill)
        d.text((stx + 10, 62), sname, fill=stext, font=FONT)
        stx += stw + 12

    d.text((stx + 30, 64), "codex.levelgen_props/1 * PROPS_LAYERS.md", fill=(100, 116, 139), font=FONT)

    # Layout geometry: 3 columns
    # Left: 320 px, Right: 340 px, Middle: remaining ~ 880 px
    left_w = 320
    right_w = 340
    middle_x = left_w + 32
    middle_w = total_w - middle_x - right_w - 24
    content_y = 104
    content_h = total_h - content_y - 20

    # 3. Left Panel (Layers & Reroll)
    d.rectangle([16, content_y, 16 + left_w, content_y + content_h], fill=(15, 23, 42), outline=(30, 41, 59))
    d.text((28, content_y + 16), "ФАЙЛ ПРОПСОВ", fill=(148, 163, 184), font=FONT)

    # File selector box
    d.rectangle([28, content_y + 36, 16 + left_w - 12, content_y + 68], fill=(2, 6, 23), outline=(51, 65, 85))
    d.text((38, content_y + 46), "L_Gen_Depot_1.props.json (main)", fill=(241, 245, 249), font=FONT)
    d.text((28, content_y + 74), "Content/Data/LevelGen/props/L_Gen_Depot_1.props.json", fill=(100, 116, 139), font=FONT)

    # Badges
    d.rectangle([28, content_y + 96, 74, content_y + 116], fill=(30, 41, 59))
    d.text((34, content_y + 101), f"rev {props.get('revision', 1)}", fill=(103, 232, 249), font=FONT)

    tot_props = sum(len(l.get("objects", [])) for l in props.get("layers", []))
    d.rectangle([82, content_y + 96, 172, content_y + 116], fill=(30, 41, 59))
    d.text((88, content_y + 101), f"{tot_props} объектов", fill=(148, 163, 184), font=FONT)

    # Buttons
    d.rectangle([28, content_y + 128, 155, content_y + 160], fill=(8, 145, 178))
    d.text((40, content_y + 138), "Сгенерировать все", fill=(2, 6, 23), font=FONT)

    d.rectangle([165, content_y + 128, 16 + left_w - 12, content_y + 160], fill=(5, 150, 105))
    d.text((188, content_y + 138), "Сохранить", fill=(2, 6, 23), font=FONT)

    # Layers List
    d.text((28, content_y + 180), "СЛОИ РАЗМЕЩЕНИЯ", fill=(148, 163, 184), font=FONT)

    ly_y = content_y + 204
    for l in props.get("layers", []):
        lid = l.get("id", "")
        kind = l.get("kind", "")
        seed = l.get("seed", 1)
        objs = l.get("objects", [])
        rep = l.get("report", {})
        gate = rep.get("gate", "pass")

        # Layer card
        d.rectangle([28, ly_y, 16 + left_w - 12, ly_y + 116], fill=(2, 6, 23), outline=(51, 65, 85))

        # Title + kind
        d.text((40, ly_y + 12), f"[V] {lid.upper()}", fill=(241, 245, 249), font=FONT)
        d.rectangle([130, ly_y + 10, 180, ly_y + 26], fill=(30, 41, 59))
        d.text((136, ly_y + 13), kind, fill=(148, 163, 184), font=FONT)

        # Seed & Reroll button
        d.text((40, ly_y + 44), f"Seed: {seed}", fill=(148, 163, 184), font=FONT)
        d.rectangle([190, ly_y + 36, 16 + left_w - 24, ly_y + 60], fill=(30, 41, 59), outline=(51, 65, 85))
        d.text((200, ly_y + 42), "Перебросить", fill=(103, 232, 249), font=FONT)

        # Summary line
        d.rectangle([40, ly_y + 74, 16 + left_w - 24, ly_y + 98], fill=(15, 23, 42))
        d.text((48, ly_y + 80), f"{len(objs)} шт. размещено", fill=(226, 232, 240), font=FONT)
        gate_col = (52, 211, 153) if gate == "pass" else (248, 113, 113)
        d.text((220, ly_y + 80), f"GATE: {gate.upper()}", fill=gate_col, font=FONT)

        ly_y += 130

    # 4. Right Panel (Report & Actions)
    rx = total_w - right_w - 16
    d.rectangle([rx, content_y, rx + right_w, content_y + content_h], fill=(15, 23, 42), outline=(30, 41, 59))
    d.text((rx + 16, content_y + 16), "ОТЧЕТ РАЗМЕЩЕНИЯ (T2)", fill=(148, 163, 184), font=FONT)

    ry_y = content_y + 44
    for l in props.get("layers", []):
        lid = l.get("id", "")
        rep = l.get("report", {})
        gate = rep.get("gate", "pass")
        req = rep.get("requested", len(l.get("objects", [])))
        placed = rep.get("placed", len(l.get("objects", [])))
        kept = rep.get("kept", 0)
        short = rep.get("short_by", 0)

        d.rectangle([rx + 16, ry_y, rx + right_w - 16, ry_y + 130], fill=(2, 6, 23), outline=(51, 65, 85))
        d.text((rx + 26, ry_y + 12), f"Слой: {lid}", fill=(241, 245, 249), font=FONT)

        gate_col = (52, 211, 153) if gate == "pass" else (248, 113, 113)
        d.text((rx + 200, ry_y + 12), f"GATE: {gate.upper()}", fill=gate_col, font=FONT)

        d.text((rx + 26, ry_y + 36), f"Запрошено: {req}   Размещено: {placed}", fill=(148, 163, 184), font=FONT)
        d.text((rx + 26, ry_y + 54), f"Сохранено (kept): {kept}   Нехватка: {short}", fill=(148, 163, 184), font=FONT)

        rej = rep.get("rejected", {})
        tot_rej = sum(rej.values()) if rej else 0
        d.text((rx + 26, ry_y + 76), f"Отклонено кандидатов: {tot_rej} шт.", fill=(203, 213, 225), font=FONT)
        d.text((rx + 26, ry_y + 96), f"route: {rej.get('route', 0)}  spacing: {rej.get('spacing', 0)}  door: {rej.get('door', 0)}", fill=(100, 116, 139), font=FONT)

        ry_y += 146

    # Unreal buttons on right
    d.line([(rx + 16, ry_y + 10), (rx + right_w - 16, ry_y + 10)], fill=(30, 41, 59), width=1)
    d.text((rx + 16, ry_y + 24), "ИНТЕГРАЦИЯ С UNREAL", fill=(148, 163, 184), font=FONT)

    d.rectangle([rx + 16, ry_y + 48, rx + right_w - 16, ry_y + 80], fill=(30, 41, 59), outline=(51, 65, 85))
    d.text((rx + 60, ry_y + 58), "Открыть в Unreal", fill=(241, 245, 249), font=FONT)

    d.rectangle([rx + 16, ry_y + 92, rx + right_w - 16, ry_y + 124], fill=(30, 41, 59), outline=(51, 65, 85))
    d.text((rx + 36, ry_y + 102), "Прогнать маршрут с пропсами", fill=(241, 245, 249), font=FONT)

    d.text((rx + 16, ry_y + 138), "OnWorldBeginPlay: спавн из JSON.\nПересборка карты не требуется.", fill=(100, 116, 139), font=FONT)

    # 5. Middle Panel: Layout Canvas + Props Overlay
    d.rectangle([middle_x, content_y, middle_x + middle_w, content_y + content_h], fill=(15, 23, 42), outline=(30, 41, 59))

    # Canvas inner view
    cv_x = middle_x + 12
    cv_y = content_y + 44
    cv_w = middle_w - 24
    cv_h = content_h - 110

    d.rectangle([cv_x, cv_y, cv_x + cv_w, cv_y + cv_h], fill=(25, 28, 34), outline=(51, 65, 85))

    # Toolbar above canvas
    d.text((cv_x + 6, content_y + 16), "Раскладка: gen_ref_01_02_depot_1   Слой Z: Все уровни", fill=(226, 232, 240), font=FONT)
    d.text((cv_x + cv_w - 150, content_y + 16), "[x] Зоны исключений", fill=(148, 163, 184), font=FONT)

    # Geometry bounds & transform
    (min_x, min_y), (max_x, max_y) = layout["bounds"]["min"], layout["bounds"]["max"]
    dx_cm = max_x - min_x
    dy_cm = max_y - min_y
    pad = 20
    scale = min((cv_w - 2 * pad) / dy_cm, (cv_h - 2 * pad) / dx_cm)
    ox = cv_x + (cv_w - dy_cm * scale) / 2
    oy = cv_y + (cv_h - dx_cm * scale) / 2

    def P(pt):
        # pt: [x_world, y_world]
        # Screen X = ox + (pt[1] - min_y) * scale
        # Screen Y = oy + (max_x - pt[0]) * scale
        return (ox + (pt[1] - min_y) * scale, oy + (max_x - pt[0]) * scale)

    # Draw zones
    for z in layout.get("zones", []):
        c = ZONE_TINT.get(z["kind"], (150, 150, 150))
        pts = [P(p) for p in z["poly"]]
        d.polygon(pts, fill=tuple(int(v * 0.35 + 25 * 0.65) for v in c), outline=c)

    # Draw buildings
    for b in layout.get("buildings", []):
        corners = b["footprint"]
        if len(corners) == 2:
            (ax, ay), (bx, by) = corners
            corners = [[ax, ay], [bx, ay], [bx, by], [ax, by]]
        fp = [P(p) for p in corners]
        fill = (70, 70, 78) if b.get("set_piece") == "chimney" else (150, 120, 100) if b.get("height", 0) >= 600 else (170, 150, 120)
        d.polygon(fp, fill=fill, outline=(255, 255, 255))
        if b.get("roof_walkable"):
            d.polygon(fp, outline=(80, 220, 120))
        for door in b.get("doors", []):
            dp = door["pos"] if isinstance(door, dict) else door
            dq = P(dp)
            d.ellipse([dq[0] - 3, dq[1] - 3, dq[0] + 3, dq[1] + 3], fill=(255, 255, 255))
        for hole in b.get("roof_holes", []):
            hq = P(hole)
            d.rectangle([hq[0] - 4, hq[1] - 4, hq[0] + 4, hq[1] + 4], outline=(255, 120, 60), width=2)
        for s in b.get("stairs", []):
            sq = P(s["pos"])
            d.rectangle([sq[0] - 4, sq[1] - 4, sq[0] + 4, sq[1] + 4], fill=(80, 220, 120))

    # Draw walls
    for w in layout.get("walls", []):
        a = P(w["a"])
        b = P(w["b"])
        col = (200, 200, 210) if w.get("kind", "wall") != "fence" else (230, 230, 120)
        th = max(1, int(w.get("thickness", 20) * scale))
        d.line([a, b], fill=col, width=th)

    # Draw covers
    for c in layout.get("covers", []):
        cq = P(c["pos"])
        cr = max(2, c.get("size", [100, 60])[0] / 2 * scale)
        d.rectangle([cq[0] - cr, cq[1] - cr * 0.6, cq[0] + cr, cq[1] + cr * 0.6], fill=(120, 110, 90), outline=(200, 190, 160))

    # Draw ladders
    for l in layout.get("ladders", []):
        lq = P(l["pos"])
        d.rectangle([lq[0] - 4, lq[1] - 7, lq[0] + 4, lq[1] + 7], fill=(255, 230, 0), outline=(0, 0, 0))

    # Draw routes
    for r in layout.get("routes", []):
        main = r.get("mandatory", False)
        col = (0, 140, 255) if main else (90, 200, 255)
        d.line([P(p) for p in r["points"]], fill=col, width=4 if main else 2)

    # Draw start & spawn
    sq = P(layout["spawn"]["pos"])
    d.ellipse([sq[0] - 10, sq[1] - 10, sq[0] + 10, sq[1] + 10], outline=(0, 230, 90), width=3)
    if layout.get("end"):
        eq = P(layout["end"])
        d.rectangle([eq[0] - 8, eq[1] - 8, eq[0] + 8, eq[1] + 8], outline=(255, 80, 80), width=3)

    # 6. Props Overlay!
    # Draw Crates (blue squares) and Barrels (red circles)
    for l in props.get("layers", []):
        kind = l.get("kind", "")
        for o in l.get("objects", []):
            oq = P(o["pos"])
            is_locked = o.get("locked", False)
            yaw = o.get("yaw", 0)

            if kind == "crates":
                # Crate: blue square
                sz = max(4, 50 * scale)
                # Rotate rect
                rad = math.radians(-yaw)
                cos_a, sin_a = math.cos(rad), math.sin(rad)
                corners = [(-sz, -sz), (sz, -sz), (sz, sz), (-sz, sz)]
                rot_corners = [(oq[0] + x * cos_a - y * sin_a, oq[1] + x * sin_a + y * cos_a) for x, y in corners]

                fill_col = (2, 132, 199)
                outline_col = (251, 191, 36) if is_locked else (125, 211, 252)
                width = 2 if is_locked else 1
                d.polygon(rot_corners, fill=fill_col, outline=outline_col)

                if is_locked:
                    # Gold lock dot
                    d.ellipse([oq[0] + 5, oq[1] - 5, oq[0] + 8, oq[1] - 2], fill=(251, 191, 36))

            elif kind == "barrels":
                # Barrel: red circle
                rad = max(4, 32 * scale)
                fill_col = (239, 68, 68)
                outline_col = (251, 191, 36) if is_locked else (248, 113, 113)
                d.ellipse([oq[0] - rad, oq[1] - rad, oq[0] + rad, oq[1] + rad], fill=fill_col, outline=outline_col, width=2 if is_locked else 1)

                # Inner yellow core for explosive
                d.ellipse([oq[0] - rad * 0.4, oq[1] - rad * 0.4, oq[0] + rad * 0.4, oq[1] + rad * 0.4], fill=(251, 191, 36))

    # Canvas Legend Box
    d.rectangle([cv_x + 12, cv_y + 12, cv_x + 160, cv_y + 88], fill=(15, 23, 42), outline=(51, 65, 85))
    # Crates icon
    d.rectangle([cv_x + 20, cv_y + 20, cv_x + 30, cv_y + 30], fill=(2, 132, 199), outline=(125, 211, 252))
    d.text((cv_x + 38, cv_y + 19), "Ящики (crates)", fill=(226, 232, 240), font=FONT)
    # Barrels icon
    d.ellipse([cv_x + 20, cv_y + 38, cv_x + 30, cv_y + 48], fill=(239, 68, 68), outline=(248, 113, 113))
    d.ellipse([cv_x + 23, cv_y + 41, cv_x + 27, cv_y + 45], fill=(251, 191, 36))
    d.text((cv_x + 38, cv_y + 38), "Бочки (взрывные)", fill=(226, 232, 240), font=FONT)
    # Locked icon
    d.rectangle([cv_x + 20, cv_y + 56, cv_x + 30, cv_y + 66], outline=(251, 191, 36), width=2)
    d.text((cv_x + 38, cv_y + 56), "Заблокировано", fill=(251, 191, 36), font=FONT)

    # Info footer inside canvas
    d.text((cv_x + 12, cv_y + cv_h - 20), f"Map: L_Gen_Depot_1 (177 x 124 м)   rev {props.get('revision', 1)}   {tot_props} объектов", fill=(251, 191, 36), font=FONT)

    # Selected prop card below canvas
    card_y = cv_y + cv_h + 12
    d.rectangle([cv_x, card_y, cv_x + cv_w, card_y + 44], fill=(2, 6, 23), outline=(8, 145, 178))
    d.rectangle([cv_x + 10, card_y + 10, cv_x + 160, card_y + 34], fill=(8, 47, 73))
    d.text((cv_x + 18, card_y + 16), "crates_00000001_0", fill=(103, 232, 249), font=FONT)
    d.text((cv_x + 175, card_y + 16), "Вариант: crate_s", fill=(226, 232, 240), font=FONT)
    d.text((cv_x + 300, card_y + 16), "X: -1600 см, Y: -1550 см", fill=(148, 163, 184), font=FONT)
    d.text((cv_x + 470, card_y + 16), "Yaw: 90°", fill=(148, 163, 184), font=FONT)
    d.text((cv_x + 550, card_y + 16), "Слой: ground", fill=(148, 163, 184), font=FONT)

    d.rectangle([cv_x + cv_w - 140, card_y + 8, cv_x + cv_w - 10, card_y + 36], fill=(69, 26, 3), outline=(180, 83, 9))
    d.text((cv_x + cv_w - 124, card_y + 16), "[*] Заблокирован", fill=(251, 191, 36), font=FONT)

    os.makedirs(os.path.dirname(OUT_PNG), exist_ok=True)
    img.save(OUT_PNG)
    print(f"Screenshot rendered to {OUT_PNG} ({total_w}x{total_h})")

if __name__ == "__main__":
    render()
