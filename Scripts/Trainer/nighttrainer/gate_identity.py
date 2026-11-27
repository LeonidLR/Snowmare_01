"""Bind gate evidence to the exact semantic config and frozen source revision."""
import argparse
import hashlib
import json
from pathlib import Path
import subprocess
import time


def config_hash(config):
    return hashlib.sha256(json.dumps(config, sort_keys=True, separators=(',', ':'),
                                     allow_nan=False).encode('utf-8')).hexdigest()


def freeze_sha(repo):
    return subprocess.check_output(['git', '-C', str(repo), 'rev-parse', 'HEAD'],
                                   text=True, stderr=subprocess.DEVNULL).strip()


def identity(config, sha):
    return {'config_sha256': config_hash(config), 'freeze_sha': sha}


def validate_pass(path, config, sha, now=None):
    doc = json.loads(Path(path).read_text(encoding='utf-8-sig'))
    age = (time.time() if now is None else now) - float(doc.get('created_unix', 0))
    if doc.get('passed') is not True or not 0 <= age <= 86400:
        raise ValueError('Training gate missing pass status or expired (24h)')
    if doc.get('identity') != identity(config, sha):
        raise ValueError('Training gate config hash / freeze SHA mismatch; refresh gate')
    return doc


if __name__ == '__main__':
    p = argparse.ArgumentParser(description=__doc__)
    p.add_argument('--gate', required=True)
    p.add_argument('--config', required=True)
    p.add_argument('--repo', required=True)
    a = p.parse_args()
    validate_pass(a.gate, json.loads(Path(a.config).read_text(encoding='utf-8-sig')), freeze_sha(a.repo))
