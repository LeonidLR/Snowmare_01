"""Round mark selection and resolution shared by offline consumers."""


def choose_round(records, now, previous=False, explicit=None):
    current, prior, started, active = None, None, 0, False
    for row in records:
        if row.get('t', 0) > now:
            continue
        kind = row.get('type')
        if kind not in ('round_start', 'round_end'):
            continue
        if explicit is not None and row.get('round') == explicit:
            return explicit
        if kind == 'round_start':
            prior, current, started, active = current, row['round'], row.get('t', 0), True
        else:
            current, active = row['round'], False
    if explicit is not None:
        return None
    if not active:
        return current
    contact = any(started <= r.get('t', 0) <= now and (
        r.get('type') in ('shot', 'shot_hit', 'damage', 'sighting')
        or bool(r.get('threats'))
        or any(op.get('seen_by_enemy', 0) > 0 for op in r.get('ops', []))) for r in records)
    if previous or (now - started < 30 and not contact):
        if prior is not None:
            return prior
        if previous:
            return None
    return current


def round_marks(records):
    """Late marks resolve by their explicit round, never by event timestamp.

    Once event marks exist the footer is only a compatibility mirror, so it
    must not label unmarked rounds. Repeated marks keep the latest value.
    """
    marks = [r for r in records if r.get('type') == 'round_mark']
    rounds = {str(r.get('round', '')) for r in records if r.get('type') in ('round_start', 'round_end')}
    if not rounds:
        rounds = {'legacy'}
    if marks:
        result = {rid: '' for rid in rounds}
        for row in marks:
            result[str(row['round'])] = str(row.get('value', ''))
        return result
    footer = next((r for r in reversed(records) if r.get('type') == 'footer'), {})
    return {rid: str(footer.get('mark', '')) for rid in rounds}
