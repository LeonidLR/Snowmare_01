import { defineConfig, Plugin } from 'vite'
import react from '@vitejs/plugin-react'
import fs from 'fs'
import path from 'path'
import { spawn } from 'child_process'

const PROJECT_PATH = path.resolve(__dirname, '../../')
const LEVELS_DIR = path.resolve(__dirname, '../../Content/Data/LevelJson')
const TELEMETRY_DIR = path.resolve(__dirname, '../../Saved/Telemetry')
const RAW_RUNS_FILE = path.join(TELEMETRY_DIR, 'raw_runs', 'runs.jsonl')
const BOT_PRESETS_FILE = path.resolve(__dirname, '../../Content/Data/Bot/bot_presets.json')
const SPATIAL_RUNS_DIR = path.join(TELEMETRY_DIR, 'spatial_runs')
const GIF_EXPORTS_DIR = path.join(TELEMETRY_DIR, 'exports_gif')

const setupApiMiddlewares = (middlewares: any) => {
  const addCors = (res: any) => {
    res.setHeader('Access-Control-Allow-Origin', '*')
    res.setHeader('Access-Control-Allow-Methods', 'GET, POST, OPTIONS')
    res.setHeader('Access-Control-Allow-Headers', 'Content-Type')
  }

  // 0. Health check для индикатора подключения
  middlewares.use('/api/health', (req: any, res: any) => {
    addCors(res)
    res.statusCode = 200
    res.setHeader('Content-Type', 'application/json; charset=utf-8')
    res.end(JSON.stringify({ status: 'ok', projectPath: PROJECT_PATH, levelsDir: LEVELS_DIR }))
  })

  // 1. Получение списка всех этапов
  middlewares.use('/api/list-stages', (req: any, res: any) => {
    addCors(res)
    try {
      const files = fs.readdirSync(LEVELS_DIR)
      const stages: any[] = []
      for (const file of files) {
        if (file.startsWith('stage_') && file.endsWith('.json') && file !== 'stage_template.json') {
          const p = path.join(LEVELS_DIR, file)
          const content = JSON.parse(fs.readFileSync(p, 'utf-8'))
          stages.push({
            stage_id: content.level_id || file.replace('.json', ''),
            stage_number: content.stage_number || 1,
            stage_name: content.level_name || file,
            act: content.act || 1,
            biome_id: content.biome_id || 'bunker_industrial',
            waves_count: content.waves ? content.waves.length : 0,
            description: content.description || ''
          })
        }
      }
      stages.sort((a, b) => a.stage_number - b.stage_number)
      res.statusCode = 200
      res.setHeader('Content-Type', 'application/json; charset=utf-8')
      res.end(JSON.stringify(stages))
    } catch (e: any) {
      res.statusCode = 500
      res.end(JSON.stringify({ error: e.message }))
    }
  })

  // 2. Чтение конкретного этапа
  middlewares.use('/api/get-stage', (req: any, res: any) => {
    addCors(res)
    const url = new URL(req.url || '', 'http://localhost:5173')
    const stageId = url.searchParams.get('id') || 'stage_01'
    const targetPath = path.join(LEVELS_DIR, `${stageId}.json`)
    if (fs.existsSync(targetPath)) {
      const content = fs.readFileSync(targetPath, 'utf-8')
      res.statusCode = 200
      res.setHeader('Content-Type', 'application/json; charset=utf-8')
      res.end(content)
    } else {
      const fallback = path.join(LEVELS_DIR, 'level_01_outpost.json')
      if (fs.existsSync(fallback)) {
        res.statusCode = 200
        res.setHeader('Content-Type', 'application/json; charset=utf-8')
        res.end(fs.readFileSync(fallback, 'utf-8'))
      } else {
        res.statusCode = 404
        res.end(JSON.stringify({ error: 'Stage not found' }))
      }
    }
  })

  // 3. Сохранение этапа на диск
  middlewares.use('/api/save-stage', (req: any, res: any, next: any) => {
    addCors(res)
    if (req.method === 'OPTIONS') {
      res.statusCode = 204
      res.end()
      return
    }
    if (req.method === 'POST') {
      let body = ''
      req.on('data', (chunk: any) => { body += chunk })
      req.on('end', () => {
        try {
          const data = JSON.parse(body)
          const stageId = data.level_id || 'stage_01'
          const targetPath = path.join(LEVELS_DIR, `${stageId}.json`)
          fs.writeFileSync(targetPath, JSON.stringify(data, null, 2), 'utf-8')
          
          // Также синхронизируем в рабочий файл игры
          const activeLevelPath = path.join(LEVELS_DIR, 'level_01_outpost.json')
          fs.writeFileSync(activeLevelPath, JSON.stringify(data, null, 2), 'utf-8')

          res.statusCode = 200
          res.setHeader('Content-Type', 'application/json')
          res.end(JSON.stringify({ success: true, stage_id: stageId }))
        } catch (e: any) {
          res.statusCode = 500
          res.end(JSON.stringify({ error: e.message }))
        }
      })
    } else if (typeof next === 'function') {
      next()
    }
  })

  // 4. Fallback: старый get-config
  middlewares.use('/api/get-config', (req: any, res: any) => {
    addCors(res)
    const targetPath = path.join(LEVELS_DIR, 'stage_01.json')
    const fallback = path.join(LEVELS_DIR, 'level_01_outpost.json')
    const p = fs.existsSync(targetPath) ? targetPath : fallback
    if (fs.existsSync(p)) {
      res.statusCode = 200
      res.setHeader('Content-Type', 'application/json; charset=utf-8')
      res.end(fs.readFileSync(p, 'utf-8'))
    } else {
      res.statusCode = 404
      res.end(JSON.stringify({ error: 'File not found' }))
    }
  })

  // Очистка логов телеметрии
  middlewares.use('/api/clear-telemetry', (req: any, res: any, next: any) => {
    addCors(res)
    if (req.method === 'OPTIONS') {
      res.statusCode = 204
      res.end()
      return
    }
    if (req.method === 'POST') {
      const targetPath = RAW_RUNS_FILE
      fs.mkdirSync(path.dirname(targetPath), { recursive: true })
      fs.writeFileSync(targetPath, '', 'utf-8')
      res.statusCode = 200
      res.setHeader('Content-Type', 'application/json')
      res.end(JSON.stringify({ success: true }))
    } else if (typeof next === 'function') {
      next()
    }
  })

  // 5a. Live bot batch status (Sprint 05-B): Scripts/bot_run.ps1 keeps Saved/Telemetry/bot_status.json current.
  middlewares.use('/api/bot-status', (req: any, res: any) => {
    addCors(res)
    const idle = { isRunning: false, currentRun: 0, totalRuns: 0, activeRuns: 0, victories: 0, defeats: 0, aborted: 0, errors: 0, elapsedSec: 0, profile: '' }
    try {
      const statusPath = path.join(TELEMETRY_DIR, 'bot_status.json')
      if (!fs.existsSync(statusPath)) {
        res.setHeader('Content-Type', 'application/json')
        res.end(JSON.stringify(idle))
        return
      }
      const status = JSON.parse(fs.readFileSync(statusPath, 'utf-8').replace(/^﻿/, ''))
      // A runner that died (window closed) leaves isRunning true: check its process.
      if (status.isRunning && status.pid) {
        try {
          process.kill(status.pid, 0)
        } catch {
          status.isRunning = false
          status.stale = true
        }
      }
      if (status.isRunning && status.startedAtUtc) {
        status.elapsedSec = Math.round((Date.now() - Date.parse(status.startedAtUtc)) / 1000)
      }
      res.setHeader('Content-Type', 'application/json')
      res.end(JSON.stringify({ ...idle, ...status }))
    } catch (e: any) {
      res.setHeader('Content-Type', 'application/json')
      res.end(JSON.stringify({ ...idle, error: e.message }))
    }
  })

  // 5. Автоматическое чтение свежих логов телеметрии
  middlewares.use('/api/get-telemetry', (req: any, res: any) => {
    addCors(res)
    const targetPath = RAW_RUNS_FILE
    if (fs.existsSync(targetPath)) {
      const content = fs.readFileSync(targetPath, 'utf-8')
      res.statusCode = 200
      res.setHeader('Content-Type', 'text/plain; charset=utf-8')
      res.end(content)
    } else {
      res.statusCode = 200
      res.end('')
    }
  })

  // 6. Получение списка пресетов бота
  middlewares.use('/api/get-bot-presets', (req: any, res: any) => {
    addCors(res)
    try {
      const presetsPath = BOT_PRESETS_FILE
      if (fs.existsSync(presetsPath)) {
        const content = fs.readFileSync(presetsPath, 'utf-8')
        res.statusCode = 200
        res.setHeader('Content-Type', 'application/json; charset=utf-8')
        res.end(content)
      } else {
        const defaults = [
          { id: "short_check", name: "Короткая проверка", description: "1 забег, NORMAL, 5.0x", runs: 1, profile: "NORMAL", speed: 5.0 },
          { id: "fast_test", name: "Быстрый тест", description: "5 забегов, NORMAL, 8.0x", runs: 5, profile: "NORMAL", speed: 8.0 },
          { id: "standard_eval", name: "Стандартный прогон", description: "20 забегов, NORMAL, 8.0x", runs: 20, profile: "NORMAL", speed: 8.0 },
          { id: "veteran_stress", name: "Стресс-тест ветерана", description: "50 забегов, VETERAN, 10.0x", runs: 50, profile: "VETERAN", speed: 10.0 }
        ]
        res.statusCode = 200
        res.setHeader('Content-Type', 'application/json; charset=utf-8')
        res.end(JSON.stringify(defaults))
      }
    } catch (e: any) {
      res.statusCode = 500
      res.end(JSON.stringify({ error: e.message }))
    }
  })

  // 7. Сохранение пользовательского пресета бота
  middlewares.use('/api/save-bot-preset', (req: any, res: any, next: any) => {
    addCors(res)
    if (req.method === 'OPTIONS') {
      res.statusCode = 204
      res.end()
      return
    }
    if (req.method === 'POST') {
      let body = ''
      req.on('data', (chunk: any) => { body += chunk })
      req.on('end', () => {
        try {
          const presetsPath = BOT_PRESETS_FILE
          let presets = []
          if (fs.existsSync(presetsPath)) {
            presets = JSON.parse(fs.readFileSync(presetsPath, 'utf-8'))
          }
          const newPreset = JSON.parse(body)
          const existingIdx = presets.findIndex((p: any) => p.id === newPreset.id)
          if (existingIdx >= 0) {
            presets[existingIdx] = newPreset
          } else {
            presets.push(newPreset)
          }
          fs.writeFileSync(presetsPath, JSON.stringify(presets, null, 2), 'utf-8')
          res.statusCode = 200
          res.setHeader('Content-Type', 'application/json')
          res.end(JSON.stringify({ success: true, presets }))
        } catch (e: any) {
          res.statusCode = 500
          res.end(JSON.stringify({ error: e.message }))
        }
      })
    } else if (typeof next === 'function') {
      next()
    }
  })

  // 8. Запуск симулятора с поддержкой параметров пресета
  middlewares.use('/api/run-bot', (req: any, res: any, next: any) => {
    addCors(res)
    if (req.method === 'OPTIONS') {
      res.statusCode = 204
      res.end()
      return
    }
    if (req.method === 'POST') {
      let body = ''
      req.on('data', (chunk: any) => { body += chunk })
      req.on('end', () => {
        try {
          const payload = body ? JSON.parse(body) : {}

          // Сохраняем выбранный конфиг перед запуском
          if (payload.config) {
            const activeLevelPath = path.join(LEVELS_DIR, 'level_01_outpost.json')
            fs.writeFileSync(activeLevelPath, JSON.stringify(payload.config, null, 2), 'utf-8')
            if (payload.config.level_id) {
              const stagePath = path.join(LEVELS_DIR, `${payload.config.level_id}.json`)
              fs.writeFileSync(stagePath, JSON.stringify(payload.config, null, 2), 'utf-8')
            }
          }

          // Запускаем отдельное видимое консольное окно с аргументами пресета
          // Unreal playtest bot (Scripts/run_simulations.bat, the C++ bot); not there yet -> say so instead of failing silently.
          const batPath = path.resolve(__dirname, '../../Scripts/run_simulations.bat')
          if (!fs.existsSync(batPath)) {
            res.statusCode = 501
            res.setHeader('Content-Type', 'application/json')
            res.end(JSON.stringify({ error: 'Бот Unreal ещё не готов (Scripts/run_simulations.bat отсутствует). Конфиг уровня сохранён.' }))
            return
          }
          const args = ['/c', 'start', '""', batPath]
          if (payload.runs) {
            args.push(String(payload.runs))
            args.push(String(payload.profile || 'NORMAL'))
            args.push(String(payload.speed || '5.0'))
          }

          const child = spawn('cmd.exe', args, {
            cwd: PROJECT_PATH,
            detached: true,
            stdio: 'ignore'
          })
          child.unref()

          res.statusCode = 200
          res.setHeader('Content-Type', 'application/json')
          res.end(JSON.stringify({
            success: true,
            message: payload.runs 
              ? `Запущен пресет: ${payload.runs} забегов (${payload.profile || 'NORMAL'}, ${payload.speed || 5.0}x)`
              : 'Окно симулятора открыто! Выберите режим в консоли.'
          }))
        } catch (e: any) {
          res.statusCode = 500
          res.setHeader('Content-Type', 'application/json')
          res.end(JSON.stringify({ error: e.message }))
        }
      })
    } else if (typeof next === 'function') {
      next()
    }
  })

  // 9. Список забегов пространственной телеметрии
  middlewares.use('/api/get-spatial-telemetry-list', (req: any, res: any) => {
    addCors(res)
    try {
      const spatialDir = SPATIAL_RUNS_DIR
      if (!fs.existsSync(spatialDir)) {
        fs.mkdirSync(spatialDir, { recursive: true })
      }
      const files = fs.readdirSync(spatialDir).filter((f: string) => f.endsWith('.json'))
      const list = files.map((fileName: string) => {
        const fullPath = path.join(spatialDir, fileName)
        try {
          const content = fs.readFileSync(fullPath, 'utf-8')
          const data = JSON.parse(content)
          return {
            fileName,
            sessionId: data.session_id || fileName,
            timestamp: data.timestamp_utc || '',
            levelId: data.level_id || '',
            profile: data.tester_profile || '',
            durationSec: data.duration_sec || 0,
            result: data.summary?.result || 'UNKNOWN',
            wavesCleared: data.summary?.waves_cleared || 0,
            framesCount: data.frames?.length || 0,
            chokePointsCount: data.summary?.choke_points_detected?.length || 0
          }
        } catch (err) {
          return { fileName, sessionId: fileName, error: true }
        }
      }).sort((a: any, b: any) => (b.timestamp || '').localeCompare(a.timestamp || ''))
      res.statusCode = 200
      res.setHeader('Content-Type', 'application/json; charset=utf-8')
      res.end(JSON.stringify(list))
    } catch (e: any) {
      res.statusCode = 500
      res.end(JSON.stringify({ error: e.message }))
    }
  })

  // 10. Получение конкретного файла пространственной телеметрии
  middlewares.use('/api/get-spatial-telemetry', (req: any, res: any) => {
    addCors(res)
    try {
      const url = new URL(req.url, 'http://localhost')
      const fileName = url.searchParams.get('file')
      if (!fileName) {
        res.statusCode = 400
        res.end(JSON.stringify({ error: 'Missing file parameter' }))
        return
      }
      const safeName = path.basename(fileName)
      const targetPath = path.join(SPATIAL_RUNS_DIR, safeName)
      if (fs.existsSync(targetPath)) {
        const content = fs.readFileSync(targetPath, 'utf-8')
        res.statusCode = 200
        res.setHeader('Content-Type', 'application/json; charset=utf-8')
        res.end(content)
      } else {
        res.statusCode = 404
        res.end(JSON.stringify({ error: 'File not found' }))
      }
    } catch (e: any) {
      res.statusCode = 500
      res.end(JSON.stringify({ error: e.message }))
    }
  })

  // 11. Сохранение экспортированного GIF на диск
  middlewares.use('/api/save-gif', (req: any, res: any, next: any) => {
    addCors(res)
    if (req.method === 'OPTIONS') {
      res.statusCode = 204
      res.end()
      return
    }
    if (req.method === 'POST') {
      let body = ''
      req.on('data', (chunk: any) => { body += chunk })
      req.on('end', () => {
        try {
          const { filename, base64Data } = JSON.parse(body)
          const safeName = path.basename(filename || 'replay.gif')
          if (!fs.existsSync(GIF_EXPORTS_DIR)) {
            fs.mkdirSync(GIF_EXPORTS_DIR, { recursive: true })
          }
          const buffer = Buffer.from(base64Data, 'base64')
          fs.writeFileSync(path.join(GIF_EXPORTS_DIR, safeName), buffer)
          res.statusCode = 200
          res.setHeader('Content-Type', 'application/json')
          res.end(JSON.stringify({ success: true, filename: safeName }))
        } catch (e: any) {
          res.statusCode = 500
          res.end(JSON.stringify({ error: e.message }))
        }
      })
    } else if (typeof next === 'function') {
      next()
    }
  })

  // 12. Удаление отдельного файла забега и связанных с ним гифок
  middlewares.use('/api/delete-spatial-telemetry', (req: any, res: any, next: any) => {
    addCors(res)
    if (req.method === 'OPTIONS') {
      res.statusCode = 204
      res.end()
      return
    }
    if (req.method === 'POST') {
      let body = ''
      req.on('data', (chunk: any) => { body += chunk })
      req.on('end', () => {
        try {
          const { file } = JSON.parse(body || '{}')
          if (!file) {
            res.statusCode = 400
            res.end(JSON.stringify({ error: 'Missing file parameter' }))
            return
          }
          const safeName = path.basename(file)
          const targetJson = path.join(SPATIAL_RUNS_DIR, safeName)
          let deletedJson = false
          if (fs.existsSync(targetJson)) {
            fs.unlinkSync(targetJson)
            deletedJson = true
          }

          // Поиск и удаление ассоциированных GIF
          let deletedGifs = 0
          if (fs.existsSync(GIF_EXPORTS_DIR)) {
            const baseId = safeName.replace('.json', '').replace('run_', '')
            const gifFiles = fs.readdirSync(GIF_EXPORTS_DIR)
            for (const gf of gifFiles) {
              if (gf.includes(baseId) || gf.includes(safeName.replace('.json', ''))) {
                fs.unlinkSync(path.join(GIF_EXPORTS_DIR, gf))
                deletedGifs++
              }
            }
          }
          res.statusCode = 200
          res.setHeader('Content-Type', 'application/json')
          res.end(JSON.stringify({ success: true, deletedJson, deletedGifs }))
        } catch (e: any) {
          res.statusCode = 500
          res.end(JSON.stringify({ error: e.message }))
        }
      })
    } else if (typeof next === 'function') {
      next()
    }
  })

  // 13. Полная очистка списка забегов и всех связанных GIF
  middlewares.use('/api/clear-spatial-telemetry', (req: any, res: any, next: any) => {
    addCors(res)
    if (req.method === 'OPTIONS') {
      res.statusCode = 204
      res.end()
      return
    }
    if (req.method === 'POST') {
      try {
        let deletedRuns = 0
        let deletedGifs = 0
        if (fs.existsSync(SPATIAL_RUNS_DIR)) {
          const files = fs.readdirSync(SPATIAL_RUNS_DIR)
          for (const f of files) {
            if (f.endsWith('.json')) {
              fs.unlinkSync(path.join(SPATIAL_RUNS_DIR, f))
              deletedRuns++
            }
          }
        }
        if (fs.existsSync(GIF_EXPORTS_DIR)) {
          const gifs = fs.readdirSync(GIF_EXPORTS_DIR)
          for (const g of gifs) {
            if (g.endsWith('.gif') || g.endsWith('.webm') || g.endsWith('.mp4')) {
              fs.unlinkSync(path.join(GIF_EXPORTS_DIR, g))
              deletedGifs++
            }
          }
        }
        res.statusCode = 200
        res.setHeader('Content-Type', 'application/json')
        res.end(JSON.stringify({ success: true, deletedRuns, deletedGifs }))
      } catch (e: any) {
        res.statusCode = 500
        res.end(JSON.stringify({ error: e.message }))
      }
    } else if (typeof next === 'function') {
      next()
    }
  })
}

const gameSyncPlugin = (): Plugin => ({
  name: 'game-sync-plugin',
  configureServer(server) {
    setupApiMiddlewares(server.middlewares)
  },
  configurePreviewServer(server) {
    setupApiMiddlewares(server.middlewares)
  }
})

export default defineConfig({
  plugins: [react(), gameSyncPlugin()],
  server: {
    port: 5173,
    open: true
  },
  preview: {
    port: 5173
  }
})
