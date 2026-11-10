#!/usr/bin/env python3
"""PMC Arena Tactical Replay Generator (top-down 2D replay).

Generates a single self-contained HTML page from codex.pmc_arena_track/1 JSON files.
Phone-first responsive design (375 px up to 1280+ px desktop), dark/light theme,
top-down map with building/wall outlines, directional fighter dots with facing ticks,
order tags, shot lines, objective status, interactive scrub timeline with kill markers
and quiet spell indicators.

Usage:
  python Scripts/Tools/arena_replay/build_replay.py <track.json> [...] [--out <dir>]
  python Scripts/Tools/arena_replay/build_replay.py --latest [--out <dir>]
  python Scripts/Tools/arena_replay/build_replay.py --test
"""
from __future__ import annotations

import argparse
import datetime
import glob
import json
import math
import os
import re
import sys
from typing import Any, Dict, List, Optional, Tuple

HTML_TEMPLATE = r"""<!DOCTYPE html>
<html lang="en" data-theme="dark">
<head>
  <meta charset="UTF-8">
  <meta name="viewport" content="width=device-width, initial-scale=1.0, maximum-scale=1.0, user-scalable=no">
  <title>Codex Tactics // PMC Arena Replay</title>
  <style>
    :root {
      --bg: #090d16;
      --surface: #111827;
      --surface-border: #1f293d;
      --surface-hover: #1e293b;
      --text: #f3f4f6;
      --text-muted: #94a3b8;
      --text-dim: #64748b;
      --map-bg: #05070d;
      --grid-line: rgba(255, 255, 255, 0.05);
      --wall-color: #334155;
      --building-fill: #1e293b;
      --building-stroke: #475569;
      --team-a: #ef4444;
      --team-a-bg: rgba(239, 68, 68, 0.15);
      --team-b: #22c55e;
      --team-b-bg: rgba(34, 197, 94, 0.15);
      --obj-neutral: #f59e0b;
      --obj-bg: rgba(245, 158, 11, 0.15);
      --quiet-spell: rgba(245, 158, 11, 0.25);
      --quiet-border: #d97706;
      --accent: #38bdf8;
      --accent-bg: rgba(56, 189, 248, 0.15);
      --kill-mark: #f43f5e;
    }
    [data-theme="light"] {
      --bg: #f8fafc;
      --surface: #ffffff;
      --surface-border: #e2e8f0;
      --surface-hover: #f1f5f9;
      --text: #0f172a;
      --text-muted: #475569;
      --text-dim: #94a3b8;
      --map-bg: #f1f5f9;
      --grid-line: rgba(0, 0, 0, 0.06);
      --wall-color: #94a3b8;
      --building-fill: #e2e8f0;
      --building-stroke: #cbd5e1;
      --team-a: #dc2626;
      --team-a-bg: rgba(220, 38, 38, 0.12);
      --team-b: #16a34a;
      --team-b-bg: rgba(22, 163, 74, 0.12);
      --obj-neutral: #d97706;
      --obj-bg: rgba(217, 119, 6, 0.15);
      --quiet-spell: rgba(217, 119, 6, 0.25);
      --quiet-border: #b45309;
      --accent: #0284c7;
      --accent-bg: rgba(2, 132, 199, 0.12);
      --kill-mark: #e11d48;
    }

    * { box-sizing: border-box; margin: 0; padding: 0; }
    body {
      font-family: -apple-system, BlinkMacSystemFont, "Segoe UI", Roboto, "Helvetica Neue", sans-serif;
      background: var(--bg);
      color: var(--text);
      display: flex;
      flex-direction: column;
      min-height: 100vh;
      -webkit-font-smoothing: antialiased;
      overflow-x: hidden;
    }

    /* Top bar */
    .top-bar {
      display: flex;
      align-items: center;
      justify-content: space-between;
      padding: 8px 14px;
      background: var(--surface);
      border-bottom: 1px solid var(--surface-border);
      gap: 8px;
      flex-wrap: wrap;
    }
    .brand {
      display: flex;
      align-items: center;
      gap: 8px;
    }
    .brand-tag {
      background: var(--accent-bg);
      color: var(--accent);
      font-size: 10px;
      font-weight: 700;
      letter-spacing: 0.8px;
      padding: 2px 6px;
      border-radius: 4px;
      text-transform: uppercase;
    }
    .brand-title {
      font-size: 13px;
      font-weight: 700;
      letter-spacing: 0.5px;
    }
    .top-actions {
      display: flex;
      align-items: center;
      gap: 6px;
    }
    .btn {
      background: var(--surface-hover);
      color: var(--text);
      border: 1px solid var(--surface-border);
      padding: 5px 10px;
      border-radius: 6px;
      font-size: 11px;
      font-weight: 600;
      cursor: pointer;
      display: inline-flex;
      align-items: center;
      gap: 4px;
      user-select: none;
      transition: background 0.15s, border-color 0.15s;
    }
    .btn:hover { background: var(--surface-border); }
    .btn.active {
      background: var(--accent-bg);
      border-color: var(--accent);
      color: var(--accent);
    }

    /* Round Picker Tabs */
    .round-picker {
      display: flex;
      align-items: center;
      gap: 6px;
      padding: 6px 14px;
      background: var(--surface);
      border-bottom: 1px solid var(--surface-border);
      overflow-x: auto;
      scrollbar-width: thin;
    }
    .round-tab {
      padding: 4px 10px;
      border-radius: 4px;
      font-size: 11px;
      font-weight: 600;
      cursor: pointer;
      white-space: nowrap;
      background: var(--surface-hover);
      color: var(--text-muted);
      border: 1px solid transparent;
    }
    .round-tab.active {
      background: var(--accent-bg);
      color: var(--accent);
      border-color: var(--accent);
    }

    /* Result Banner */
    .banner {
      display: flex;
      align-items: center;
      justify-content: space-between;
      padding: 8px 14px;
      background: var(--surface);
      border-bottom: 1px solid var(--surface-border);
      gap: 12px;
      flex-wrap: wrap;
    }
    .banner-result {
      display: flex;
      align-items: center;
      gap: 8px;
    }
    .winner-badge {
      font-size: 12px;
      font-weight: 800;
      padding: 3px 8px;
      border-radius: 4px;
      text-transform: uppercase;
      letter-spacing: 0.5px;
    }
    .winner-a { background: var(--team-a-bg); color: var(--team-a); border: 1px solid var(--team-a); }
    .winner-b { background: var(--team-b-bg); color: var(--team-b); border: 1px solid var(--team-b); }
    .winner-draw { background: var(--surface-hover); color: var(--text-muted); border: 1px solid var(--surface-border); }
    .banner-meta {
      font-size: 11px;
      color: var(--text-muted);
      display: flex;
      gap: 10px;
      flex-wrap: wrap;
    }
    .banner-alive {
      display: flex;
      align-items: center;
      gap: 8px;
      font-size: 11px;
      font-weight: 700;
    }
    .alive-pill {
      display: flex;
      align-items: center;
      gap: 4px;
      padding: 2px 6px;
      border-radius: 4px;
    }
    .alive-pill.team-a { background: var(--team-a-bg); color: var(--team-a); }
    .alive-pill.team-b { background: var(--team-b-bg); color: var(--team-b); }

    /* Main Container (Responsive) */
    .main-container {
      display: flex;
      flex-direction: column;
      flex: 1;
      width: 100%;
      max-width: 1400px;
      margin: 0 auto;
    }
    @media (min-width: 900px) {
      .main-container {
        flex-direction: row;
        height: calc(100vh - 120px);
        min-height: 550px;
      }
    }

    /* Left Map Area */
    .map-pane {
      display: flex;
      flex-direction: column;
      flex: 1;
      position: relative;
      background: var(--map-bg);
      overflow: hidden;
      min-height: 340px;
    }
    @media (min-width: 900px) {
      .map-pane {
        flex: 2;
        border-right: 1px solid var(--surface-border);
      }
    }
    .canvas-wrap {
      flex: 1;
      position: relative;
      width: 100%;
      height: 100%;
      min-height: 280px;
      cursor: crosshair;
      touch-action: none;
    }
    canvas {
      display: block;
      width: 100%;
      height: 100%;
    }
    .map-overlay-tools {
      position: absolute;
      top: 10px;
      right: 10px;
      display: flex;
      flex-direction: column;
      gap: 4px;
      z-index: 10;
    }
    .map-tool-btn {
      width: 30px;
      height: 30px;
      background: rgba(17, 24, 39, 0.85);
      backdrop-filter: blur(4px);
      border: 1px solid var(--surface-border);
      border-radius: 6px;
      color: var(--text);
      font-size: 14px;
      cursor: pointer;
      display: flex;
      align-items: center;
      justify-content: center;
      user-select: none;
    }
    .map-tool-btn:hover { background: var(--surface-border); }

    .map-legend {
      position: absolute;
      bottom: 8px;
      left: 10px;
      background: rgba(17, 24, 39, 0.8);
      backdrop-filter: blur(4px);
      padding: 4px 8px;
      border-radius: 4px;
      border: 1px solid var(--surface-border);
      display: flex;
      align-items: center;
      gap: 12px;
      font-size: 10px;
      color: var(--text-muted);
      pointer-events: none;
      z-index: 5;
    }
    .legend-item {
      display: flex;
      align-items: center;
      gap: 4px;
    }
    .legend-dot {
      width: 8px;
      height: 8px;
      border-radius: 50%;
    }
    .legend-dot.team-a { background: var(--team-a); }
    .legend-dot.team-b { background: var(--team-b); }
    .legend-dot.obj { background: var(--obj-neutral); }

    /* Timeline & Controls Bar */
    .controls-pane {
      background: var(--surface);
      border-top: 1px solid var(--surface-border);
      padding: 8px 14px;
      display: flex;
      flex-direction: column;
      gap: 8px;
      z-index: 20;
    }
    .scrub-container {
      position: relative;
      width: 100%;
      height: 28px;
      display: flex;
      align-items: center;
      cursor: pointer;
      user-select: none;
    }
    .scrub-track {
      position: relative;
      width: 100%;
      height: 8px;
      background: var(--surface-hover);
      border: 1px solid var(--surface-border);
      border-radius: 4px;
      overflow: hidden;
    }
    .scrub-fill {
      position: absolute;
      top: 0;
      left: 0;
      height: 100%;
      width: 0%;
      background: var(--accent);
      border-radius: 3px 0 0 3px;
      pointer-events: none;
    }
    .scrub-quiet-spell {
      position: absolute;
      top: 0;
      height: 100%;
      background: var(--quiet-spell);
      border-left: 1px dashed var(--quiet-border);
      border-right: 1px dashed var(--quiet-border);
      pointer-events: none;
    }
    .scrub-marker {
      position: absolute;
      top: -3px;
      width: 2px;
      height: 14px;
      transform: translateX(-50%);
      pointer-events: none;
    }
    .scrub-marker.kill {
      background: var(--kill-mark);
      z-index: 3;
    }
    .scrub-marker.obj {
      background: var(--obj-neutral);
      width: 3px;
      z-index: 3;
    }
    .scrub-thumb {
      position: absolute;
      top: 50%;
      width: 14px;
      height: 14px;
      background: var(--text);
      border: 2px solid var(--accent);
      border-radius: 50%;
      transform: translate(-50%, -50%);
      pointer-events: none;
      box-shadow: 0 1px 4px rgba(0, 0, 0, 0.4);
      z-index: 4;
    }

    .playback-bar {
      display: flex;
      align-items: center;
      justify-content: space-between;
      gap: 8px;
      flex-wrap: wrap;
    }
    .play-btns {
      display: flex;
      align-items: center;
      gap: 4px;
    }
    .time-readout {
      font-family: monospace;
      font-size: 12px;
      font-weight: 700;
      color: var(--text);
      min-width: 100px;
    }
    .speed-btns {
      display: flex;
      align-items: center;
      gap: 2px;
    }
    .speed-btn {
      padding: 3px 6px;
      font-size: 10px;
      font-weight: 700;
      background: var(--surface-hover);
      border: 1px solid var(--surface-border);
      border-radius: 4px;
      cursor: pointer;
      color: var(--text-muted);
    }
    .speed-btn.active {
      background: var(--accent);
      color: #000;
      border-color: var(--accent);
    }

    /* Right Sidebar (Roster & Event Log) */
    .sidebar-pane {
      background: var(--surface);
      display: flex;
      flex-direction: column;
      overflow-y: auto;
      min-height: 250px;
    }
    @media (min-width: 900px) {
      .sidebar-pane {
        flex: 1;
        max-width: 420px;
      }
    }
    .sidebar-section {
      padding: 10px 14px;
      border-bottom: 1px solid var(--surface-border);
    }
    .sidebar-title {
      font-size: 11px;
      font-weight: 700;
      color: var(--text-muted);
      text-transform: uppercase;
      letter-spacing: 0.5px;
      margin-bottom: 8px;
      display: flex;
      justify-content: space-between;
      align-items: center;
    }

    /* Fighter Card in Dossier */
    .dossier-card {
      background: var(--surface-hover);
      border: 1px solid var(--surface-border);
      border-radius: 6px;
      padding: 8px 10px;
      margin-bottom: 6px;
      cursor: pointer;
      transition: border-color 0.15s, background 0.15s;
    }
    .dossier-card:hover { border-color: var(--text-dim); }
    .dossier-card.selected {
      border-color: var(--accent);
      background: var(--accent-bg);
    }
    .dossier-card.down {
      opacity: 0.55;
    }
    .card-top {
      display: flex;
      align-items: center;
      justify-content: space-between;
      font-size: 12px;
      font-weight: 700;
      margin-bottom: 4px;
    }
    .card-name {
      display: flex;
      align-items: center;
      gap: 6px;
    }
    .card-tag {
      font-size: 10px;
      font-weight: 600;
      padding: 1px 5px;
      border-radius: 3px;
      text-transform: uppercase;
    }
    .tag-a { background: var(--team-a-bg); color: var(--team-a); }
    .tag-b { background: var(--team-b-bg); color: var(--team-b); }
    .card-hp-bar {
      width: 100%;
      height: 4px;
      background: var(--surface-border);
      border-radius: 2px;
      overflow: hidden;
      margin-bottom: 4px;
    }
    .card-hp-fill {
      height: 100%;
      background: var(--accent);
      transition: width 0.1s linear;
    }
    .card-details {
      display: flex;
      align-items: center;
      justify-content: space-between;
      font-size: 10px;
      color: var(--text-muted);
    }
    .order-badge {
      display: inline-block;
      padding: 1px 4px;
      border-radius: 3px;
      background: rgba(255, 255, 255, 0.08);
      font-weight: 600;
      color: var(--text);
    }

    /* Event Log List */
    .event-log {
      max-height: 220px;
      overflow-y: auto;
      font-size: 11px;
      display: flex;
      flex-direction: column;
      gap: 4px;
    }
    .event-row {
      display: flex;
      align-items: center;
      justify-content: space-between;
      padding: 4px 6px;
      border-radius: 4px;
      background: var(--surface-hover);
      cursor: pointer;
    }
    .event-row:hover { background: var(--surface-border); }
    .event-time { font-family: monospace; font-size: 10px; color: var(--text-dim); }
    .event-text { flex: 1; margin: 0 6px; }
    .event-icon { font-size: 11px; }

    /* Quiet spell warning pill */
    .quiet-pill {
      font-size: 10px;
      color: var(--obj-neutral);
      background: var(--obj-bg);
      border: 1px solid var(--obj-neutral);
      padding: 2px 6px;
      border-radius: 4px;
      font-weight: 600;
    }
  </style>
</head>
<body>

  <!-- Top Bar -->
  <header class="top-bar">
    <div class="brand">
      <span class="brand-tag">PMC Arena</span>
      <span class="brand-title" id="pageTitle">Replay</span>
    </div>
    <div class="top-actions">
      <button class="btn" id="btnToggleTags">🏷️ Orders: ON</button>
      <button class="btn" id="btnToggleTheme">☀️ / 🌙</button>
    </div>
  </header>

  <!-- Round Picker (tabs if multiple rounds) -->
  <div class="round-picker" id="roundPicker" style="display:none;"></div>

  <!-- Round Result Banner -->
  <div class="banner">
    <div class="banner-result">
      <span class="winner-badge" id="winnerBadge">VICTORY</span>
      <div class="banner-meta">
        <span id="bannerReason">Elimination</span>
        <span id="bannerDuration">0.0s</span>
        <span id="bannerShots">0 shots</span>
      </div>
    </div>
    <div class="banner-alive">
      <div class="alive-pill team-a" id="aliveTeamA">A: 3/3</div>
      <div class="alive-pill team-b" id="aliveTeamB">B: 3/3</div>
    </div>
  </div>

  <!-- Main Viewport Layout -->
  <main class="main-container">

    <!-- Map Viewport -->
    <section class="map-pane">
      <div class="canvas-wrap" id="canvasWrap">
        <canvas id="arenaCanvas"></canvas>
      </div>

      <!-- Map overlay tool buttons -->
      <div class="map-overlay-tools">
        <button class="map-tool-btn" id="btnZoomIn" title="Zoom in">+</button>
        <button class="map-tool-btn" id="btnZoomOut" title="Zoom out">−</button>
        <button class="map-tool-btn" id="btnResetView" title="Reset View">⟲</button>
      </div>

      <!-- Map Legend -->
      <div class="map-legend">
        <div class="legend-item"><div class="legend-dot team-a"></div>Team A</div>
        <div class="legend-item"><div class="legend-dot team-b"></div>Team B</div>
        <div class="legend-item"><div class="legend-dot obj"></div>Objective</div>
        <div class="legend-item"><span>✕</span> KIA</div>
      </div>

      <!-- Timeline & Playback Bar -->
      <div class="controls-pane">
        <!-- Scrubber bar -->
        <div class="scrub-container" id="scrubContainer">
          <div class="scrub-track" id="scrubTrack">
            <!-- Quiet spells and markers injected dynamically -->
            <div class="scrub-fill" id="scrubFill"></div>
            <div class="scrub-thumb" id="scrubThumb"></div>
          </div>
        </div>

        <!-- Playback Controls -->
        <div class="playback-bar">
          <div class="play-btns">
            <button class="btn" id="btnPlayPause">▶ Play</button>
            <button class="btn" id="btnStepBack" title="Step -0.5s">◀</button>
            <button class="btn" id="btnStepFwd" title="Step +0.5s">▶</button>
            <button class="btn" id="btnRestart" title="Rewind">⏮</button>
          </div>
          <div class="time-readout" id="timeReadout">00:00.0 / 00:00.0</div>
          <div class="speed-btns">
            <button class="speed-btn active" data-speed="1">1x</button>
            <button class="speed-btn" data-speed="4">4x</button>
            <button class="speed-btn" data-speed="16">16x</button>
          </div>
        </div>
      </div>
    </section>

    <!-- Sidebar: Fighter Dossier & Combat Log -->
    <aside class="sidebar-pane">
      <!-- Team A Roster -->
      <div class="sidebar-section">
        <div class="sidebar-title">
          <span style="color:var(--team-a)">Team A (Attacker)</span>
          <span id="countTeamA">3/3 Alive</span>
        </div>
        <div id="rosterTeamA"></div>
      </div>

      <!-- Team B Roster -->
      <div class="sidebar-section">
        <div class="sidebar-title">
          <span style="color:var(--team-b)">Team B (Defender)</span>
          <span id="countTeamB">3/3 Alive</span>
        </div>
        <div id="rosterTeamB"></div>
      </div>

      <!-- Combat Event Log -->
      <div class="sidebar-section" style="flex:1;">
        <div class="sidebar-title">
          <span>Combat Event Log</span>
          <span id="eventCountBadge">0 events</span>
        </div>
        <div class="event-log" id="eventLogList"></div>
      </div>
    </aside>

  </main>

  <!-- Replay Data inlined by Python builder -->
  <script id="replayData" type="application/json">
__REPLAY_DATA_JSON__
  </script>

  <script>
    (function() {
      // Load bundled data
      const dataPayload = JSON.parse(document.getElementById('replayData').textContent);
      const rounds = dataPayload.rounds || [];
      let currentRoundIdx = 0;
      let round = rounds[0] || null;

      // Playback State
      let isPlaying = false;
      let currentTime = 0.0;
      let playbackSpeed = 1.0;
      let showOrderTags = true;
      let selectedFighterIdx = null;
      let lastAnimTimestamp = null;

      // Camera / View State
      let viewZoom = 1.0;
      let viewPanX = 0;
      let viewPanY = 0;
      let isDragging = false;
      let dragStartX = 0;
      let dragStartY = 0;

      // Touch gesture support
      let touchStartX = 0;
      let touchStartY = 0;
      let initialPinchDist = null;

      // DOM Elements
      const canvas = document.getElementById('arenaCanvas');
      const ctx = canvas.getContext('2d');
      const canvasWrap = document.getElementById('canvasWrap');
      const btnPlayPause = document.getElementById('btnPlayPause');
      const btnStepBack = document.getElementById('btnStepBack');
      const btnStepFwd = document.getElementById('btnStepFwd');
      const btnRestart = document.getElementById('btnRestart');
      const timeReadout = document.getElementById('timeReadout');
      const scrubContainer = document.getElementById('scrubContainer');
      const scrubTrack = document.getElementById('scrubTrack');
      const scrubFill = document.getElementById('scrubFill');
      const scrubThumb = document.getElementById('scrubThumb');
      const btnToggleTags = document.getElementById('btnToggleTags');
      const btnToggleTheme = document.getElementById('btnToggleTheme');
      const roundPicker = document.getElementById('roundPicker');
      const winnerBadge = document.getElementById('winnerBadge');
      const bannerReason = document.getElementById('bannerReason');
      const bannerDuration = document.getElementById('bannerDuration');
      const bannerShots = document.getElementById('bannerShots');
      const aliveTeamA = document.getElementById('aliveTeamA');
      const aliveTeamB = document.getElementById('aliveTeamB');
      const rosterTeamA = document.getElementById('rosterTeamA');
      const rosterTeamB = document.getElementById('rosterTeamB');
      const countTeamA = document.getElementById('countTeamA');
      const countTeamB = document.getElementById('countTeamB');
      const eventLogList = document.getElementById('eventLogList');
      const eventCountBadge = document.getElementById('eventCountBadge');
      const pageTitle = document.getElementById('pageTitle');

      // Theme toggle
      btnToggleTheme.addEventListener('click', () => {
        const doc = document.documentElement;
        const cur = doc.getAttribute('data-theme') || 'dark';
        doc.setAttribute('data-theme', cur === 'dark' ? 'light' : 'dark');
        render();
      });

      // Tags toggle
      btnToggleTags.addEventListener('click', () => {
        showOrderTags = !showOrderTags;
        btnToggleTags.textContent = showOrderTags ? '🏷️ Orders: ON' : '🏷️ Orders: OFF';
        btnToggleTags.classList.toggle('active', showOrderTags);
        render();
      });

      // Setup Round Picker
      if (rounds.length > 1) {
        roundPicker.style.display = 'flex';
        roundPicker.innerHTML = '';
        rounds.forEach((r, idx) => {
          const tab = document.createElement('div');
          tab.className = 'round-tab' + (idx === 0 ? ' active' : '');
          tab.textContent = `Round ${r.round || idx + 1} (${r.winner ? 'Won ' + r.winner : 'Draw'}, ${r.duration_s.toFixed(1)}s)`;
          tab.addEventListener('click', () => switchRound(idx));
          roundPicker.appendChild(tab);
        });
      } else {
        roundPicker.style.display = 'none';
      }

      function switchRound(idx) {
        currentRoundIdx = idx;
        round = rounds[idx];
        currentTime = 0.0;
        selectedFighterIdx = null;
        document.querySelectorAll('.round-tab').forEach((el, i) => {
          el.classList.toggle('active', i === idx);
        });
        resetCamera();
        setupScrubberMarkers();
        updateBanner();
        initRosterDOM();
        buildEventLog();
        render();
      }

      function updateBanner() {
        if (!round) return;
        pageTitle.textContent = `${dataPayload.run_id || 'Arena'} // ${round.map || 'Combat Lab'}`;
        const w = round.winner;
        if (w === 'A') {
          winnerBadge.textContent = 'TEAM A VICTORY';
          winnerBadge.className = 'winner-badge winner-a';
        } else if (w === 'B') {
          winnerBadge.textContent = 'TEAM B VICTORY';
          winnerBadge.className = 'winner-badge winner-b';
        } else {
          winnerBadge.textContent = 'DRAW / TIMEOUT';
          winnerBadge.className = 'winner-badge winner-draw';
        }
        bannerReason.textContent = round.end_reason ? `End: ${round.end_reason}` : (round.scenario || 'Skirmish');
        bannerDuration.textContent = `${round.duration_s.toFixed(1)}s`;
        bannerShots.textContent = `${round.shots ? round.shots.length : 0} shots`;
      }

      function setupScrubberMarkers() {
        if (!round) return;
        // Clear previous markers
        document.querySelectorAll('.scrub-quiet-spell, .scrub-marker').forEach(el => el.remove());
        const dur = Math.max(0.1, round.duration_s);

        // Quiet spells
        (round.quiet_spells || []).forEach(sp => {
          const leftPct = (sp[0] / dur) * 100;
          const widthPct = ((sp[1] - sp[0]) / dur) * 100;
          const el = document.createElement('div');
          el.className = 'scrub-quiet-spell';
          el.style.left = `${leftPct}%`;
          el.style.width = `${widthPct}%`;
          el.title = `Quiet spell: ${(sp[1] - sp[0]).toFixed(1)}s without shots`;
          scrubTrack.appendChild(el);
        });

        // Kill markers
        (round.kills || []).forEach(k => {
          const leftPct = (k.t / dur) * 100;
          const el = document.createElement('div');
          el.className = 'scrub-marker kill';
          el.style.left = `${leftPct}%`;
          el.title = `Kill at ${k.t.toFixed(1)}s: ${k.victim} by ${k.by || 'unknown'}`;
          scrubTrack.appendChild(el);
        });

        // Objective events
        (round.objective_events || []).forEach(oe => {
          const leftPct = (oe.t / dur) * 100;
          const el = document.createElement('div');
          el.className = 'scrub-marker obj';
          el.style.left = `${leftPct}%`;
          el.title = `Objective ${oe.event} at ${oe.t.toFixed(1)}s by Team ${oe.side}`;
          scrubTrack.appendChild(el);
        });
      }

      function buildEventLog() {
        if (!round) return;
        eventLogList.innerHTML = '';
        const allEvts = [];
        (round.kills || []).forEach(k => allEvts.push({ t: k.t, type: 'kill', icon: '💀', text: `${k.victim} eliminated by ${k.by || 'enemy'}` }));
        (round.objective_events || []).forEach(o => allEvts.push({ t: o.t, type: 'obj', icon: '🚩', text: `Objective ${o.event} by Team ${o.side}` }));
        (round.heals || []).forEach(h => allEvts.push({ t: h.t, type: 'heal', icon: '🩹', text: `Team ${h.side} medical treatment` }));
        (round.tactics || []).slice(0, 40).forEach(tc => allEvts.push({ t: tc.t, type: 'tactic', icon: '📋', text: `${tc.who}: order ${tc.order || tc.decision}` }));

        allEvts.sort((a, b) => a.t - b.t);
        eventCountBadge.textContent = `${allEvts.length} events`;

        allEvts.forEach(ev => {
          const row = document.createElement('div');
          row.className = 'event-row';

          const timeSpan = document.createElement('span');
          timeSpan.className = 'event-time';
          timeSpan.textContent = `${Number(ev.t || 0).toFixed(1)}s`;

          const iconSpan = document.createElement('span');
          iconSpan.className = 'event-icon';
          iconSpan.textContent = ev.icon || '';

          const textSpan = document.createElement('span');
          textSpan.className = 'event-text';
          textSpan.textContent = ev.text || '';

          row.appendChild(timeSpan);
          row.appendChild(iconSpan);
          row.appendChild(textSpan);

          row.addEventListener('click', () => {
            currentTime = ev.t;
            render();
          });
          eventLogList.appendChild(row);
        });
      }

      function resetCamera() {
        viewZoom = 1.0;
        viewPanX = 0;
        viewPanY = 0;
      }

      // Resize canvas to match display container with HiDPI support
      function resizeCanvas() {
        const rect = canvasWrap.getBoundingClientRect();
        const dpr = window.devicePixelRatio || 1;
        canvas.width = Math.max(300, Math.floor(rect.width * dpr));
        canvas.height = Math.max(280, Math.floor(rect.height * dpr));
        ctx.setTransform(1, 0, 0, 1, 0, 0);
        ctx.scale(dpr, dpr);
      }

      window.addEventListener('resize', () => {
        resizeCanvas();
        render();
      });

      // Controls event listeners
      btnPlayPause.addEventListener('click', () => {
        isPlaying = !isPlaying;
        btnPlayPause.textContent = isPlaying ? '⏸ Pause' : '▶ Play';
        btnPlayPause.classList.toggle('active', isPlaying);
        if (isPlaying) {
          lastAnimTimestamp = performance.now();
          requestAnimationFrame(animationLoop);
        }
      });

      btnStepBack.addEventListener('click', () => {
        currentTime = Math.max(0, currentTime - 0.5);
        render();
      });

      btnStepFwd.addEventListener('click', () => {
        if (!round) return;
        currentTime = Math.min(round.duration_s, currentTime + 0.5);
        render();
      });

      btnRestart.addEventListener('click', () => {
        currentTime = 0.0;
        render();
      });

      document.querySelectorAll('.speed-btn').forEach(btn => {
        btn.addEventListener('click', () => {
          document.querySelectorAll('.speed-btn').forEach(b => b.classList.remove('active'));
          btn.classList.add('active');
          playbackSpeed = parseFloat(btn.getAttribute('data-speed')) || 1.0;
        });
      });

      // Scrub bar interaction
      function handleScrub(e) {
        if (!round) return;
        const rect = scrubTrack.getBoundingClientRect();
        const clientX = e.touches ? e.touches[0].clientX : e.clientX;
        const frac = Math.max(0, Math.min(1, (clientX - rect.left) / rect.width));
        currentTime = frac * round.duration_s;
        render();
      }

      let isScrubbing = false;
      scrubContainer.addEventListener('mousedown', (e) => { isScrubbing = true; handleScrub(e); });
      scrubContainer.addEventListener('touchstart', (e) => { isScrubbing = true; handleScrub(e); }, { passive: true });
      window.addEventListener('mousemove', (e) => { if (isScrubbing) handleScrub(e); });
      window.addEventListener('touchmove', (e) => { if (isScrubbing) handleScrub(e); }, { passive: true });
      window.addEventListener('mouseup', () => { isScrubbing = false; });
      window.addEventListener('touchend', () => { isScrubbing = false; });

      // Pan & Zoom controls
      document.getElementById('btnZoomIn').addEventListener('click', () => { viewZoom = Math.min(6.0, viewZoom * 1.3); render(); });
      document.getElementById('btnZoomOut').addEventListener('click', () => { viewZoom = Math.max(0.4, viewZoom / 1.3); render(); });
      document.getElementById('btnResetView').addEventListener('click', () => { resetCamera(); render(); });

      // Mouse drag pan
      canvasWrap.addEventListener('mousedown', (e) => {
        isDragging = true;
        dragStartX = e.clientX - viewPanX;
        dragStartY = e.clientY - viewPanY;
      });
      window.addEventListener('mousemove', (e) => {
        if (!isDragging) return;
        viewPanX = e.clientX - dragStartX;
        viewPanY = e.clientY - dragStartY;
        render();
      });
      window.addEventListener('mouseup', () => { isDragging = false; });

      // Touch gestures (1-finger pan, 2-finger pinch zoom)
      canvasWrap.addEventListener('touchstart', (e) => {
        if (e.touches.length === 1) {
          isDragging = true;
          touchStartX = e.touches[0].clientX - viewPanX;
          touchStartY = e.touches[0].clientY - viewPanY;
        } else if (e.touches.length === 2) {
          isDragging = false;
          initialPinchDist = Math.hypot(
            e.touches[0].clientX - e.touches[1].clientX,
            e.touches[0].clientY - e.touches[1].clientY
          );
        }
      }, { passive: true });

      canvasWrap.addEventListener('touchmove', (e) => {
        if (e.touches.length === 1 && isDragging) {
          viewPanX = e.touches[0].clientX - touchStartX;
          viewPanY = e.touches[0].clientY - touchStartY;
          render();
        } else if (e.touches.length === 2 && initialPinchDist) {
          const dist = Math.hypot(
            e.touches[0].clientX - e.touches[1].clientX,
            e.touches[0].clientY - e.touches[1].clientY
          );
          if (dist > 0 && initialPinchDist > 0) {
            const factor = dist / initialPinchDist;
            viewZoom = Math.max(0.35, Math.min(8.0, viewZoom * factor));
            initialPinchDist = dist;
            render();
          }
        }
      }, { passive: true });

      canvasWrap.addEventListener('touchend', () => {
        isDragging = false;
        initialPinchDist = null;
      });

      // Mouse wheel zoom
      canvasWrap.addEventListener('wheel', (e) => {
        e.preventDefault();
        const factor = e.deltaY < 0 ? 1.15 : 0.85;
        viewZoom = Math.max(0.3, Math.min(8.0, viewZoom * factor));
        render();
      }, { passive: false });

      // Click to select fighter
      canvasWrap.addEventListener('click', (e) => {
        if (!round) return;
        const rect = canvasWrap.getBoundingClientRect();
        const clickX = e.clientX - rect.left;
        const clickY = e.clientY - rect.top;

        const bounds = getBounds();
        const tPos = worldToScreenTransform(bounds, rect.width, rect.height);
        const frameIdx = getFrameIndex();
        const frame = round.frames[frameIdx] || [];

        let closestIdx = null;
        let closestDist = 26; // 26px tap tolerance

        for (let i = 0; i < round.fighters.length; i++) {
          if (!frame[i]) continue;
          const [sx, sy] = tPos(frame[i][0], frame[i][1]);
          const d = Math.hypot(clickX - sx, clickY - sy);
          if (d < closestDist) {
            closestDist = d;
            closestIdx = i;
          }
        }
        selectedFighterIdx = closestIdx;
        render();
      });

      // Keyboard shortcuts
      window.addEventListener('keydown', (e) => {
        if (e.target && ['INPUT', 'TEXTAREA'].includes(e.target.tagName)) return;
        if (e.code === 'Space') {
          e.preventDefault();
          btnPlayPause.click();
        } else if (e.code === 'ArrowLeft') {
          e.preventDefault();
          btnStepBack.click();
        } else if (e.code === 'ArrowRight') {
          e.preventDefault();
          btnStepFwd.click();
        }
      });

      function animationLoop(timestamp) {
        if (!isPlaying || !round) return;
        const delta = (timestamp - lastAnimTimestamp) / 1000;
        lastAnimTimestamp = timestamp;

        currentTime += delta * playbackSpeed;
        if (currentTime >= round.duration_s) {
          currentTime = round.duration_s;
          isPlaying = false;
          btnPlayPause.textContent = '▶ Play';
          btnPlayPause.classList.remove('active');
        }
        render();
        if (isPlaying) {
          requestAnimationFrame(animationLoop);
        }
      }

      function getFrameIndex() {
        if (!round || !round.frames.length) return 0;
        const dt = round.dt || 0.2;
        const idx = Math.floor(currentTime / dt);
        return Math.max(0, Math.min(round.frames.length - 1, idx));
      }

      function getBounds() {
        if (round.map_layout && round.map_layout.bounds) {
          const b = round.map_layout.bounds;
          return { minX: b.min[0], minY: b.min[1], maxX: b.max[0], maxY: b.max[1] };
        }
        if (round.bounds) {
          return { minX: round.bounds[0], minY: round.bounds[1], maxX: round.bounds[2], maxY: round.bounds[3] };
        }
        return { minX: -6000, minY: -5000, maxX: -2000, maxY: -4000 };
      }

      function worldToScreenTransform(bounds, viewW, viewH) {
        const pad = 24;
        const worldW = Math.max(500, bounds.maxX - bounds.minX);
        const worldH = Math.max(500, bounds.maxY - bounds.minY);

        const baseScale = Math.min((viewW - pad * 2) / worldW, (viewH - pad * 2) / worldH);
        const scale = baseScale * viewZoom;

        const centerX = viewW / 2 + viewPanX;
        const centerY = viewH / 2 + viewPanY;
        const worldMidX = (bounds.minX + bounds.maxX) / 2;
        const worldMidY = (bounds.minY + bounds.maxY) / 2;

        return function(wx, wy) {
          const sx = centerX + (wx - worldMidX) * scale;
          const sy = centerY + (wy - worldMidY) * scale;
          return [sx, sy, scale];
        };
      }

      function formatTime(s) {
        const m = Math.floor(s / 60);
        const sec = (s % 60).toFixed(1);
        return `${m < 10 ? '0' : ''}${m}:${sec < 10 ? '0' : ''}${sec}`;
      }

      function render() {
        if (!round) return;

        const rect = canvasWrap.getBoundingClientRect();
        const dpr = window.devicePixelRatio || 1;
        const viewW = rect.width;
        const viewH = rect.height;

        ctx.clearRect(0, 0, viewW, viewH);

        const bounds = getBounds();
        const tPos = worldToScreenTransform(bounds, viewW, viewH);
        const sample = tPos(0, 0);
        const scale = sample[2];

        // 1. Draw Grid
        drawGrid(ctx, bounds, tPos, scale, viewW, viewH);

        // 2. Draw Level Geometry (buildings & walls)
        if (round.map_layout) {
          drawMapLayout(ctx, round.map_layout, tPos, scale);
        }

        // 3. Draw Objective
        if (round.objective) {
          drawObjective(ctx, round.objective, tPos, scale);
        }

        // 4. Draw Active Shot Lines
        drawShots(ctx, tPos);

        // 5. Draw Fighters
        const frameIdx = getFrameIndex();
        const frame = (round.frames && round.frames[frameIdx]) ? round.frames[frameIdx] : [];
        const stateRow = (round.states && round.states[frameIdx]) ? round.states[frameIdx] : [];

        let aliveA = 0, aliveB = 0;
        let totalHpA = 0, totalHpB = 0;

        (round.fighters || []).forEach((f, i) => {
          const pos = (frame && i < frame.length) ? frame[i] : null;
          const st = (stateRow && i < stateRow.length) ? stateRow[i] : null;
          const rawHp = (st && st[2] !== null && st[2] !== undefined) ? Number(st[2]) : 0;
          const isDown = !st || rawHp <= 0;
          const side = f.side || (i < 3 ? 'A' : 'B');

          if (!isDown) {
            if (side === 'A') { aliveA++; totalHpA += rawHp; }
            else { aliveB++; totalHpB += rawHp; }
          }

          if (pos && pos.length >= 2 && pos[0] !== null && pos[1] !== null) {
            drawFighter(ctx, i, f, pos, st, isDown, tPos, scale);
          }
        });

        // 6. Update UI Readouts
        updateUI(aliveA, aliveB, totalHpA, totalHpB, stateRow);
      }

      function drawGrid(ctx, bounds, tPos, scale, viewW, viewH) {
        const theme = document.documentElement.getAttribute('data-theme') || 'dark';
        ctx.strokeStyle = theme === 'dark' ? 'rgba(255,255,255,0.05)' : 'rgba(0,0,0,0.06)';
        ctx.lineWidth = 1;

        // 10-meter grid (1000 cm)
        const step = 1000;
        const startX = Math.floor(bounds.minX / step) * step;
        const endX = Math.ceil(bounds.maxX / step) * step;
        const startY = Math.floor(bounds.minY / step) * step;
        const endY = Math.ceil(bounds.maxY / step) * step;

        ctx.beginPath();
        for (let x = startX; x <= endX; x += step) {
          const [sx1, sy1] = tPos(x, startY);
          const [sx2, sy2] = tPos(x, endY);
          ctx.moveTo(sx1, sy1);
          ctx.lineTo(sx2, sy2);
        }
        for (let y = startY; y <= endY; y += step) {
          const [sx1, sy1] = tPos(startX, y);
          const [sx2, sy2] = tPos(endX, y);
          ctx.moveTo(sx1, sy1);
          ctx.lineTo(sx2, sy2);
        }
        ctx.stroke();
      }

      function drawMapLayout(ctx, layout, tPos, scale) {
        const theme = document.documentElement.getAttribute('data-theme') || 'dark';
        const buildFill = theme === 'dark' ? '#1e293b' : '#e2e8f0';
        const buildStroke = theme === 'dark' ? '#475569' : '#cbd5e1';
        const wallColor = theme === 'dark' ? '#64748b' : '#94a3b8';

        // Buildings
        (layout.buildings || []).forEach(b => {
          if (!b.footprint || b.footprint.length < 2) return;
          const [p1, p2] = b.footprint;
          const [sx1, sy1] = tPos(Math.min(p1[0], p2[0]), Math.min(p1[1], p2[1]));
          const [sx2, sy2] = tPos(Math.max(p1[0], p2[0]), Math.max(p1[1], p2[1]));
          const w = sx2 - sx1;
          const h = sy2 - sy1;
          ctx.fillStyle = buildFill;
          ctx.fillRect(sx1, sy1, w, h);
          ctx.strokeStyle = buildStroke;
          ctx.lineWidth = 1.5;
          ctx.strokeRect(sx1, sy1, w, h);
        });

        // Walls
        (layout.walls || []).forEach(w => {
          if (!w.a || !w.b) return;
          const [sx1, sy1] = tPos(w.a[0], w.a[1]);
          const [sx2, sy2] = tPos(w.b[0], w.b[1]);
          ctx.strokeStyle = wallColor;
          ctx.lineWidth = Math.max(2, (w.thickness || 100) * scale);
          ctx.beginPath();
          ctx.moveTo(sx1, sy1);
          ctx.lineTo(sx2, sy2);
          ctx.stroke();
        });
      }

      function drawObjective(ctx, obj, tPos, scale) {
        const [ox, oy] = [obj.x || 0, obj.y || 0];
        const [sx, sy] = tPos(ox, oy);
        const radiusCm = obj.radius || 600;
        const rScreen = Math.max(8, radiusCm * scale);

        // Determine current controller at currentTime
        let currentController = null;
        (round.objective_events || []).forEach(oe => {
          if (currentTime >= oe.t) {
            currentController = oe.side;
          }
        });

        const colorA = '#ef4444';
        const colorB = '#22c55e';
        const colorNeutral = '#f59e0b';
        let strokeColor = colorNeutral;
        let fillColor = 'rgba(245, 158, 11, 0.15)';

        if (currentController === 'A') {
          strokeColor = colorA;
          fillColor = 'rgba(239, 68, 68, 0.2)';
        } else if (currentController === 'B') {
          strokeColor = colorB;
          fillColor = 'rgba(34, 197, 94, 0.2)';
        }

        // Fill area
        ctx.beginPath();
        ctx.arc(sx, sy, rScreen, 0, Math.PI * 2);
        ctx.fillStyle = fillColor;
        ctx.fill();

        // Dashed boundary
        ctx.lineWidth = 2;
        ctx.strokeStyle = strokeColor;
        ctx.setLineDash([4, 4]);
        ctx.beginPath();
        ctx.arc(sx, sy, rScreen, 0, Math.PI * 2);
        ctx.stroke();
        ctx.setLineDash([]);

        // Center marker
        ctx.beginPath();
        ctx.arc(sx, sy, 3, 0, Math.PI * 2);
        ctx.fillStyle = strokeColor;
        ctx.fill();

        // Label
        ctx.font = 'bold 9px sans-serif';
        ctx.fillStyle = strokeColor;
        ctx.textAlign = 'center';
        const statusText = currentController ? `OBJ (Team ${currentController})` : `OBJ [${obj.attacker ? 'Atk: ' + obj.attacker : 'Point'}]`;
        ctx.fillText(statusText, sx, sy - rScreen - 4);
      }

      function drawShots(ctx, tPos) {
        if (!round || !round.shots) return;
        const windowS = 0.45; // Shot line visible for ~0.45s

        round.shots.forEach(s => {
          if (currentTime >= s.t && currentTime <= s.t + windowS) {
            const age = currentTime - s.t;
            const alpha = Math.max(0, 1.0 - (age / windowS));
            const isHit = s.hit === true;
            const side = s.side || 'A';

            let color = side === 'A' ? `rgba(239, 68, 68, ${alpha})` : `rgba(34, 197, 94, ${alpha})`;
            if (isHit) {
              color = side === 'A' ? `rgba(255, 100, 100, ${alpha})` : `rgba(100, 255, 150, ${alpha})`;
            }

            const [x1, y1] = s.from_pos || [0, 0];
            const [x2, y2] = s.to_pos || [x1 + 100, y1];

            const [sx1, sy1] = tPos(x1, y1);
            const [sx2, sy2] = tPos(x2, y2);

            ctx.strokeStyle = color;
            ctx.lineWidth = isHit ? 2.5 : 1.5;
            ctx.beginPath();
            ctx.moveTo(sx1, sy1);
            ctx.lineTo(sx2, sy2);
            ctx.stroke();

            // Impact burst
            ctx.fillStyle = isHit ? `rgba(251, 191, 36, ${alpha})` : `rgba(148, 163, 184, ${alpha * 0.7})`;
            ctx.beginPath();
            ctx.arc(sx2, sy2, isHit ? 4 : 2, 0, Math.PI * 2);
            ctx.fill();
          }
        });
      }

      function drawFighter(ctx, idx, fighter, pos, state, isDown, tPos, scale) {
        const [wx, wy] = pos;
        const [sx, sy] = tPos(wx, wy);
        const side = fighter.side || (idx < 3 ? 'A' : 'B');
        const isSelected = selectedFighterIdx === idx;
        const theme = document.documentElement.getAttribute('data-theme') || 'dark';

        const colorA = '#ef4444';
        const colorB = '#22c55e';
        const baseColor = side === 'A' ? colorA : colorB;

        if (isDown) {
          // Dimmed casualty cross
          ctx.strokeStyle = 'rgba(148, 163, 184, 0.5)';
          ctx.lineWidth = 2;
          const sz = 5;
          ctx.beginPath();
          ctx.moveTo(sx - sz, sy - sz);
          ctx.lineTo(sx + sz, sy + sz);
          ctx.moveTo(sx + sz, sy - sz);
          ctx.lineTo(sx - sz, sy + sz);
          ctx.stroke();

          // Subtle name label
          ctx.font = '8px sans-serif';
          ctx.fillStyle = 'rgba(148, 163, 184, 0.6)';
          ctx.textAlign = 'center';
          ctx.fillText(getShortName(fighter.id), sx, sy + 12);
          return;
        }

        // Alive fighter dot
        const yawDeg = state ? state[0] : 0;
        const yawRad = (yawDeg * Math.PI) / 180;
        const radius = Math.max(6, Math.min(11, 7 * viewZoom));

        // Selection ring
        if (isSelected) {
          ctx.strokeStyle = '#38bdf8';
          ctx.lineWidth = 2.5;
          ctx.beginPath();
          ctx.arc(sx, sy, radius + 4, 0, Math.PI * 2);
          ctx.stroke();
        }

        // Dot fill
        ctx.fillStyle = baseColor;
        ctx.beginPath();
        ctx.arc(sx, sy, radius, 0, Math.PI * 2);
        ctx.fill();

        // Dot border
        ctx.strokeStyle = '#ffffff';
        ctx.lineWidth = 1.5;
        ctx.stroke();

        // Facing Tick (Yaw)
        const tickLen = radius + 6;
        const tx = sx + tickLen * Math.cos(yawRad);
        const ty = sy + tickLen * Math.sin(yawRad);

        ctx.strokeStyle = '#ffffff';
        ctx.lineWidth = 2;
        ctx.beginPath();
        ctx.moveTo(sx + radius * Math.cos(yawRad), sy + radius * Math.sin(yawRad));
        ctx.lineTo(tx, ty);
        ctx.stroke();

        // Stance badge if crouched/prone
        const stance = state ? state[1] : 'stand';
        if (stance !== 'stand') {
          ctx.fillStyle = '#000000';
          ctx.font = 'bold 8px sans-serif';
          ctx.textAlign = 'center';
          ctx.fillText(stance === 'crouch' ? 'C' : 'P', sx, sy + 3);
        }

        // Name tag
        ctx.font = 'bold 9px sans-serif';
        ctx.fillStyle = theme === 'dark' ? '#f3f4f6' : '#0f172a';
        ctx.textAlign = 'center';
        ctx.fillText(getShortName(fighter.id), sx, sy - radius - 3);

        // Order tag (if enabled)
        if (showOrderTags && state && state[3]) {
          const orderText = state[3];
          ctx.font = 'bold 8px sans-serif';
          const txtW = ctx.measureText(orderText).width;
          const bgW = txtW + 6;
          const bgH = 12;
          const bgX = sx - bgW / 2;
          const bgY = sy + radius + 4;

          ctx.fillStyle = theme === 'dark' ? 'rgba(15, 23, 42, 0.85)' : 'rgba(255, 255, 255, 0.85)';
          ctx.fillRect(bgX, bgY, bgW, bgH);
          ctx.strokeStyle = baseColor;
          ctx.lineWidth = 1;
          ctx.strokeRect(bgX, bgY, bgW, bgH);

          ctx.fillStyle = baseColor;
          ctx.textAlign = 'center';
          ctx.fillText(orderText, sx, bgY + 9);
        }
      }

      function getShortName(fullName) {
        if (!fullName) return '';
        const parts = fullName.split(' ');
        return parts[parts.length - 1]; // e.g. "Operator Lind" -> "Lind"
      }

      function updateUI(aliveA, aliveB, hpA, hpB, stateRow) {
        const totalA = round.fighters.filter((f, i) => (f.side || (i < 3 ? 'A' : 'B')) === 'A').length;
        const totalB = round.fighters.length - totalA;

        aliveTeamA.textContent = `A: ${aliveA}/${totalA} (${Math.round(hpA)} HP)`;
        aliveTeamB.textContent = `B: ${aliveB}/${totalB} (${Math.round(hpB)} HP)`;
        countTeamA.textContent = `${aliveA}/${totalA} Alive`;
        countTeamB.textContent = `${aliveB}/${totalB} Alive`;

        // Time readout
        timeReadout.textContent = `${formatTime(currentTime)} / ${formatTime(round.duration_s)}`;

        // Scrub thumb & fill
        const dur = Math.max(0.1, round.duration_s);
        const frac = Math.max(0, Math.min(1, currentTime / dur));
        scrubFill.style.width = `${frac * 100}%`;
        scrubThumb.style.left = `${frac * 100}%`;

        // Update Roster lists in-place (no DOM rebuild)
        updateRosterUI(stateRow);
      }

      const rosterCards = [];

      function initRosterDOM() {
        rosterTeamA.innerHTML = '';
        rosterTeamB.innerHTML = '';
        rosterCards.length = 0;

        if (!round || !round.fighters) return;

        round.fighters.forEach((f, idx) => {
          const side = f.side || (idx < 3 ? 'A' : 'B');
          const container = (side === 'A') ? rosterTeamA : rosterTeamB;

          const card = document.createElement('div');
          card.className = 'dossier-card';

          const cardTop = document.createElement('div');
          cardTop.className = 'card-top';

          const cardName = document.createElement('span');
          cardName.className = 'card-name';

          const tag = document.createElement('span');
          const safeSide = (side === 'A' || side === 'B') ? side.toLowerCase() : 'a';
          tag.className = `card-tag tag-${safeSide}`;
          tag.textContent = side;

          const fighterLabel = f.id || (`Fighter ${idx + 1}`);
          const nameText = document.createTextNode(` ${fighterLabel}`);
          cardName.appendChild(tag);
          cardName.appendChild(nameText);

          const hpText = document.createElement('span');
          hpText.style.fontFamily = 'monospace';
          hpText.style.fontSize = '11px';

          cardTop.appendChild(cardName);
          cardTop.appendChild(hpText);

          const hpBar = document.createElement('div');
          hpBar.className = 'card-hp-bar';

          const hpFill = document.createElement('div');
          hpFill.className = 'card-hp-fill';
          hpFill.style.background = (side === 'A') ? 'var(--team-a)' : 'var(--team-b)';
          hpBar.appendChild(hpFill);

          const cardDetails = document.createElement('div');
          cardDetails.className = 'card-details';

          const stanceLabel = document.createElement('span');
          stanceLabel.textContent = 'Stance: ';
          const stanceStrong = document.createElement('strong');
          stanceStrong.textContent = '-';
          stanceLabel.appendChild(stanceStrong);

          const orderBadge = document.createElement('span');
          orderBadge.className = 'order-badge';
          orderBadge.textContent = 'Idle';

          cardDetails.appendChild(stanceLabel);
          cardDetails.appendChild(orderBadge);

          card.appendChild(cardTop);
          card.appendChild(hpBar);
          card.appendChild(cardDetails);

          card.addEventListener('click', () => {
            selectedFighterIdx = idx;
            render();
          });

          container.appendChild(card);

          rosterCards[idx] = {
            card: card,
            hpText: hpText,
            hpFill: hpFill,
            stanceStrong: stanceStrong,
            orderBadge: orderBadge,
            side: side,
          };
        });
      }

      function updateRosterUI(stateRow) {
        if (!rosterCards || rosterCards.length === 0) return;

        rosterCards.forEach((rc, idx) => {
          if (!rc) return;
          const st = (stateRow && idx < stateRow.length) ? stateRow[idx] : null;
          const rawHp = (st && st[2] !== null && st[2] !== undefined) ? Number(st[2]) : 0;
          const hp = Math.max(0, rawHp);
          const isDown = !st || rawHp <= 0;
          const maxHp = 150;
          const hpPct = Math.max(0, Math.min(100, (hp / maxHp) * 100));
          const order = (st && st[3] !== null && st[3] !== undefined) ? String(st[3]) : (isDown ? 'KIA' : 'Idle');
          const stance = (st && st[1] !== null && st[1] !== undefined) ? String(st[1]) : '-';

          const isSel = (selectedFighterIdx === idx);
          rc.card.className = `dossier-card ${isDown ? 'down' : ''} ${isSel ? 'selected' : ''}`;
          rc.hpText.textContent = isDown ? 'KIA' : `${Math.round(hp)} HP`;
          rc.hpFill.style.width = `${hpPct}%`;
          rc.stanceStrong.textContent = stance;
          rc.orderBadge.textContent = order;
        });
      }

      // Initial setup
      resizeCanvas();
      switchRound(0);
    })();
  </script>
</body>
</html>
"""


def parse_round_track(track_data: dict, run_dir: str = "", map_layout: Optional[dict] = None) -> dict:
    """Parses one round track and computes derived metrics and pre-processed events."""
    dt = float(track_data.get("dt") or 0.2)
    units = str(track_data.get("units") or "cm").lower()
    # Normalize coordinates to cm
    scale_to_cm = 100.0 if units == "m" else 1.0

    raw_frames = track_data.get("frames") or []
    scaled_frames = []
    for f in raw_frames:
        if f is None:
            scaled_frames.append(None)
        else:
            scaled_f = []
            for p in f:
                if p and len(p) >= 2 and p[0] is not None and p[1] is not None:
                    scaled_f.append([round(float(p[0]) * scale_to_cm, 1), round(float(p[1]) * scale_to_cm, 1)])
                else:
                    scaled_f.append(None)
            scaled_frames.append(scaled_f)
    frames = scaled_frames

    states = track_data.get("states") or []
    fighters = track_data.get("fighters") or []
    events = track_data.get("events") or []

    duration_s = float(track_data.get("duration_s") or (len(frames) * dt if frames else 0.0))
    winner = track_data.get("winner")

    # Objective
    raw_obj = track_data.get("objective")
    obj_data = None
    if isinstance(raw_obj, dict):
        def_radius = 6.0 if units == "m" else 600.0
        obj_data = {
            "x": float(raw_obj.get("x") or 0.0) * scale_to_cm,
            "y": float(raw_obj.get("y") or 0.0) * scale_to_cm,
            "radius": float(raw_obj.get("radius") or def_radius) * scale_to_cm,
            "attacker": raw_obj.get("attacker"),
        }

    # Bounding box
    all_x = []
    all_y = []
    for f in (frames or []):
        for p in (f or []):
            if p and len(p) >= 2 and p[0] is not None and p[1] is not None:
                all_x.append(float(p[0]))
                all_y.append(float(p[1]))
    if obj_data:
        all_x.extend([obj_data["x"] - obj_data["radius"], obj_data["x"] + obj_data["radius"]])
        all_y.extend([obj_data["y"] - obj_data["radius"], obj_data["y"] + obj_data["radius"]])

    bounds = None
    if all_x and all_y:
        pad_x = max(200.0, (max(all_x) - min(all_x)) * 0.15)
        pad_y = max(200.0, (max(all_y) - min(all_y)) * 0.15)
        bounds = [round(min(all_x) - pad_x, 1), round(min(all_y) - pad_y, 1), round(max(all_x) + pad_x, 1), round(max(all_y) + pad_y, 1)]

    # Pre-process Events
    shots_t = sorted(float(e.get("t") or 0.0) for e in events if isinstance(e, dict) and e.get("type") == "shot")

    # Quiet spells (> 30s without a shot)
    quiet_spells = []
    if shots_t:
        if shots_t[0] >= 30.0:
            quiet_spells.append([0.0, round(shots_t[0], 1)])
        for a, b in zip(shots_t, shots_t[1:]):
            if b - a >= 30.0:
                quiet_spells.append([round(a, 1), round(b, 1)])
        if duration_s - shots_t[-1] >= 30.0:
            quiet_spells.append([round(shots_t[-1], 1), round(duration_s, 1)])
    elif duration_s >= 30.0:
        quiet_spells.append([0.0, round(duration_s, 1)])

    # Fighter lookup
    fighter_side = {}
    fighter_idx_map = {}
    for i, f in enumerate(fighters):
        if not f:
            continue
        s = f.get("side") or ("A" if i < len(fighters) // 2 else "B")
        fighter_side[(f.get("id"), s)] = i
        fighter_idx_map[i] = f

    # Process Shots with spatial start and end coordinates
    processed_shots = []
    kills = []
    tactics = []
    heals = []
    objective_events = []

    for e in events:
        if not isinstance(e, dict):
            continue
        tp = e.get("type")
        t = float(e.get("t") or 0.0)

        if tp == "shot":
            shooter_name = e.get("from")
            shooter_side = e.get("side", "A")
            target_raw = e.get("to")
            hit = bool(e.get("hit"))
            dist_m = float(e.get("dist_m") or 0.0)

            frame_idx = max(0, min(len(frames) - 1, int(round(t / dt)))) if dt > 0 and frames else 0
            cur_frame = (frames[frame_idx] or []) if (frames and frame_idx < len(frames)) else []
            cur_states = (states[frame_idx] or []) if (states and frame_idx < len(states)) else []

            # Shooter position
            shooter_idx = None
            for idx, f in enumerate(fighters):
                if f and f.get("id") == shooter_name and (f.get("side") == shooter_side or not f.get("side")):
                    shooter_idx = idx
                    break

            from_pos = [0.0, 0.0]
            shooter_yaw = 0.0
            if shooter_idx is not None and shooter_idx < len(cur_frame):
                pt = cur_frame[shooter_idx]
                if pt and len(pt) >= 2 and pt[0] is not None and pt[1] is not None:
                    from_pos = [float(pt[0]), float(pt[1])]
                if shooter_idx < len(cur_states):
                    st = cur_states[shooter_idx]
                    if st and len(st) > 0 and st[0] is not None:
                        shooter_yaw = float(st[0])

            # Resolve Target position
            to_pos = None
            enemy_side = "B" if shooter_side == "A" else "A"

            # 1. Match direct target name if given
            if target_raw:
                for idx, f in enumerate(fighters):
                    if f and f.get("id") == target_raw and f.get("side") == enemy_side:
                        if idx < len(cur_frame):
                            pt = cur_frame[idx]
                            if pt and len(pt) >= 2 and pt[0] is not None and pt[1] is not None:
                                to_pos = [float(pt[0]), float(pt[1])]
                        break

            # 2. If target is actor or not resolved, find alive enemy
            if not to_pos and cur_frame:
                alive_enemies = []
                for idx, f in enumerate(fighters):
                    if f and f.get("side") == enemy_side:
                        st = cur_states[idx] if idx < len(cur_states) else None
                        if st and len(st) > 2 and st[2] is not None and st[2] > 0:
                            if idx < len(cur_frame):
                                pt = cur_frame[idx]
                                if pt and len(pt) >= 2 and pt[0] is not None and pt[1] is not None:
                                    alive_enemies.append((idx, [float(pt[0]), float(pt[1])]))

                if alive_enemies:
                    # Pick enemy closest to yaw aim direction
                    rad = math.radians(shooter_yaw)
                    best_enemy = None
                    best_diff = 999.0
                    for _, epos in alive_enemies:
                        dx = epos[0] - from_pos[0]
                        dy = epos[1] - from_pos[1]
                        angle = math.atan2(dy, dx)
                        diff = abs(math.atan2(math.sin(angle - rad), math.cos(angle - rad)))
                        if diff < best_diff:
                            best_diff = diff
                            best_enemy = epos
                    if best_enemy:
                        to_pos = best_enemy

            # 3. Fallback to yaw projection
            if not to_pos:
                dist_cm = (dist_m * 100.0) if dist_m > 0 else 2500.0
                rad = math.radians(shooter_yaw)
                to_pos = [from_pos[0] + dist_cm * math.cos(rad), from_pos[1] + dist_cm * math.sin(rad)]

            processed_shots.append({
                "t": round(t, 2),
                "from": shooter_name,
                "side": shooter_side,
                "to": target_raw,
                "hit": hit,
                "from_pos": [round(from_pos[0], 1), round(from_pos[1], 1)],
                "to_pos": [round(to_pos[0], 1), round(to_pos[1], 1)],
            })

        elif tp == "kill":
            kills.append({
                "t": round(t, 2),
                "victim": e.get("victim") or e.get("target"),
                "by": e.get("by"),
                "side": e.get("side"),
            })
        elif tp in ("tactic", "decision"):
            tactics.append({
                "t": round(t, 2),
                "who": e.get("who"),
                "side": e.get("side"),
                "order": e.get("order") or e.get("decision"),
            })
        elif tp == "heal":
            heals.append({"t": round(t, 2), "side": e.get("side")})
        elif tp == "objective":
            objective_events.append({
                "t": round(t, 2),
                "event": e.get("event"),
                "side": e.get("side"),
            })

    return {
        "round": int(track_data.get("round") or 1),
        "map": track_data.get("map", "L_FormationLabCombat"),
        "scenario": track_data.get("scenario"),
        "winner": winner,
        "end_reason": track_data.get("end_reason"),
        "duration_s": round(duration_s, 2),
        "dt": round(dt, 3),
        "bounds": bounds,
        "objective": obj_data,
        "map_layout": map_layout,
        "fighters": fighters,
        "frames": frames,
        "states": states,
        "shots": processed_shots,
        "kills": kills,
        "tactics": tactics,
        "heals": heals,
        "objective_events": objective_events,
        "quiet_spells": quiet_spells,
    }


def find_map_layout(map_name: str, search_roots: List[str]) -> Optional[dict]:
    """Attempts to find and parse levelgen geometry for the given map name."""
    for root in search_roots:
        level_json_path = os.path.join(root, "Content", "Data", "LevelJson", f"{map_name}.json")
        if os.path.isfile(level_json_path):
            try:
                with open(level_json_path, "r", encoding="utf-8") as f:
                    doc = json.load(f)
                layout_ref = doc.get("levelgen_layout")
                if layout_ref:
                    layout_path = os.path.join(root, layout_ref)
                    if os.path.isfile(layout_path):
                        with open(layout_path, "r", encoding="utf-8") as lf:
                            ldoc = json.load(lf)
                        return {
                            "bounds": ldoc.get("bounds"),
                            "buildings": ldoc.get("buildings", []),
                            "walls": ldoc.get("walls", []),
                        }
            except Exception:
                pass

        # Check accepted directory directly
        accepted_layout = os.path.join(root, "Content", "Data", "LevelGen", "accepted", f"{map_name}.json")
        if os.path.isfile(accepted_layout):
            try:
                with open(accepted_layout, "r", encoding="utf-8") as lf:
                    ldoc = json.load(lf)
                return {
                    "bounds": ldoc.get("bounds"),
                    "buildings": ldoc.get("buildings", []),
                    "walls": ldoc.get("walls", []),
                }
            except Exception:
                pass

    return None


def generate_replay_html(run_id: str, parsed_rounds: List[dict], output_path: str) -> None:
    """Builds a single self-contained HTML page embedding all rounds."""
    payload = {
        "run_id": run_id,
        "generated_at": datetime.datetime.now().isoformat(),
        "rounds": parsed_rounds,
    }
    json_str = json.dumps(payload, ensure_ascii=False)
    # Safely escape for inline script tag
    json_escaped = json_str.replace("<", "\\u003c")

    html_content = HTML_TEMPLATE.replace("__REPLAY_DATA_JSON__", json_escaped)

    os.makedirs(os.path.dirname(os.path.abspath(output_path)), exist_ok=True)
    with open(output_path, "w", encoding="utf-8") as f:
        f.write(html_content)


def find_latest_arena_run(arena_dirs: List[str]) -> Tuple[str, List[str], Optional[str]]:
    """Finds the newest run directory with round tracks."""
    for base in arena_dirs:
        if not os.path.isdir(base):
            continue
        runs = []
        for entry in os.listdir(base):
            full = os.path.join(base, entry)
            if os.path.isdir(full):
                tracks = glob.glob(os.path.join(full, "round_*.track.json"))
                if tracks:
                    runs.append((os.path.getmtime(full), entry, full, tracks))
        if runs:
            runs.sort(key=lambda x: x[0], reverse=True)
            newest = runs[0]
            run_json = os.path.join(base, f"{newest[1]}.json")
            return newest[1], sorted(newest[3]), run_json if os.path.isfile(run_json) else None

    raise FileNotFoundError("No arena runs with round tracks found in search directories.")


def run_self_test(fixtures_dir: str, output_dir: str) -> bool:
    """Runs test mode: validates all fixtures, frame counts, and generates test HTML."""
    print("=" * 60)
    print("Running Arena Replay Self-Test...")
    print("=" * 60)

    if not os.path.isdir(fixtures_dir):
        print(f"FAIL: Fixtures directory not found at: {fixtures_dir}")
        return False

    fixture_files = sorted(glob.glob(os.path.join(fixtures_dir, "*.track.json")))
    if not fixture_files:
        print(f"FAIL: No .track.json fixtures found in {fixtures_dir}")
        return False

    print(f"Found {len(fixture_files)} fixtures in {fixtures_dir}:")
    parsed_rounds = []
    all_ok = True

    for ff in fixture_files:
        base_name = os.path.basename(ff)
        try:
            with open(ff, "r", encoding="utf-8") as f:
                data = json.load(f)

            frames = data.get("frames") or []
            fighters = data.get("fighters") or []
            events = data.get("events") or []
            duration = data.get("duration_s", 0)

            # Assertions
            if len(frames) == 0:
                print(f"  [FAIL] {base_name}: frames count is 0")
                all_ok = False
                continue
            if len(fighters) == 0:
                print(f"  [FAIL] {base_name}: fighters count is 0")
                all_ok = False
                continue

            parsed = parse_round_track(data, run_dir=fixtures_dir)
            parsed_rounds.append(parsed)

            shots = sum(1 for e in events if e.get("type") == "shot")
            kills = sum(1 for e in events if e.get("type") == "kill")
            objs = sum(1 for e in events if e.get("type") == "objective")
            print(f"  [OK] {base_name:36} frames={len(frames):3} fighters={len(fighters)} shots={shots:3} kills={kills} obj={objs} dur={duration:.1f}s")
        except Exception as e:
            print(f"  [ERROR] {base_name}: {e}")
            all_ok = False

    if not all_ok:
        print("\nTest failed on fixture validation.")
        return False

    # Generate test HTML
    test_out = os.path.join(output_dir, "test_replay.html")
    try:
        generate_replay_html("test_fixtures_run", parsed_rounds, test_out)
        size_bytes = os.path.getsize(test_out)
        print(f"\nGenerated test replay HTML: {test_out} ({size_bytes} bytes)")
        if size_bytes < 1000:
            print("FAIL: Generated HTML file is too small.")
            return False
    except Exception as e:
        print(f"FAIL: Generating HTML failed: {e}")
        return False

    print("\nALL SELF-TEST CHECKS PASSED (5/5 fixtures valid, HTML generated).")
    print("=" * 60)
    return True


def main():
    parser = argparse.ArgumentParser(description="PMC Arena Tactical Replay Page Generator")
    parser.add_argument("tracks", nargs="*", help="Track JSON files or run directory")
    parser.add_argument("--out", default="", help="Output directory for generated HTML")
    parser.add_argument("--latest", action="store_true", help="Process latest run from Saved/Arena")
    parser.add_argument("--test", action="store_true", help="Run self-test on test fixtures")

    args = parser.parse_args()

    script_dir = os.path.dirname(os.path.abspath(__file__))
    repo_root = os.path.abspath(os.path.join(script_dir, "..", "..", ".."))

    # Search paths for Saved/Arena and Content
    search_roots = [
        repo_root,
        os.path.abspath(os.path.join(repo_root, "..", "CodexTactics")),
    ]
    arena_search_dirs = [os.path.join(r, "Saved", "Arena") for r in search_roots]

    fixtures_dir = os.path.join(script_dir, "fixtures")
    default_out_dir = args.out or os.path.join(repo_root, "Saved", "ArenaReplay")

    if args.test:
        success = run_self_test(fixtures_dir, default_out_dir)
        sys.exit(0 if success else 1)

    track_files = []
    run_id = "replay"
    run_meta = None

    if args.latest:
        try:
            run_id, track_files, run_json_path = find_latest_arena_run(arena_search_dirs)
            print(f"Processing latest run: {run_id} ({len(track_files)} rounds)")
            if run_json_path and os.path.isfile(run_json_path):
                with open(run_json_path, "r", encoding="utf-8") as f:
                    run_meta = json.load(f)
        except Exception as e:
            print(f"Error finding latest run: {e}", file=sys.stderr)
            sys.exit(1)
    elif args.tracks:
        for item in args.tracks:
            if os.path.isdir(item):
                run_id = os.path.basename(os.path.normpath(item))
                found = sorted(glob.glob(os.path.join(item, "round_*.track.json")))
                track_files.extend(found)
            elif os.path.isfile(item):
                track_files.append(item)
                if run_id == "replay":
                    base_n = os.path.basename(item)
                    run_id = re.sub(r"\.(track\.)?json$", "", base_n)
    else:
        parser.print_help()
        sys.exit(1)

    if not track_files:
        print("No track files found to process.", file=sys.stderr)
        sys.exit(1)

    parsed_rounds = []
    for tf in track_files:
        with open(tf, "r", encoding="utf-8") as f:
            data = json.load(f)
        map_name = data.get("map", "L_FormationLabCombat")
        layout = find_map_layout(map_name, search_roots)
        parsed = parse_round_track(data, run_dir=os.path.dirname(tf), map_layout=layout)

        # Infer run_id from track if generic
        if (run_id == "replay" or run_id.startswith("round_")) and data.get("run"):
            run_id = data.get("run")

        # Merge metadata from run.json if present
        if run_meta and "rounds" in run_meta:
            rnd_num = parsed.get("round", 1)
            for rr in run_meta.get("rounds", []):
                if rr.get("index") == rnd_num:
                    if not parsed.get("end_reason"):
                        parsed["end_reason"] = rr.get("end_reason")
                    if not parsed.get("scenario"):
                        parsed["scenario"] = rr.get("scenario")
                    break

        parsed_rounds.append(parsed)

    clean_run_id = re.sub(r"[^\w.-]", "_", str(run_id or "replay")).strip("._")
    if not clean_run_id:
        clean_run_id = "replay"
    out_file = os.path.join(default_out_dir, f"{clean_run_id}_replay.html")
    generate_replay_html(clean_run_id, parsed_rounds, out_file)
    print(f"Successfully generated arena replay: {out_file} ({os.path.getsize(out_file)} bytes)")


if __name__ == "__main__":
    main()
