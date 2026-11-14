# Chat Idle Banner Implementation Report

**Author:** Gemini (Independent Port Lead)  
**Date:** 2026-11-11  
**Task:** `chat-idle-banner` (Tooling, Designer Decision B)  
**Branch:** `gemini/chat-idle-banner` from `interim/2026-11-12` (`CodexTactics-gemc5`)  
**Scope:** Server-side idle tracking in `Scripts/Tools/agent_chat.py` & `agent_chat_server.py`, UI red banner in `Tools/AgentChat/index.html`, and unit test suite in `Scripts/Tools/tests/test_agent_chat_idle.py`.

---

## 1. Idle Tracking Algorithm

As requested, for each participant (`gemini`, `codex`, `claude`):
1. Determine the participant's latest post index $idx_{\text{last}}$ in the chat history.
2. Scan for incoming messages addressing the participant ($(to == p \lor to == \text{"all"}) \land from \neq p$) that occurred **after** $idx_{\text{last}}$ (i.e. with no subsequent reply from that participant).
3. Identify the **oldest** such unanswered message $m_{\text{oldest\_unanswered}}$ and its timestamp $T$.
4. Calculate idle duration:
   $$\text{idle}[p] = \max(0, \lfloor \text{now} - T \rfloor)$$
   If the participant has answered all directed messages, or has never received any messages after their last post, $\text{idle}[p] = 0$.

Implemented in `Scripts/Tools/agent_chat.py`:
- `parse_iso_timestamp(ts_str)`
- `compute_idle_seconds(messages, participants, now)`
- `get_idle_dict(storage_dir, participants, now)`

---

## 2. API Integration in `agent_chat_server.py`

- **Endpoints updated:**
  * `/api/messages`: Returns `{"messages": [...], "idle": {"claude": sec, "gemini": sec, "codex": sec}}`.
  * `/api/presence`: Returns `{"claude": {...}, "gemini": {...}, "codex": {...}, "designer": {...}, "idle": {...}}`.
  * `/api/idle`: Dedicated endpoint returning `{"idle": {...}}`.
- **Preservation of Same-Origin Checks:**
  `same_origin_only` method remains untouched and enforced; unauthorized cross-origin requests continue to receive `403 Forbidden`.
- **Process Safety:**
  No running server process was restarted or terminated.

---

## 3. UI Red Alert Banner in `Tools/AgentChat/index.html`

- Added `#idle-banners-container` positioned immediately below `<header>` and above the chat scroll area.
- Banner condition: When `idle[name] > 600` seconds (10 minutes):
  * Renders a styled red alert banner: `'<Имя> спит N мин'`, where $N = \lfloor\text{seconds} / 60\rfloor$.
  * Styled with high-contrast red background (`#ef4444` / `#dc2626`), warning icon `⚠️`, bold typography, and subtle pulse animation (`@keyframes idle-pulse`).
  * If `idle[name] <= 600`, the banner is hidden automatically.
- Integrated into both `fetchPresence()` and `fetchMessages()` polling loops (every 2 seconds).

---

## 4. Test Suite in `Scripts/Tools/tests/test_agent_chat_idle.py`

8 unit tests covering:
1. `test_empty_messages`: Zero idle on empty chat.
2. `test_single_direct_message`: Transition across 600s boundary (300s $\implies$ active, 700s $\implies$ sleeping).
3. `test_answered_message_resets_idle`: Reply immediately clears idle to 0.
4. `test_oldest_unanswered_message_selected`: Oldest unanswered message drives elapsed time.
5. `test_broadcast_message_to_all`: Poster is not idle from their own post; recipients track time.
6. `test_partial_replies_to_broadcast`: Selective replies clear responding agent while others continue sleeping.
7. `test_get_idle_dict_from_disk`: End-to-end reading from temporary storage directory with `chat.jsonl`.
8. `test_handler_endpoints`: Ephemeral HTTP server verifying `/api/messages`, `/api/presence`, `/api/idle` response payload and same-origin rejection (403 on foreign origin).

All 8 tests PASS (`python -m unittest discover -s Scripts/Tools/tests -p "test_*.py"`).
