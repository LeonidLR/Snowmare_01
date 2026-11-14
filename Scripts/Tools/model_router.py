"""Model router for UE5 game-dev tasks in Claude Code (OFF by default).

Claude Code cannot switch the main session's model per prompt. The closest native architecture:
  1. a UserPromptSubmit hook runs `model_router.py --hook` before the main model answers;
  2. the router asks TypeSafe Jev ONE question — which of 4 complexity levels the task is (a choice with
     probabilities) — plus whether it is a short follow-up only understandable in the dialogue;
  3. when the level's probability >= the threshold (0.6) and it is not a follow-up, the hook adds a note telling the
     main session to hand the task to the level's subagent (.claude/agents/ue-l{1..4}-*.md, each pinned to a model:
     Haiku 4.5 / Sonnet 5.5 / Opus 5.5 / Fable 5.1); otherwise nothing is added and the main session does it itself.
Never blocking: disabled -> exits at once; any error / timeout -> exits 0 without output (the prompt runs as if the
router did not exist).

Config: .claude/model_router.json (enabled, threshold, level -> agent / model, estimate weights).
Stats:  Saved/ModelRouter/stats.json (tasks per level, self-handled, errors, estimated tokens / minutes saved).

Usage:
  python Scripts/Tools/model_router.py --on | --off | --status | --reset-stats
  python Scripts/Tools/model_router.py --classify "task text"     one task -> JSON (works while OFF)
  python Scripts/Tools/model_router.py --test                      the 10-example dry run as a table (works while OFF)
  python Scripts/Tools/model_router.py --hook                      (from the hook; reads the prompt JSON on stdin)
"""

import json
import os
import re
import sys
import threading
import time

ROOT = os.path.abspath(os.path.join(os.path.dirname(__file__), "..", ".."))
CONFIG = os.path.join(ROOT, ".claude", "model_router.json")
STATS = os.path.join(ROOT, "Saved", "ModelRouter", "stats.json")
sys.path.insert(0, os.path.dirname(__file__))
# UTF-8 out whatever the console code page (the hook's JSON and the status carry Cyrillic).
for _stream in (sys.stdout, sys.stderr):
    try:
        _stream.reconfigure(encoding="utf-8")
    except Exception:
        pass

LEVEL_CRITERIA = {
    "1": "Trivial: find a variable, rename something, fix comments, change a scalar parameter in a Material Instance, "
         "a simple one-line hotfix",
    "2": "Routine: a small C++ helper function, sketching Blueprint logic, a simple procedural material, a Python script "
         "automating the UE / Blender editor",
    "3": "Major: implementing a gameplay sub-component, a multi-step refactor of a system, designing a complex Niagara "
         "VFX effect, setting up basic variable replication",
    "4": "Hardest: designing a game system's architecture, multiplayer network architecture, deep crash / minidump "
         "debugging, designing the core loop or world-streaming systems",
}

# Short replies that only make sense inside the current dialogue (checked in code before asking Jev).
FOLLOWUP = re.compile(r"^\s*(ок|ok|окей|да|нет|ага|угу|давай|продолжай|дальше|ещё|еще|сделай (короче|длиннее|проще)|короче|"
                      r"запушь|закоммить|повтори|почему\??|а (если|почему|как)\b.*|это|так|хорошо|спасибо|отлично)\b[\s\S]{0,40}$",
                      re.IGNORECASE)


def load_config():
    with open(CONFIG, encoding="utf-8") as f:
        return json.load(f)


def save_config(config):
    with open(CONFIG, "w", encoding="utf-8") as f:
        json.dump(config, f, ensure_ascii=False, indent=2)
        f.write("\n")


def load_stats():
    try:
        with open(STATS, encoding="utf-8") as f:
            return json.load(f)
    except Exception:
        return {"routed": {"1": 0, "2": 0, "3": 0, "4": 0}, "self_low_confidence": 0, "self_followup": 0, "errors": 0,
                "tokens_saved_estimate": 0, "minutes_saved_estimate": 0.0, "last": None}


def save_stats(stats):
    os.makedirs(os.path.dirname(STATS), exist_ok=True)
    with open(STATS, "w", encoding="utf-8") as f:
        json.dump(stats, f, ensure_ascii=False, indent=2)


def is_followup_by_text(text):
    words = len(text.split())
    return words <= 3 or bool(FOLLOWUP.match(text.strip()))


def classify(text, config, timeout=None):
    """{level, model, agent, confidence, route, reason}. Never raises; a failure -> route False."""
    timeout = timeout if timeout is not None else config.get("timeout_seconds", 4.0)
    threshold = config.get("confidence_threshold", 0.6)
    result = {"level": None, "model": None, "agent": None, "confidence": 0.0, "route": False, "reason": ""}
    if is_followup_by_text(text):
        result["reason"] = "short follow-up (context of the dialogue): main session"
        result["followup"] = True
        return result
    try:
        import jev_client
    except Exception as error:
        result["reason"] = "router unavailable: %s" % error
        result["error"] = True
        return result
    state = {"project": "Unreal Engine 5.8 C++ tactical game (C++ gameplay, Blueprints, materials, shaders, Niagara, Python editor tools)",
             "request": text[:4000]}
    questions = {
        "level": {"type": "choice", "instructions": "Look at `request`: a task a game developer asks an AI coding assistant to do. "
                  "How complex is it to do well?", "criteria": LEVEL_CRITERIA},
        "followup": {"type": "noul", "instructions": "Look at `request`. Is it only a short reaction or clarification that cannot "
                     "be understood without the previous conversation (e.g. «ok, make it shorter»)?"},
    }
    box = {}

    def call():
        box["answer"] = jev_client.ask(state, questions, attempts=1, timeout=timeout)

    worker = threading.Thread(target=call, daemon=True)
    worker.start()
    worker.join(timeout + 0.5)
    if "answer" not in box:
        result["reason"] = "router timeout: main session"
        result["error"] = True
        return result
    answers, error = box["answer"]
    if answers is None:
        result["reason"] = "router error (%s): main session" % error
        result["error"] = True
        return result
    if jev_client.noul(answers, "followup", 0.0) >= 0.6:
        result["reason"] = "follow-up judged by the router: main session"
        result["followup"] = True
        return result
    level_answer = answers.get("level") or {}
    level = level_answer.get("choice")
    probabilities = level_answer.get("probabilities") or {}
    confidence = float(probabilities.get(level, level_answer.get("confidence", 0.0)) or 0.0)
    level_config = config["levels"].get(str(level)) if level else None
    if not level_config:
        result["reason"] = "no level: main session"
        return result
    result.update(level=str(level), model=level_config["model_name"], agent=level_config["agent"], confidence=confidence)
    result["route"] = confidence >= threshold
    result["reason"] = ("routed to %s" % level_config["agent"]) if result["route"] else "confidence %.0f%% < %.0f%%: main session" % (
        confidence * 100, threshold * 100)
    return result


def record(stats, config, result):
    if result.get("error"):
        stats["errors"] += 1
    elif result.get("followup"):
        stats["self_followup"] += 1
    elif not result["route"]:
        stats["self_low_confidence"] += 1
    else:
        level = result["level"]
        stats["routed"][level] = stats["routed"].get(level, 0) + 1
        model = config["levels"][level]["model"]
        flagship = config.get("flagship_model", "fable")
        cost, time_w = config["relative_cost_weight"], config["relative_time_weight"]
        stats["tokens_saved_estimate"] += int(config["estimated_tokens_per_task"][level] * max(0.0, cost[flagship] - cost[model]))
        stats["minutes_saved_estimate"] += round(config["estimated_minutes_per_task"][level] * max(0.0, time_w[flagship] - time_w[model]), 2)
    stats["last"] = {k: result.get(k) for k in ("level", "agent", "confidence", "route", "reason")}


def hook():
    started = time.time()
    try:
        config = load_config()
        if not config.get("enabled"):
            return 0  # OFF: nothing at all
        payload = json.loads(sys.stdin.buffer.read().decode("utf-8", errors="replace") or "{}")
        prompt = payload.get("prompt", "")
        if not prompt.strip() or prompt.strip().startswith("/"):
            return 0
        result = classify(prompt, config)
        if os.environ.get("CODEX_ROUTER_DEBUG") == "1":
            print(json.dumps(result, ensure_ascii=False), file=sys.stderr)
        stats = load_stats()
        record(stats, config, result)
        save_stats(stats)
        if not result["route"]:
            return 0
        level = config["levels"][result["level"]]
        note = ("[model-router] Уровень %s «%s», уверенность %.0f%%. Передай эту задачу субагенту `%s` (модель %s) через Agent "
                "с subagent_type=\"%s\" и полным самостоятельным описанием задачи; верни его результат пользователю, сохранив его "
                "последнюю строку «Выполнено моделью: …». Если задача требует контекста текущего диалога, которого у субагента нет, "
                "или делегирование явно хуже — выполни сам и скажи почему. (router %.1f s)") % (
            result["level"], level["name"], result["confidence"] * 100, level["agent"], level["model_name"], level["agent"],
            time.time() - started)
        print(json.dumps({"hookSpecificOutput": {"hookEventName": "UserPromptSubmit", "additionalContext": note}}, ensure_ascii=False))
    except Exception:
        if os.environ.get("CODEX_ROUTER_DEBUG") == "1":
            import traceback
            traceback.print_exc()
        try:
            stats = load_stats()
            stats["errors"] += 1
            save_stats(stats)
        except Exception:
            pass
    return 0


TEST_TASKS = [
    "Переименуй ноду CustomEvent «OnHitRecieved» в «OnHitReceived» в BP_Operative",
    "Найди, где в проекте объявлена переменная StanceBlendTime",
    "Поменяй в MI_Barricade скалярный параметр Roughness с 0.4 на 0.65",
    "Напиши небольшую C++ функцию-хелпер, которая возвращает ближайшего живого врага к точке",
    "Сделай Python-скрипт для редактора UE, который массово переименует все текстуры в папке по шаблону T_<Name>_<Suffix>",
    "Реализуй компонент инвентаря бойца на C++ с ячейками, весом и передачей предметов между бойцами",
    "Спроектируй эффект метели в Niagara с GPU-частицами, коллизией со снегом и зависимостью от силы ветра",
    "Спроектируй систему сохранения игры: слоты, версионирование, асинхронная запись, миграция старых сейвов и сохранение состояния мира",
    "ок, сделай короче",
    "а почему так?",
]


def test():
    config = load_config()
    rows = []
    for task in TEST_TASKS:
        result = classify(task, config, timeout=max(config.get("timeout_seconds", 4.0), 8.0))
        level = result["level"] or "—"
        name = config["levels"][result["level"]]["name"] if result["level"] else ("контекстная реплика" if result.get("followup") else "—")
        target = (config["levels"][result["level"]]["model_name"] if result["route"] else "основная сессия (без маршрутизации)")
        rows.append((task, "%s %s" % (level, name), target, "%.0f%%" % (result["confidence"] * 100) if result["level"] else "—", result["reason"]))
    print("| Задача | Уровень | Модель | Уверенность | Решение |")
    print("|---|---|---|---|---|")
    for row in rows:
        print("| %s |" % " | ".join(row))
    return 0


def status():
    config = load_config()
    stats = load_stats()
    print("model router: %s (threshold %.0f%%, timeout %.1f s)" % ("ON" if config.get("enabled") else "OFF",
          config["confidence_threshold"] * 100, config["timeout_seconds"]))
    for level, item in sorted(config["levels"].items()):
        print("  level %s %-11s -> %-16s %-45s tasks: %d" % (level, item["name"], item["agent"], item["model_name"], stats["routed"].get(level, 0)))
    print("  main session: low confidence %d, follow-ups %d; router errors / timeouts %d" % (
        stats["self_low_confidence"], stats["self_followup"], stats["errors"]))
    print("  estimated saving vs the flagship (relative units, see config notes): ~%d tokens, ~%.1f min" % (
        stats["tokens_saved_estimate"], stats["minutes_saved_estimate"]))
    return 0


def main(argv):
    if "--hook" in argv:
        return hook()
    if "--on" in argv or "--off" in argv:
        config = load_config()
        config["enabled"] = "--on" in argv
        save_config(config)
        return status()
    if "--reset-stats" in argv:
        if os.path.exists(STATS):
            os.remove(STATS)
        return status()
    if "--classify" in argv:
        text = argv[argv.index("--classify") + 1] if argv.index("--classify") + 1 < len(argv) else sys.stdin.read()
        print(json.dumps(classify(text, load_config()), ensure_ascii=False, indent=2))
        return 0
    if "--test" in argv:
        return test()
    return status()


if __name__ == "__main__":
    sys.exit(main(sys.argv[1:]))
