# Codex 110-soviet-1464-relayout (after 107b) - DESIGNER PRIORITY: "finish the Soviet buildings of the generator"

Plan: Gemini 072 - git show 51ef1be2:docs/port/gemini/outbox/072-soviet-relayout.plan.md (designer answers: rooms B, walls B,
section A 26 m + C 22.5 m short variant, storeys A; series order 1-464 first). Scope of THIS task: the 1-464 khrushchevka only,
phases 1-5 of the plan (data + templates -> pure bay planner -> stair core + navmesh -> openings + wall classes -> kit dressing),
on the Story Showcase Kit map (L_Gen_StoryShowcase_Kit). Other series (1-528 / 1-335 / 1-447) are NOT in this task.
Small commits per phase on codex/110-soviet-1464; each phase: its tests + the walk / ramp / cutaway smokes the plan names, on the
worker only. Doors: keep the door data contract stable (Grok G018 finishes door gameplay in parallel - agree in chat if you touch
planDoors.ts). Do not rebuild maps in the main checkout; say exactly which map files the Lead must rebuild / copy.
Post "110 PHASE n | PASS/FAIL | hash" after each phase, then FINISH.
