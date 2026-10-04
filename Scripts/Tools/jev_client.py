"""Shared TypeSafe (Jev System One) client for the project's tools (typesafe_triage.py, jev_digest.py).

The key: TYPESAFE_API_KEY from the environment, else the Windows user environment in the registry (a key set with setx
after the shell started is only there), else Saved/Config/typesafe.key. Never printed.
ask() returns (answers, error): no key / API failure -> (None, reason); callers decide (never invent answers).
Usage per request is printed on stderr so the caller's stdout stays a clean brief.
"""

import json
import os
import sys
import time
import urllib.error
import urllib.request

ROOT = os.path.abspath(os.path.join(os.path.dirname(__file__), "..", ".."))
API_URL = "https://api.typesafe.ai/v1/systemone"
MODEL = "jev-latest"


def api_key():
    key = os.environ.get("TYPESAFE_API_KEY", "").strip()
    if not key and sys.platform == "win32":
        try:
            import winreg
            with winreg.OpenKey(winreg.HKEY_CURRENT_USER, "Environment") as reg:
                key = str(winreg.QueryValueEx(reg, "TYPESAFE_API_KEY")[0]).strip()
        except OSError:
            pass
    path = os.path.join(ROOT, "Saved", "Config", "typesafe.key")
    if not key and os.path.exists(path):
        with open(path, encoding="utf-8") as f:
            key = f.read().strip()
    return key


def ask(state, questions, attempts=4, timeout=60):
    key = api_key()
    if not key:
        return None, "no TypeSafe API key"
    body = json.dumps({"state": state, "model": MODEL, "questions": questions}).encode("utf-8")
    for attempt in range(attempts):
        request = urllib.request.Request(API_URL, data=body, method="POST",
                                         headers={"Authorization": "Bearer " + key, "Content-Type": "application/json"})
        try:
            with urllib.request.urlopen(request, timeout=timeout) as response:
                data = json.loads(response.read().decode("utf-8"))
                usage = data.get("usage", {})
                print("[Jev] %s: %d questions, %s in / %s out tokens" % (data.get("model"), len(questions),
                      usage.get("input_tokens", "?"), usage.get("output_tokens", "?")), file=sys.stderr)
                return data.get("answers", {}), None
        except urllib.error.HTTPError as error:
            if error.code in (429, 529) and attempt + 1 < attempts:
                time.sleep(2 ** attempt)
                continue
            return None, "HTTP %d: %s" % (error.code, error.read().decode("utf-8", "replace")[:200])
        except Exception as error:  # network, timeout
            return None, str(error)
    return None, "retries exhausted"


def choice(answers, qid, default=None):
    answer = (answers or {}).get(qid) or {}
    return answer.get("choice", default), answer.get("confidence", 0.0)


def noul(answers, qid, default=None):
    return ((answers or {}).get(qid) or {}).get("noul", default)


def score(answers, qid, default=None):
    return ((answers or {}).get(qid) or {}).get("score", default)
