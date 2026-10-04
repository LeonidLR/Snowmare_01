#!/usr/bin/env python3
# ==============================================================================
# Codex Tactics — TypeSafe (Jev System One) Triage & Verification Bridge
# 
# Coordinates tasks between Gemini (Architect/Shaders), Jev (System 1 Decisions),
# and Claude Code (Opus 5.5 Gameplay/C++ Implementation).
#
# Primitives:
# - Choice: Deterministic classification into agent/subsystem domains
# - Noul: Calibrated condition probabilities (violations, regressions)
# - Score: Graded complexity & risk assessment
# ==============================================================================

import os
import sys
import json
import argparse
import urllib.request
import urllib.error
import subprocess
import re

sys.path.insert(0, os.path.dirname(__file__))
import jev_client  # noqa: E402

ROOT = jev_client.ROOT

# Ensure UTF-8 output on Windows consoles
if hasattr(sys.stdout, "reconfigure"):
    sys.stdout.reconfigure(encoding="utf-8", errors="replace")
if hasattr(sys.stderr, "reconfigure"):
    sys.stderr.reconfigure(encoding="utf-8", errors="replace")

API_URL = "https://api.typesafe.ai/v1/systemone"
DEFAULT_MODEL = "jev-latest"

def get_api_key():
    return jev_client.api_key()  # environment, registry, Saved/Config/typesafe.key

def query_typesafe(state, questions, model=DEFAULT_MODEL):
    """Executes a typed evaluation against TypeSafe Jev."""
    api_key = get_api_key()
    if not api_key:
        print("[TypeSafe:WARNING] TYPESAFE_API_KEY not set in environment. Running in local heuristic mode.", file=sys.stderr)
        return simulate_offline_evaluation(state, questions)

    payload = {
        "state": state,
        "model": model,
        "questions": questions
    }

    req = urllib.request.Request(
        API_URL,
        data=json.dumps(payload).encode("utf-8"),
        headers={
            "Authorization": f"Bearer {api_key}",
            "Content-Type": "application/json"
        },
        method="POST"
    )

    try:
        with urllib.request.urlopen(req, timeout=10) as resp:
            data = json.loads(resp.read().decode("utf-8"))
            return data.get("answers", {})
    except urllib.error.HTTPError as e:
        error_body = e.read().decode("utf-8")
        print(f"[TypeSafe:ERROR] API returned {e.code}: {error_body}", file=sys.stderr)
        return simulate_offline_evaluation(state, questions)
    except Exception as e:
        print(f"[TypeSafe:ERROR] Failed to connect: {e}", file=sys.stderr)
        return simulate_offline_evaluation(state, questions)

def simulate_offline_evaluation(state, questions):
    """Heuristic fallback when API key is not yet configured, preserving exact schema."""
    state_str = str(state).lower()
    answers = {}

    for q_id, q_data in questions.items():
        q_type = q_data.get("type")
        instructions = str(q_data.get("instructions", "")).lower()

        if q_type == "choice":
            criteria = q_data.get("criteria", [])
            selected = criteria[0] if criteria else "unknown"
            
            # Simple keyword matching for offline fallback
            if "claude" in instructions or "agent" in instructions or "responsibility" in instructions:
                if any(k in state_str for k in ["shader", "material", "vfx", "scalability", "render"]):
                    selected = "Gemini_Shaders_VFX"
                elif any(k in state_str for k in ["c++", "ai", "anim", "test", "subsystem", "character", "bug"]):
                    selected = "Claude_Gameplay_CPP"
                elif any(k in state_str for k in ["balance", "hp", "speed", "damage"]):
                    selected = "Balance_Data_Config"
                else:
                    selected = "User_Discussion"
            elif "bottleneck" in instructions or "failure" in instructions:
                if "acid" in state_str or "spitter" in state_str:
                    selected = "SpitterAcidOverdamage"
                elif "cold" in state_str or "freeze" in state_str:
                    selected = "ColdAccumulation"
                elif "ammo" in state_str:
                    selected = "AmmoStarvation"
                else:
                    selected = "FlankBreach"

            answers[q_id] = {
                "selected": selected,
                "confidence": 0.88,
                "probabilities": {opt: (0.88 if opt == selected else 0.04) for opt in criteria}
            }

        elif q_type == "noul":
            # Probability evaluation
            prob = 0.5
            if "violate" in instructions or "boundary" in instructions:
                prob = 0.95 if any(f in state_str for f in ["shaders/", "content/vfx/"]) and "claude" in state_str else 0.05
            elif "complex" in instructions or "c++" in instructions:
                prob = 0.85 if any(k in state_str for k in ["class", "component", "algorithm", "c++", "subsystem"]) else 0.20
            elif "urgent" in instructions or "crash" in instructions:
                prob = 0.90 if any(k in state_str for k in ["crash", "freeze", "fail", "broken"]) else 0.15

            answers[q_id] = {
                "value": prob
            }

        elif q_type == "score":
            answers[q_id] = {
                "score": 3.0,
                "confidence": 0.85
            }

    return answers

# ------------------------------------------------------------------------------
# Action 1: Task Triage (Маршрутизация входящей задачи)
# ------------------------------------------------------------------------------
def cmd_triage(task_description):
    print(f"\n[Jev:System1] Triaging task: '{task_description}'...")

    questions = {
        "assigned_agent": {
            "type": "choice",
            "instructions": "Which agent should own and implement this task based on project rules?",
            "criteria": {
                "Claude_Gameplay_CPP": "Gameplay C++ code, AI behavior, animation graphs, or unit tests",
                "Gemini_Shaders_VFX": "Materials, shaders, VFX, performance, scalability, rendering",
                "Balance_Data_Config": "Numerical balance tweaks, DataAssets, weapon stats",
                "User_Discussion": "High-level game design, artistic choice, or user preference"
            }
        },
        "requires_deep_cpp": {
            "type": "noul",
            "instructions": "Does this task require writing new C++ classes, refactoring logic, or complex algorithms?",
            "criteria": {
                "true": "Requires C++ compilation, tests, or header changes",
                "false": "Can be handled via asset tweaks, shaders, or configs"
            }
        },
        "task_domain": {
            "type": "choice",
            "instructions": "What is the primary technical domain of this feature or fix?",
            "criteria": {
                "Enemy_AI_Behavior": "AI controllers, behavior trees, state machines, enemy tactical logic",
                "Animation_ABP": "Animation blueprints, blend spaces, montages, bone layered blending",
                "Turn_Based_Gorky_Grid": "Turn-based combat subsystem, Gorky 17 grid, AP budgets",
                "Combat_Rules_Weapons": "Damage formulas, weapon data assets, shooting mechanics",
                "VFX_Landscape_Shaders": "Materials, shaders, Niagara, footprints, snow tracks",
                "HUD_UMG_UI": "Widgets, action bar, dialogue panels, user interface",
                "Cold_Survival": "Cold accumulation, warmth sources, survival rules"
            }
        },
        "implementation_complexity": {
            "type": "score",
            "instructions": "Score implementation complexity from 1 (trivial one-line tweak) to 5 (major multi-file architectural subsystem).",
            "criteria": [
                "Trivial single variable or parameter adjustment",
                "Small localized fix in existing function",
                "New feature within existing component with tests",
                "New gameplay archetype or subsystem",
                "Major architectural engine or multi-domain refactor"
            ]
        }
    }

    answers = query_typesafe(task_description, questions)

    ans_agent = answers.get("assigned_agent", {})
    agent = ans_agent.get("choice") or ans_agent.get("selected", "Claude_Gameplay_CPP")
    agent_conf = ans_agent.get("confidence", 0.0)

    ans_cpp = answers.get("requires_deep_cpp", {})
    cpp_prob = ans_cpp.get("noul") if "noul" in ans_cpp else ans_cpp.get("value", 0.0)

    ans_domain = answers.get("task_domain", {})
    domain = ans_domain.get("choice") or ans_domain.get("selected", "Enemy_AI_Behavior")

    ans_comp = answers.get("implementation_complexity", {})
    complexity = ans_comp.get("score", 3.0)

    print("\n" + "=" * 60)
    print(" 🎯 TYPESAFE (JEV) TRIAGE BRIEF FOR TANDEM")
    print("=" * 60)
    print(f" • Assigned Owner   : {agent} (confidence: {agent_conf:.2f})")
    print(f" • Primary Domain   : {domain}")
    print(f" • C++ Required     : {'YES' if cpp_prob >= 0.5 else 'NO'} (probability: {cpp_prob:.2f})")
    print(f" • Complexity Score : {complexity:.1f} / 5.0")
    print("-" * 60)

    if agent == "Claude_Gameplay_CPP":
        print(" [Handoff Directive]: Route to Claude Code (Opus 5.5). Pre-filtered prompt ready for TANDEM.md.")
    elif agent == "Gemini_Shaders_VFX":
        print(" [Handoff Directive]: Route to Gemini. Save Opus tokens; execute in shaders/scripts.")
    elif agent == "Balance_Data_Config":
        print(" [Handoff Directive]: Tune DA_GameBalanceConfig directly without recompilation.")
    else:
        print(" [Handoff Directive]: Clarify design with user before touching code.")
    print("=" * 60 + "\n")

    return answers

# ------------------------------------------------------------------------------
# Action 2: Audit Git Diff (Проверка границ ответственности тандема)
# ------------------------------------------------------------------------------
# Hard boundaries are rules, so code checks them (Jev 1.13 is literal and may miss a path); Jev rates the regression
# risk of what is about to be committed. Claude 2026-10-04: the audit used `git diff --stat` (unstaged only), so after
# `git add` it audited the other agents' leftovers instead of the commit.
PROTECTED_FOR = {
    "claude": ["Content/VFX/", "Shaders/", "Config/DefaultScalability.ini", "Config/DefaultEditor.ini",
               "Config/DefaultInput.ini", "Content/Maps/L_MovementTest.umap", "Content/Combat_Dog/", "Content/RifleAnims/",
               "Content/Crawl_MocapAnimPack/", "Content/Post_Apo_Survivor/", "Content/Mutant_monster_1/",
               "Content/mutant_monster_2/", "Content/Biochemical_Monster_2/", "Content/Characters/Mannequins/",
               "Content/Input/", "Content/LevelPrototyping/", "Content/ThirdPerson/", "Content/__ExternalActors__/",
               "Content/__ExternalObjects__/", "Content/VigilanteContent/", "CLAUDE.md"],
    "gemini": ["Source/CodexTactics/", "Source/CodexTacticsTests/", "Content/Maps/L_MovementTest.umap"],
}


def git_lines(*args):
    try:
        return [l for l in subprocess.run(["git", *args], capture_output=True, text=True, encoding="utf-8",
                                          errors="replace", check=True).stdout.splitlines() if l.strip()]
    except Exception:
        return []


def cmd_audit_diff(agent_name="claude"):
    staged = git_lines("diff", "--cached", "--name-only")
    scope = "staged (the commit)" if staged else "unstaged"
    files = staged or git_lines("diff", "--name-only")
    print(f"\n[Jev:System1] Auditing {scope} changes for agent '{agent_name}' ({len(files)} files)...")
    if not files:
        print("Clean workspace: No changes to audit.")
        return
    protected = PROTECTED_FOR.get(agent_name.lower(), [])
    breaches = [f for f in files if any(f == p or f.startswith(p) for p in protected)]
    stat = git_lines("diff", "--cached", "--stat") if staged else git_lines("diff", "--stat")

    answers, error = jev_client.ask(
        {"changed_files": files[:60], "diff_stat_tail": stat[-1:] if stat else []},
        {"regression_risk": {
            "type": "score",
            "instructions": "Rate the regression risk of committing the files in `changed_files` to a UE5 game project.",
            "criteria": ["Documentation, tests, tools or data only",
                         "An isolated rule or leaf function with its own tests",
                         "A gameplay subsystem used every frame (AI, combat, movement)",
                         "Game mode, player controller, save game or turn manager plumbing",
                         "Engine-level plumbing or a data schema change"]}})
    risk = jev_client.score(answers, "regression_risk")

    print("\n" + "=" * 60)
    print(" 🛡️ TYPESAFE (JEV) SAFETY & BOUNDARY AUDIT")
    print("=" * 60)
    print(f" • Agent Audited   : {agent_name.upper()} — {scope}")
    print(f" • Boundary Breach : {'⚠️ VIOLATION: ' + ', '.join(breaches) if breaches else '✅ PASS (code check of protected paths)'}")
    print(f" • Risk Rating     : {('%.1f / 4 (Jev)' % risk) if risk is not None else 'n/a (Jev unavailable: %s)' % error}")
    print("-" * 60)
    for line in stat[-12:]:
        print(line)
    print("=" * 60 + "\n")
    if breaches:
        sys.exit(1)

# ------------------------------------------------------------------------------
# Action 3: Telemetry Distillation (Сжатие забегов бота для Claude)
# ------------------------------------------------------------------------------
def bucket(value, edges, words):
    for edge, word in zip(edges, words):
        if value < edge:
            return word
    return words[-1]


def cmd_evaluate_telemetry(file_path, last=20):
    """The last N real run records (runs.jsonl): numbers in code, a worded summary for Jev, a 6-line brief out."""
    runs = []
    if os.path.exists(file_path):
        with open(file_path, encoding="utf-8") as f:
            for line in f:
                try:
                    runs.append(json.loads(line))
                except ValueError:
                    pass
    runs = runs[-last:]
    if not runs:
        print(f"[Jev:Telemetry] No run records in {file_path}.")
        return
    n = len(runs)
    wins = sum(1 for r in runs if r.get("result") == "VICTORY")
    causes, failed_waves = {}, {}
    for r in runs:
        context = r.get("death_context") or {}
        if r.get("result") != "VICTORY":
            cause = context.get("cause", "UNKNOWN")
            causes[cause] = causes.get(cause, 0) + 1
            wave = context.get("failed_wave", 0)
            failed_waves[wave] = failed_waves.get(wave, 0) + 1
    avg_waves = sum(r.get("waves_cleared", 0) for r in runs) / n
    avg_cold = sum(r.get("squad_avg_cold_percent", 0) for r in runs) / n
    levels = sorted({r.get("level_id", "?") for r in runs})
    profiles = sorted({r.get("tester_profile", "?") for r in runs})
    words = [
        f"The squad won {bucket(wins / n, [0.05, 0.3, 0.6, 0.9], ['no run', 'few runs', 'about half the runs', 'most runs', 'every run'])}.",
        "Defeats came from: " + (", ".join(f"{'freezing to death' if c == 'FREEZING_FATIGUE' else 'wounds in combat' if c == 'HP_DEPLETED' else c.lower()} "
                                         f"({bucket(v / n, [0.15, 0.4, 0.7], ['a few runs', 'many runs', 'most runs', 'nearly all runs'])})"
                                         for c, v in causes.items()) or "nothing (no defeats)") + ".",
        "Most defeats came in " + (f"wave {max(failed_waves, key=failed_waves.get)}" if failed_waves else "no wave") + " of 3.",
        f"The squad ended the runs {bucket(avg_cold, [20, 40, 60, 80], ['warm', 'chilled', 'cold', 'very cold', 'freezing'])}.",
    ]
    answers, error = jev_client.ask({"runs": words}, {
        "balance_assessment": {"type": "choice", "instructions": "How does the difficulty described in `runs` feel for a veteran player?",
                               "criteria": {"Critically_Overtuned_Frustrating": "The squad almost always dies",
                                            "Hard_But_Fair": "Defeats are frequent but the squad can win with good play",
                                            "Challenging_Optimal_Tactical": "Tense, the squad wins a little more often than it loses",
                                            "Undertuned_Trivial_Boring": "The squad wins nearly every time"}},
        "primary_fatality_cause": {"type": "choice", "instructions": "What is the main reason the squad loses in `runs`?",
                                   "criteria": {"EnemyPressureInCombat": "Operatives die of wounds in combat",
                                                "ColdSurvivalDepletion": "Operatives freeze to death",
                                                "LateWaveSpike": "One late wave is far harder than the rest",
                                                "NoClearProblem": "The defeats look like normal variance"}},
        "recommended_patch": {"type": "choice", "instructions": "Which single change would best move `runs` towards a hard but fair fight?",
                              "criteria": {"SoftenTheHardestWave": "Fewer or weaker enemies in the wave where most defeats happen",
                                           "EaseColdDrain": "Slower cold accumulation or more heat sources",
                                           "StrongerEnemyPressure": "More or tougher enemies (the fight is too easy)",
                                           "NoChange": "Leave the balance as it is"}}})
    assess, _ = jev_client.choice(answers, "balance_assessment", "n/a")
    cause, _ = jev_client.choice(answers, "primary_fatality_cause", "n/a")
    patch, _ = jev_client.choice(answers, "recommended_patch", "n/a")
    print("\n" + "=" * 60)
    print(" 📊 TYPESAFE (JEV) TELEMETRY DISTILLATION BRIEF")
    print("=" * 60)
    print(f" • Runs              : last {n} ({', '.join(profiles)}; {', '.join(levels)}) — win {wins}/{n}, waves {avg_waves:.2f}/3")
    print(f" • Defeat causes     : {causes or 'none'}; failed waves {failed_waves or 'none'}")
    print(f" • Encounter Health  : {assess}")
    print(f" • Primary Bottleneck: {cause}")
    print(f" • Prescribed Patch  : {patch} (a proposal for the user — balance is theirs)")
    if error:
        print(f" • Jev unavailable   : {error}")
    print("=" * 60)

# ------------------------------------------------------------------------------
# Action 4: Smart Test Selection
# ------------------------------------------------------------------------------
TESTS_DIR = os.path.join(ROOT, "Source", "CodexTacticsTests", "Private")
DEBUG_DIR = os.path.join(ROOT, "Source", "CodexTactics", "Private", "Debug")
TEST_RE = re.compile(r'IMPLEMENT_\w*AUTOMATION_TEST\(\s*\w+\s*,\s*"([^"]+)"')
SMOKE_RE = re.compile(r'TEXT\("CodexTactics\.(\w+Smoke)"\)')
INCLUDE_RE = re.compile(r'#include\s+"([^"]+)"')


def scan(folder, name_re):
    """{file: (names, included headers)} for the .cpp files under folder."""
    out = {}
    for base, _, names in os.walk(folder):
        for name in names:
            if name.endswith(".cpp"):
                path = os.path.join(base, name)
                with open(path, encoding="utf-8", errors="replace") as f:
                    text = f.read()
                out[path] = (name_re.findall(text), {os.path.basename(i) for i in INCLUDE_RE.findall(text)})
    return out


def cmd_smart_test():
    """Tests / smokes that include a changed header (code), Jev only for changed files nothing includes directly."""
    changed = sorted(set(git_lines("diff", "--name-only") + git_lines("diff", "--cached", "--name-only")
                         + git_lines("ls-files", "--others", "--exclude-standard", "Source")))
    sources = [f for f in changed if f.startswith("Source/") and f.endswith((".h", ".cpp"))]
    if not sources:
        print("[Jev:SmartTest] No source changes. Nothing to test.")
        print("FILTER=CodexTactics.Core.Smoke")
        print("SMOKE=None")
        return
    tests, smokes = scan(TESTS_DIR, TEST_RE), scan(DEBUG_DIR, SMOKE_RE)
    filters, smoke_names, unmapped = set(), set(), []
    for source in sources:
        name = os.path.basename(source)
        if source.startswith("Source/CodexTacticsTests/"):
            filters.update(tests.get(os.path.join(ROOT, *source.split("/")), ([], set()))[0])
            continue
        if "/Debug/" in source:
            smoke_names.update(smokes.get(os.path.join(ROOT, *source.split("/")), ([], set()))[0])
            continue
        header = os.path.splitext(name)[0] + ".h"
        hit = [n for names, includes in tests.values() if header in includes for n in names]
        filters.update(hit)
        smoke_names.update(n for names, includes in smokes.values() if header in includes for n in names)
        if not hit:
            unmapped.append(source)
    jev_note = ""
    if unmapped:
        groups = sorted({".".join(n.split(".")[:2]) for names, _ in tests.values() for n in names})
        answers, error = jev_client.ask({"changed_files_without_direct_tests": unmapped[:20]}, {
            "group": {"type": "choice",
                      "instructions": "Which automation test group of a UE5 tactics game best covers the regression risk of "
                                      "`changed_files_without_direct_tests`? Pick CodexTactics for game-wide plumbing.",
                      "criteria": {g: None for g in groups[:254]}}})
        group, confidence = jev_client.choice(answers, "group", "CodexTactics")
        if error or confidence < 0.4:
            group = "CodexTactics"
        filters.add(group)
        jev_note = f"{group} for {len(unmapped)} file(s) without direct tests" + (f" (Jev unavailable: {error})" if error else f" (Jev, conf {confidence:.2f})")
    if "CodexTactics" in filters:
        filters = {"CodexTactics"}
    # A test name is its own filter (substring match); collapse to the shortest prefixes to keep the line short.
    ordered = sorted(filters, key=len)
    collapsed = [f for i, f in enumerate(ordered) if not any(f.startswith(p) for p in ordered[:i])]
    print("\n" + "=" * 60)
    print(" ⚡ TYPESAFE (JEV) SMART TEST SELECTION")
    print("=" * 60)
    print(f" • Changed sources : {len(sources)}; direct tests {len(filters)}; {jev_note or 'all mapped by includes'}")
    print(f" • Smokes          : {', '.join(sorted(smoke_names)) or 'None'}")
    print("-" * 60)
    print("FILTER=" + "+".join(collapsed))
    print("SMOKE=" + (",".join(sorted(smoke_names)) or "None"))
    print("=" * 60 + "\n")

# ------------------------------------------------------------------------------
# Action 5: Adaptive Early-Stop (Ранняя остановка батчей симуляции)
# ------------------------------------------------------------------------------
def cmd_early_stop(file_path, since=None):
    """Code decides (every finished run of this batch lost the same way); Jev is not needed for a count."""
    if not file_path:
        file_path = os.path.join(ROOT, "Saved", "Telemetry", "raw_runs", "runs.jsonl")
    runs = []
    if os.path.exists(file_path):
        with open(file_path, encoding="utf-8") as f:
            for line in f:
                try:
                    runs.append(json.loads(line))
                except ValueError:
                    pass
    if since:
        runs = [r for r in runs if r.get("timestamp_utc", "") >= since[:19]]
    recent = runs[-5:]
    if len(recent) < 3:
        print("[Jev:EarlyStop] Not enough runs of this batch (< 3). Continue.")
        return 0
    causes = {((r.get("death_context") or {}).get("cause") if r.get("result") != "VICTORY" else "VICTORY") for r in recent}
    if len(causes) == 1 and "VICTORY" not in causes:
        print("\n" + "=" * 60)
        print(" 🛑 EARLY-EXIT: the last %d runs of this batch all lost to %s" % (len(recent), causes.pop()))
        print("=" * 60 + "\n")
        sys.exit(2)
    print(f"[Jev:EarlyStop] Mixed outcomes in the last {len(recent)} runs. Continue batch.")
    return 0

def main():
    parser = argparse.ArgumentParser(description="Codex Tactics TypeSafe Jev Triage Bridge")
    parser.add_argument("--triage", type=str, help="Triage incoming task description into agent and domain")
    parser.add_argument("--audit-diff", action="store_true", help="Audit uncommitted git changes against agent boundaries")
    parser.add_argument("--agent", type=str, default="claude", help="Agent name being audited (claude/gemini)")
    parser.add_argument("--telemetry", type=str, help="Distill runs.jsonl balance data into actionable brief")
    parser.add_argument("--smart-test", action="store_true", help="Select only impacted unit tests and smoke commands")
    parser.add_argument("--early-stop", type=str, nargs="?", const="default", help="Check if running batch should terminate early")
    parser.add_argument("--since", type=str, help="Early stop: only run records from this UTC time on (the current batch)")
    parser.add_argument("--last", type=int, default=20, help="Telemetry: how many of the latest run records")

    args = parser.parse_args()

    if args.triage:
        cmd_triage(args.triage)
    elif args.audit_diff:
        cmd_audit_diff(args.agent)
    elif args.telemetry:
        cmd_evaluate_telemetry(args.telemetry, args.last)
    elif args.smart_test:
        cmd_smart_test()
    elif args.early_stop:
        path = None if args.early_stop == "default" else args.early_stop
        cmd_early_stop(path, args.since)
    else:
        parser.print_help()

if __name__ == "__main__":
    main()
