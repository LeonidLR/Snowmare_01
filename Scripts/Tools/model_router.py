"""Advisory Jev router for Codex. Never launches a model or delegates a task."""
import json
import math
import os
import re
import sys
import threading

ROOT = os.path.abspath(os.path.join(os.path.dirname(__file__), '..', '..'))
CONFIG = os.path.join(ROOT, '.claude', 'model_router.json')
MODELS = {'luna': 'gpt-6-luna', 'sol': 'gpt-6.1-sol', 'terra': 'gpt-6.1-terra', 'astra': 'gpt-6-astra'}
CRITERIA = {'luna': 'Simple, bounded tasks', 'sol': 'General coding work',
            'terra': 'Complex implementation and debugging', 'astra': 'Demanding design and reasoning'}
LEVELS = {'1': 'Trivial', '2': 'Routine', '3': 'Major', '4': 'Hardest'}
FOLLOWUP = re.compile(r'^\s*(ок|ok|окей|да|нет|ага|угу|давай|продолжай|дальше|ещё|еще|короче|запушь|закоммить|повтори|почему|это|так|хорошо|спасибо|отлично)\b[\s\S]{0,40}$', re.I)


def load_config():
    with open(CONFIG, encoding='utf-8') as f:
        config = json.load(f)
    validate_config(config)
    return config


def validate_config(config):
    if config.get('provider') != 'codex' or config.get('models') != MODELS:
        raise ValueError('config must contain the exact Codex model allowlist')
    threshold = config.get('confidence_threshold')
    timeout = config.get('timeout_seconds')
    if isinstance(threshold, bool) or not isinstance(threshold, (int, float)) or not 0 <= threshold <= 1:
        raise ValueError('invalid confidence threshold')
    if isinstance(timeout, bool) or not isinstance(timeout, (int, float)) or not math.isfinite(timeout) or timeout <= 0:
        raise ValueError('invalid timeout')
    effort = config.get('reasoning_effort')
    if effort is not None and effort not in ('low', 'medium', 'high', 'xhigh', 'max'):
        raise ValueError('invalid reasoning effort')


def classify(text, config, timeout=None):
    result = {'level': None, 'level_label': None, 'model': None, 'model_id': None,
              'reasoning_effort': None, 'confidence': 0.0, 'probabilities': {},
              'level_probabilities': {}, 'route': False, 'recommendation': 'Keep the task in the current session.', 'reason': ''}
    try:
        validate_config(config)
        if len(text.split()) <= 3 or FOLLOWUP.match(text):
            result.update(reason='Short follow-up needs conversation context.', followup=True)
            return result
        import jev_client
        timeout = config['timeout_seconds'] if timeout is None else timeout
        questions = {
            'model': {'type': 'choice', 'instructions': 'Choose the best Codex executor for this task independently of complexity level.', 'criteria': CRITERIA},
            'level': {'type': 'choice', 'instructions': 'Classify task complexity independently for reporting.', 'criteria': LEVELS},
            'followup': {'type': 'noul', 'instructions': 'Is this a contextual follow-up that cannot be understood without previous conversation?'}}
        box = {}
        def call():
            try:
                box['answer'] = jev_client.ask({'project': 'Unreal Engine tactical game', 'request': text[:4000]}, questions, attempts=1, timeout=timeout)
            except Exception as error:
                box['exception'] = str(error)
        worker = threading.Thread(target=call, daemon=True)
        worker.start()
        worker.join(timeout + 0.5)
        if 'exception' in box:
            raise RuntimeError(box['exception'])
        if 'answer' not in box:
            raise TimeoutError('Jev timed out')
        answers, error = box['answer']
        if answers is None:
            raise RuntimeError(str(error))
        if jev_client.noul(answers, 'followup', 0.0) >= 0.6:
            result.update(reason='Jev identified a context-dependent follow-up.', followup=True)
            return result
        model_answer = answers.get('model') or {}
        level_answer = answers.get('level') or {}
        model, level = model_answer.get('choice'), str(level_answer.get('choice'))
        if model not in MODELS or level not in LEVELS:
            raise ValueError('invalid Jev model or complexity choice')
        def probabilities(answer, allowed):
            values = answer.get('probabilities') or {}
            if not isinstance(values, dict) or any(k not in allowed for k in values):
                raise ValueError('invalid probability labels')
            if any(isinstance(v, bool) or not isinstance(v, (float, int)) or not math.isfinite(v) or not 0 <= v <= 1 for v in values.values()):
                raise ValueError('invalid probability')
            return values
        probs = probabilities(model_answer, MODELS)
        level_probs = probabilities(level_answer, LEVELS)
        confidence = probs.get(model, model_answer.get('confidence', 0.0))
        if isinstance(confidence, bool) or not isinstance(confidence, (int, float)) or not math.isfinite(confidence) or not 0 <= confidence <= 1:
            raise ValueError('invalid confidence')
        result.update(level=level, level_label=LEVELS[level], model=model, model_id=MODELS[model],
                      reasoning_effort=config.get('reasoning_effort'), confidence=confidence,
                      probabilities=probs, level_probabilities=level_probs)
        if confidence >= config['confidence_threshold']:
            result.update(route=True, recommendation='Consider delegating to %s if delegation is authorized and appropriate.' % MODELS[model], reason='Model confidence meets threshold.')
        else:
            result['reason'] = 'Model confidence below threshold; do not delegate on this recommendation.'
    except Exception as error:
        result.update(route=False, error=True, reason='Router error: %s' % error)
    return result


def main(argv):
    try:
        config = load_config()
        if '--on' in argv or '--off' in argv:
            if '--on' in argv and '--off' in argv:
                raise ValueError('choose either --on or --off')
            config['enabled'] = '--on' in argv
            with open(CONFIG, 'w', encoding='utf-8') as f:
                json.dump(config, f, ensure_ascii=False, indent=2)
                f.write('\n')
            print('Codex router: %s' % ('ON' if config['enabled'] else 'OFF'))
        elif '--status' in argv or not argv:
            print('Codex router: %s; threshold %s' % ('ON' if config['enabled'] else 'OFF', config['confidence_threshold']))
            for name, model in MODELS.items():
                print('%s: %s — %s' % (name, model, CRITERIA[name]))
        elif '--classify' in argv:
            index = argv.index('--classify')
            print(json.dumps(classify(argv[index + 1] if index + 1 < len(argv) else sys.stdin.read(), config), ensure_ascii=False, indent=2))
        elif '--hook' in argv:
            if config['enabled']:
                payload = json.load(sys.stdin)
                result = classify(payload.get('prompt', ''), config)
                note = ('[model-router] %s Result: %s. Codex collaboration.spawn_agent accepts model and reasoning_effort; '
                        'use it only when delegation is explicitly authorized and appropriate. This recommendation does not switch the current model.'
                        % (result['recommendation'], json.dumps(result, ensure_ascii=False)))
                print(json.dumps({'hookSpecificOutput': {'hookEventName': 'UserPromptSubmit', 'additionalContext': note}}, ensure_ascii=False))
        elif '--test' in argv:
            print(json.dumps(classify('Implement a complex gameplay component with robust tests', config), ensure_ascii=False, indent=2))
        else:
            raise ValueError('unknown option')
        return 0
    except Exception as error:
        print(json.dumps({'route': False, 'error': True, 'reason': str(error), 'recommendation': 'Keep the task in the current session.'}), file=sys.stderr)
        return 1


if __name__ == '__main__':
    sys.exit(main(sys.argv[1:]))
