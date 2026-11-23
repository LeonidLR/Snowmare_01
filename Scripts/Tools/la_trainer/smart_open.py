"""Local-file stand-in for the `smart_open` package (Learning Agents probe P0, 2026-11-23).

UE 5.8's LearningCore trainer (`Engine/Plugins/Experimental/LearningCore/Content/Python/learning_core/train_common.py`) does
`from smart_open import open` at import time, although the shared-memory PPO trainer only opens local files with it. The PMC
arena (`CodexTactics.PmcArena Policy=train`, `Source/CodexTacticsDev/Private/Debug/PmcPolicyTraining.cpp`) puts THIS folder on the
trainer process's PYTHONPATH only when the trainer's Python cannot import the real package, so a venv with just torch + numpy
can train. Installing the real `smart_open` into that Python makes the arena ignore this file.

Not a general replacement: no S3 / HTTP / compression. docs/systems/pmc-policy-probe.md.
"""

from builtins import open  # noqa: F401  (re-exported: `from smart_open import open`)

__all__ = ["open", "parse_uri"]


def parse_uri(uri_as_string):
    """Only the plugin's File communicator (S3 / file URIs) calls this; the arena trains over shared memory."""
    raise NotImplementedError(
        "smart_open stand-in (Scripts/Tools/la_trainer/smart_open.py): parse_uri is not available - "
        "install the real smart_open package into the trainer's Python")
