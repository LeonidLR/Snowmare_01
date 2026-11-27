"""Shared canonical metrics, including read compatibility for saved legacy rows."""
import math


def damage_dealt(row):
    # An explicit new measurement wins, including zero. Do not replace invalid
    # new evidence with the legacy proxy (which includes unattributed HP loss).
    value = row.get('damage_dealt') if 'damage_dealt' in row else row.get('opp_hp_lost')
    if isinstance(value, (int, float)) and not isinstance(value, bool) and math.isfinite(value):
        return float(value)
    return None
