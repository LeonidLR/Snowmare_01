"""Bounded PMC doctrine *proxies*, selected from observed designer tactics.

Retreat survival does not prove an immediate cover reaction; suppress-then-move
does not prove elevated overwatch. Keep these distinctions in the profile.
Never invent a metric for high ground, scanning or exposure from zeros.
"""
from __future__ import annotations

import math

PROXIES = {
    'fallback_to_cover_after_contact': ('retreats_survived', 2.0, 'surviving a retreat, not contact-to-cover latency'),
    'first_in_cover_overwatch': ('behaviour.suppress_then_move', 2.0, 'suppress then move, not cover/height confirmation'),
    'distract_and_flank': ('flanks_succeeded', 2.0, 'successful flank, not confirmed distraction or stealth'),
}


def profile(dataset):
    if dataset.get('schema') != 'codex.demo_dataset/1' or dataset.get('errors'):
        raise ValueError('need an error-free codex.demo_dataset/1')
    eligible = [r for r in dataset['fights'] if not r['lab'] and not r['truncated']]
    terms = []
    for tag, (metric, cap, limitation) in PROXIES.items():
        support = sum(any(t['tag'] == tag for t in r['tactics']) for r in eligible)
        if support:
            terms.append({'tag': tag, 'metric': metric, 'cap': cap, 'support_fights': support,
                          'limitation': limitation})
    total = sum(t['support_fights'] for t in terms)
    for term in terms:
        term['weight'] = term['support_fights'] / total
    return {'schema': 'codex.doctrine_proxies/1', 'weight': 1.0, 'terms': terms,
            'source_recordings': [{k: s[k] for k in ('recording', 'sha256', 'notes_sha256')} for s in dataset['sources']],
            'excluded': [s['tag'] for s in dataset['summary'] if s['tag'] not in {t['tag'] for t in terms}],
            'note': 'Frequency selects priorities; win correlation is not a causal reward. Maximum bonus 1 point.'}


def measurement(row, metric):
    if metric.startswith('behaviour.'):
        key = metric.split('.', 1)[1]
        # Adapter defaults are ambiguous. Only explicitly measured fields count.
        if key not in row.get('behaviour_observed', []):
            return None
        value = row.get('behaviour', {}).get(key)
    else:
        value = row.get(metric)
    if isinstance(value, bool) or not isinstance(value, (int, float)) or not math.isfinite(value):
        return None
    return float(value)


def evaluate(row, cfg):
    """Missing fields receive no credit and remain listed; no renormalization.

    A crash or an explicitly contact-free round earns no doctrine credit,
    including an empty objective capture (base fitness can still reward it).
    """
    terms = cfg.get('terms', [])
    measured, missing, fraction = {}, [], 0.0
    for term in terms:
        metric = term['metric']
        value = measurement(row, metric)
        if value is None:
            missing.append(metric)
            continue
        measured[metric] = value
        fraction += max(0.0, float(term['weight'])) * max(0.0, min(value / max(1e-6, float(term['cap'])), 1.0))
    bonus = max(0.0, min(float(cfg.get('weight', 0)), 1.0)) * min(1.0, fraction)
    if row.get('crashed') or row.get('contact') is False:
        bonus = 0.0
    return {'bonus': bonus, 'measured': measured, 'missing': missing}
