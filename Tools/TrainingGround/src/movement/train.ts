// Training Ground / movement: the tuner loop on the user's API tokens (FORMATION_CONTRACT.md 6). Each round: a deterministic
// series (0 tokens) -> a compact summary -> the model proposes the next parameter set (strict JSON inside the ranges) -> the
// series scores it; the best set is kept. Stops on the budget, on no improvement for `patience` rounds, or when all checks
// are green. The model never sees raw runs; a coding agent reads only train_report.md.

import type { ModelClient, ProposePayload } from './ai.ts';
import { FIXED_PARAMS, parsePropose, proposePrompt } from './ai.ts';
import { runSeries } from './bot.ts';
import type { SeriesSummary } from './checks.ts';
import type { FormationConfigFile } from './config.ts';
import { configValues, withValues } from './config.ts';
import { round } from './geometry.ts';
import type { Layout } from './layout.ts';
import { buildNav } from './sim.ts';

export interface TrainOptions {
  layout: Layout;
  configFile: FormationConfigFile;
  client: ModelClient | null;
  rounds: number;
  budgetUsd: number;
  patience: number;
  members: number[];
  seeds: number;
  seedBase: number;
  dryRun: boolean;
  log?: (line: string) => void;
}

export interface CallRecord { round: number; model: string; inputTokens: number; outputTokens: number; costUsd: number; error?: string }

export interface TrainResult {
  baseline: { score: number; summary: SeriesSummary };
  best: { score: number; summary: SeriesSummary; file: FormationConfigFile; round: number };
  rounds: { round: number; score: number; changed: Record<string, number>; reason: string; error?: string }[];
  calls: CallRecord[];
  spendUsd: number;
  stopReason: string;
  /** The starting values (the report prints the winning diff against them). */
  baselineValues: Record<string, number>;
  /** --dry-run: the request that would be sent first (system + prompt) and a rough token estimate. */
  dryRequest?: { model: string; system: string; prompt: string; estInputTokens: number };
}

export function tunableOf(file: FormationConfigFile): ProposePayload['tunable'] {
  const out: ProposePayload['tunable'] = {};
  for (const [k, p] of Object.entries(file.params)) {
    if (FIXED_PARAMS.includes(k)) continue;
    out[k] = { value: p.value, min: p.min, max: p.max, unit: p.unit, doc: p.doc };
  }
  return out;
}

export async function train(o: TrainOptions): Promise<TrainResult> {
  const log = o.log ?? (() => {});
  const nav = buildNav(o.layout);
  const series = (file: FormationConfigFile) =>
    runSeries({ layout: o.layout, config: configValues(file), members: o.members, seeds: o.seeds, seedBase: o.seedBase, nav }).summary;

  const baseSummary = series(o.configFile);
  const result: TrainResult = {
    baseline: { score: baseSummary.score, summary: baseSummary },
    best: { score: baseSummary.score, summary: baseSummary, file: o.configFile, round: 0 },
    rounds: [], calls: [], spendUsd: 0, stopReason: '', baselineValues: configValues(o.configFile),
  };
  log(`[train] baseline score ${baseSummary.score}${baseSummary.allGreen ? ' (all green)' : ''}`);
  const tunable = tunableOf(o.configFile);

  const payload = (r: number): ProposePayload => ({
    kind: 'propose', layout: o.layout.name, round: r,
    best: { score: result.best.score, summary: result.best.summary, params: configValues(result.best.file) },
    history: result.rounds.slice(-6).map((x) => ({ round: x.round, score: x.score, changed: x.changed })),
    tunable: { ...tunable, ...Object.fromEntries(Object.entries(tunable).map(([k, t]) => [k, { ...t, value: result.best.file.params[k].value }])) },
  });

  if (o.dryRun) {
    const req = proposePrompt(payload(1));
    result.dryRequest = { model: req.model, system: req.system, prompt: req.prompt, estInputTokens: Math.ceil((req.system.length + req.prompt.length) / 4) };
    result.stopReason = 'dry run (no API call)';
    return result;
  }
  if (baseSummary.allGreen) { result.stopReason = 'all checks green at baseline'; return result; }
  if (!o.client) { result.stopReason = 'no model client (baseline only)'; return result; }

  let noImprove = 0;
  let lastCost = 0;
  for (let r = 1; r <= o.rounds; r++) {
    if (result.spendUsd >= o.budgetUsd) { result.stopReason = `budget $${o.budgetUsd} reached`; break; }
    if (result.spendUsd + lastCost > o.budgetUsd) { result.stopReason = `budget: the next call (~$${lastCost.toFixed(3)}) would exceed $${o.budgetUsd}`; break; }
    const req = proposePrompt(payload(r));
    let changed: Record<string, number> = {}, reason = '', error: string | undefined;
    try {
      const reply = await o.client.complete(req);
      lastCost = reply.costUsd;
      result.spendUsd = round(result.spendUsd + reply.costUsd, 6);
      result.calls.push({ round: r, model: req.model, inputTokens: reply.usage.input_tokens, outputTokens: reply.usage.output_tokens, costUsd: reply.costUsd });
      log(`[train] round ${r}: ${req.model} ${reply.usage.input_tokens} in / ${reply.usage.output_tokens} out, $${reply.costUsd.toFixed(4)} (total $${result.spendUsd.toFixed(4)})`);
      ({ params: changed, reason } = parsePropose(reply.text, tunable));
    } catch (e) {
      error = e instanceof Error ? e.message : String(e);
      result.calls.push({ round: r, model: req.model, inputTokens: 0, outputTokens: 0, costUsd: 0, error });
      log(`[train] round ${r}: ${error}`);
    }
    if (error || !Object.keys(changed).length) {
      result.rounds.push({ round: r, score: result.best.score, changed, reason, error: error ?? 'no parameter changed' });
      if (++noImprove >= o.patience) { result.stopReason = `no improvement for ${o.patience} rounds`; break; }
      continue;
    }
    const candidate = withValues(result.best.file, changed).file;
    const summary = series(candidate);
    result.rounds.push({ round: r, score: summary.score, changed, reason });
    log(`[train] round ${r}: score ${summary.score} (${Object.entries(changed).map(([k, v]) => `${k}=${round(v, 3)}`).join(', ')})`);
    if (summary.score > result.best.score + 1e-9) {
      result.best = { score: summary.score, summary, file: candidate, round: r };
      noImprove = 0;
    } else noImprove++;
    if (result.best.summary.allGreen) { result.stopReason = `all checks green (round ${result.best.round})`; break; }
    if (noImprove >= o.patience) { result.stopReason = `no improvement for ${o.patience} rounds`; break; }
    if (r === o.rounds) result.stopReason = `${o.rounds} rounds done`;
  }
  return result;
}

/** train_report.md (<= 30 lines): the only training output a coding agent reads. */
export function renderTrainReport(res: TrainResult, h: { layout: string; client: string; budgetUsd: number; date: string }): string {
  const changed: string[] = [];
  for (const [k, p] of Object.entries(res.best.file.params)) {
    const orig = res.baselineValues[k];
    if (orig !== undefined && orig !== p.value) changed.push(`- ${k}: ${orig} -> ${round(p.value, 4)} ${p.unit}`);
  }
  const lines = [
    `# Movement tuner report - ${h.layout}`,
    `client ${h.client} | rounds ${res.rounds.length} | calls ${res.calls.length} | spend $${res.spendUsd.toFixed(4)} of $${h.budgetUsd} | ${h.date}`,
    `stop: ${res.stopReason}`,
    `score: baseline ${res.baseline.score} -> best ${res.best.score} (round ${res.best.round})${res.best.summary.allGreen ? ' ALL GREEN' : ''}`,
    ...(res.dryRequest ? [`dry run: first request ${res.dryRequest.model}, ~${res.dryRequest.estInputTokens} input tokens (train_dry_request.txt)`] : []),
    '',
    '| # | check (best set) | pass | fail |',
    '|---|---|---|---|',
    ...res.best.summary.checks.filter((c) => !c.info).map((c) => `| ${c.id} | ${c.name} | ${c.pass} | ${c.fail} |`),
    '',
    changed.length ? 'winning parameters (diff vs formation_config.json):' : 'winning parameters: unchanged',
    ...changed.slice(0, 10),
    '',
    "best config: train_best_config.json (goes to main only after the coordinator's review)",
  ];
  return lines.slice(0, 30).join('\n') + '\n';
}
