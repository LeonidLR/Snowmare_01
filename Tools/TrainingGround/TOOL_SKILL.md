---
name: wave-level-editor
description: Standalone visual editor for designing levels, wave progression, and difficulty curves.
stack: React, Vite, TailwindCSS, Lucide, Recharts
contracts:
  input: Content/Data/LevelJson/*.json
  output: Content/Data/LevelJson/*.json
---

# Wave & Level Visual Editor (Tool Guide for AI)

## Назначение инструмента
`tools/editor` — это автономное визуальное веб-приложение для геймдизайнера, позволяющее балансировать волны монстров, тайминги и кривые сложности через интерактивные графики и слайдеры.

## Архитектурные правила
1. Инструмент НЕ зависит от кода движка. С 2026-10-02 он живёт в Unreal-репозитории (Unreal — эталон; копия в Godot-архиве заморожена).
2. Весь обмен данными — через `Content/Data/LevelJson/*.json` (игра читает их в `LevelJsonRules`); телеметрия бота — `Saved/Telemetry/`, пресеты — `Content/Data/Bot/bot_presets.json`.
3. При изменении параметров на графике или слайдерах данные сохраняются строго по схеме `Content/Data/Schemas/level_config.schema.json`.

## Ключевые возможности
* **Интерактивная кривая сложности:** график DPS и плотности врагов по волнам (1..N).
* **Слайдеры скейлинга:** регулировка крутизны кривой, множителей HP и урона.
* **Цветовая диаграмма состава орды:** наглядные доли Гончих, Стрелков, Громил в каждой волне.
* **Мгновенное сохранение / загрузка:** чтение и запись напрямую в JSON-файлы игры.