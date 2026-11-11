import copy
import contextlib
import io
import json
import tempfile
import sys
import time
import unittest
from unittest.mock import patch
import model_router as router

CONFIG = {'provider': 'codex', 'enabled': False, 'confidence_threshold': .6, 'timeout_seconds': .01,
          'reasoning_effort': None, 'models': router.MODELS.copy()}
TASK = 'Implement the requested gameplay feature carefully'


def answers(model='terra', level='2', confidence=.8, followup=0):
    return ({'model': {'choice': model, 'probabilities': {model: confidence}},
             'level': {'choice': level, 'probabilities': {level: .9}}, 'followup': {'noul': followup}}, None)


class RouterTests(unittest.TestCase):
    def classify(self, response):
        with patch('jev_client.ask', return_value=response):
            return router.classify(TASK, CONFIG)

    def test_all_models_and_independent_level(self):
        for name, model_id in router.MODELS.items():
            with self.subTest(name=name):
                result = self.classify(answers(name, '2'))
                self.assertTrue(result['route'])
                self.assertEqual((result['model_id'], result['level']), (model_id, '2'))

    def test_low_confidence_preserves_recommendation_without_routing(self):
        result = self.classify(answers(confidence=.57))
        self.assertFalse(result['route'])
        self.assertEqual(result['model_id'], router.MODELS['terra'])
        self.assertIn('current session', result['recommendation'])

    def test_invalid_choice_and_config(self):
        self.assertTrue(self.classify(answers('other'))['error'])
        config = copy.deepcopy(CONFIG)
        config['models']['terra'] = 'unknown'
        self.assertFalse(router.classify(TASK, config)['route'])

    def test_error_and_timeout(self):
        self.assertTrue(self.classify((None, 'unavailable'))['error'])
        with patch('jev_client.ask', side_effect=lambda *args, **kwargs: time.sleep(1)):
            result = router.classify(TASK, CONFIG, timeout=.01)
        self.assertTrue(result['error'])
        self.assertFalse(result['route'])

    def test_bad_probabilities_and_exception(self):
        for bad in (float('nan'), -0.1, 1.1):
            self.assertTrue(self.classify(answers(confidence=bad))['error'])
        with patch('jev_client.ask', side_effect=RuntimeError('failed')):
            result = router.classify(TASK, CONFIG)
        self.assertTrue(result['error'])
        self.assertFalse(result['route'])

    def test_long_substantive_prompt_is_not_text_followup(self):
        with patch('jev_client.ask', return_value=answers()) as ask:
            result = router.classify('Это задача по реализации подробной новой системы с множеством важных требований и проверок', CONFIG)
        ask.assert_called_once()
        self.assertTrue(result['route'])

    def test_followups(self):
        with patch('jev_client.ask') as ask:
            self.assertTrue(router.classify('ok', CONFIG)['followup'])
            ask.assert_not_called()
        self.assertTrue(self.classify(answers(followup=.8))['followup'])

    def test_hook_on_off_and_failed_recommendation(self):
        for enabled in (False, True):
            config = dict(CONFIG, enabled=enabled)
            with self.subTest(enabled=enabled), patch.object(router, 'load_config', return_value=config), \
                 patch('sys.stdin', io.StringIO(json.dumps({'prompt': TASK}))), \
                 patch('jev_client.ask', return_value=(None, 'unavailable')) as ask, \
                 contextlib.redirect_stdout(io.StringIO()) as output:
                self.assertEqual(router.main(['--hook']), 0)
                self.assertEqual(ask.called, enabled)
                if enabled:
                    note = json.loads(output.getvalue())['hookSpecificOutput']['additionalContext']
                    self.assertIn('current session', note)
                    self.assertIn('"route": false', note)
                else:
                    self.assertEqual(output.getvalue(), '')

    def test_cli_switches_and_invalid_config(self):
        with tempfile.TemporaryDirectory() as tmp:
            path = __import__('os').path.join(tmp, 'config.json')
            with open(path, 'w', encoding='utf-8') as f:
                json.dump(CONFIG, f)
            with patch.object(router, 'CONFIG', path), contextlib.redirect_stdout(io.StringIO()):
                self.assertEqual(router.main(['--on']), 0)
                self.assertTrue(router.load_config()['enabled'])
                self.assertEqual(router.main(['--off']), 0)
                self.assertFalse(router.load_config()['enabled'])
                with contextlib.redirect_stderr(io.StringIO()) as errors:
                    self.assertEqual(router.main(['--unknown']), 1)
                self.assertFalse(json.loads(errors.getvalue())['route'])
                bad = dict(CONFIG, models={'unknown': 'gpt-unknown'})
                with open(path, 'w', encoding='utf-8') as f:
                    json.dump(bad, f)
                with contextlib.redirect_stderr(io.StringIO()) as errors:
                    self.assertEqual(router.main(['--hook']), 1)
                self.assertFalse(json.loads(errors.getvalue())['route'])


if __name__ == '__main__':
    unittest.main()
