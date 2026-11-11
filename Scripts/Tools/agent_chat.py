#!/usr/bin/env python3
"""
Local agent chat: Claude <-> Gemini <-> Designer.
Storage: Saved/AgentChat/chat.jsonl in the main checkout or --root override.
Standard library only.
"""

import argparse
import contextlib
import datetime
import json
import os
import sys
import threading
import time

MAIN_DEFAULT_DIR = r"C:\Users\Zephyrus15Duo\Documents\Unreal Projects\CodexTactics"

def get_storage_dir(root_override=None):
    if root_override:
        if os.path.basename(os.path.normpath(root_override)).lower() == "agentchat":
            path = root_override
        else:
            path = os.path.join(root_override, "Saved", "AgentChat")
    else:
        # Resolve relative to script location if under a CodexTactics checkout/worktree
        script_dir = os.path.dirname(os.path.abspath(__file__))
        project_root = os.path.abspath(os.path.join(script_dir, "..", ".."))
        # If running from a worktree like CodexTactics-gemchat, default to main checkout if it exists
        if os.path.exists(MAIN_DEFAULT_DIR):
            path = os.path.join(MAIN_DEFAULT_DIR, "Saved", "AgentChat")
        else:
            path = os.path.join(project_root, "Saved", "AgentChat")
    os.makedirs(path, exist_ok=True)
    return path

@contextlib.contextmanager
def file_lock(lock_path, timeout=15.0, retry_interval=0.02):
    t0 = time.time()
    acquired = False
    while not acquired:
        try:
            fd = os.open(lock_path, os.O_CREAT | os.O_EXCL | os.O_RDWR)
            try:
                os.write(fd, str(os.getpid()).encode("utf-8"))
            finally:
                os.close(fd)
            acquired = True
            break
        except FileExistsError:
            try:
                mtime = os.path.getmtime(lock_path)
                if time.time() - mtime > 20.0:
                    try:
                        os.remove(lock_path)
                        continue
                    except OSError:
                        pass
            except OSError:
                pass
            if time.time() - t0 > timeout:
                raise TimeoutError(f"Could not acquire lock {lock_path} within {timeout}s")
            time.sleep(retry_interval)
    try:
        yield
    finally:
        try:
            os.remove(lock_path)
        except OSError:
            pass

def read_all_messages(chat_file):
    messages = []
    if not os.path.exists(chat_file):
        return messages
    with open(chat_file, "r", encoding="utf-8", errors="replace") as f:
        for line in f:
            line = line.strip()
            if not line:
                continue
            try:
                messages.append(json.loads(line))
            except json.JSONDecodeError:
                continue
    return messages

def get_last_seen_id(storage_dir, who):
    path = os.path.join(storage_dir, f"seen_{who}.json")
    if os.path.exists(path):
        try:
            with open(path, "r", encoding="utf-8") as f:
                data = json.load(f)
                return int(data.get("last_seen_id", 0))
        except (json.JSONDecodeError, OSError, ValueError):
            return 0
    return 0

def set_last_seen_id(storage_dir, who, last_id):
    path = os.path.join(storage_dir, f"seen_{who}.json")
    tmp_path = path + f".tmp.{os.getpid()}"
    try:
        with open(tmp_path, "w", encoding="utf-8") as f:
            json.dump({"last_seen_id": last_id, "ts": datetime.datetime.now(datetime.timezone.utc).isoformat()}, f)
        os.replace(tmp_path, path)
    except OSError:
        try:
            if os.path.exists(tmp_path):
                os.remove(tmp_path)
        except OSError:
            pass

def set_presence(storage_dir, who, active=True):
    path = os.path.join(storage_dir, f"presence_{who}.json")
    if active:
        tmp_path = path + f".tmp.{os.getpid()}"
        try:
            with open(tmp_path, "w", encoding="utf-8") as f:
                json.dump({"waiting_since": datetime.datetime.now(datetime.timezone.utc).isoformat(), "pid": os.getpid()}, f)
            os.replace(tmp_path, path)
        except OSError:
            try:
                if os.path.exists(tmp_path):
                    os.remove(tmp_path)
            except OSError:
                pass
    else:
        try:
            if os.path.exists(path):
                os.remove(path)
        except OSError:
            pass

def get_presence_dict(storage_dir):
    result = {}
    now = time.time()
    for who in ("claude", "gemini", "codex", "designer"):
        path = os.path.join(storage_dir, f"presence_{who}.json")
        is_present = False
        waiting_since = None
        if os.path.exists(path):
            try:
                mtime = os.path.getmtime(path)
                if now - mtime < 3600:
                    with open(path, "r", encoding="utf-8") as f:
                        data = json.load(f)
                        waiting_since = data.get("waiting_since")
                        is_present = True
                else:
                    try:
                        os.remove(path)
                    except OSError:
                        pass
            except (json.JSONDecodeError, OSError):
                pass
        result[who] = {"active": is_present, "waiting_since": waiting_since}
    return result

def check_topic_limit(messages, topic, sender, max_per_topic=10):
    if sender == "designer":
        return True
    norm_topic = topic.strip().lower()
    count = 0
    for msg in reversed(messages):
        msg_topic = msg.get("topic", "").strip().lower()
        if msg_topic != norm_topic:
            continue
        msg_from = msg.get("from", "").lower()
        if msg_from == "designer":
            break
        if msg_from in ("claude", "gemini", "codex"):
            count += 1
    return count < max_per_topic

def post_message(storage_dir, sender, to, topic, text, max_per_topic=10):
    sender = sender.strip().lower()
    to = to.strip().lower()
    if sender not in ("claude", "gemini", "codex", "designer"):
        raise ValueError(f"Invalid sender: {sender} (must be claude, gemini, or designer)")
    if to not in ("claude", "gemini", "codex", "all"):
        raise ValueError(f"Invalid recipient: {to} (must be claude, gemini, or all)")

    lock_file = os.path.join(storage_dir, "chat.lock")
    chat_file = os.path.join(storage_dir, "chat.jsonl")

    with file_lock(lock_file):
        messages = read_all_messages(chat_file)
        if not check_topic_limit(messages, topic, sender, max_per_topic):
            print("TOPIC_LIMIT: ask the designer", file=sys.stderr)
            return None

        last_id = messages[-1]["id"] if messages else 0
        new_id = last_id + 1
        msg = {
            "id": new_id,
            "ts": datetime.datetime.now(datetime.timezone.utc).isoformat(),
            "from": sender,
            "to": to,
            "topic": topic.strip(),
            "text": text
        }
        with open(chat_file, "a", encoding="utf-8") as f:
            f.write(json.dumps(msg, ensure_ascii=False) + "\n")
        
        # Sender has seen up to this id
        curr_seen = get_last_seen_id(storage_dir, sender)
        if new_id > curr_seen:
            set_last_seen_id(storage_dir, sender, new_id)

    return new_id

def format_message(msg):
    ts = msg.get("ts", "")
    if "T" in ts:
        ts = ts.split(".")[0].replace("T", " ")
    topic_str = f"[{msg.get('topic')}] " if msg.get("topic") else ""
    return f"[#{msg.get('id')} {ts}] {topic_str}{msg.get('from')} -> {msg.get('to')}:\n{msg.get('text')}"

def cmd_post(args):
    storage_dir = get_storage_dir(args.root)
    text = args.text
    if args.file:
        with open(args.file, "r", encoding="utf-8") as f:
            text = f.read()
    if text is None:
        print("Error: --text or --file is required", file=sys.stderr)
        return 1

    msg_id = post_message(storage_dir, args.sender, args.to, args.topic or "", text, args.max_per_topic)
    if msg_id is None:
        return 1
    print(msg_id)
    return 0

def cmd_read(args):
    storage_dir = get_storage_dir(args.root)
    chat_file = os.path.join(storage_dir, "chat.jsonl")
    messages = read_all_messages(chat_file)

    since_id = args.since
    if since_id is None and args.sender:
        since_id = get_last_seen_id(storage_dir, args.sender)
    
    if since_id is not None:
        messages = [m for m in messages if m.get("id", 0) > since_id]

    if args.last and args.last > 0:
        messages = messages[-args.last:]

    if args.sender and messages:
        max_id = max(m.get("id", 0) for m in messages)
        curr_seen = get_last_seen_id(storage_dir, args.sender)
        if max_id > curr_seen:
            set_last_seen_id(storage_dir, args.sender, max_id)

    if args.json:
        print(json.dumps(messages, ensure_ascii=False, indent=2))
    else:
        for msg in messages:
            print(format_message(msg))
            print("-" * 40)
    return 0

def cmd_wait(args):
    storage_dir = get_storage_dir(args.root)
    who = args.sender.strip().lower()
    if who not in ("claude", "gemini", "codex", "designer"):
        print(f"Error: Invalid participant: {who}", file=sys.stderr)
        return 1

    chat_file = os.path.join(storage_dir, "chat.jsonl")
    timeout = args.timeout
    poll_interval = args.poll_interval
    t0 = time.time()

    set_presence(storage_dir, who, True)
    try:
        while True:
            last_seen = get_last_seen_id(storage_dir, who)
            messages = read_all_messages(chat_file)
            
            # Find messages addressed to who or all, not from who, with id > last_seen
            new_msgs = [
                m for m in messages
                if m.get("id", 0) > last_seen
                and m.get("from", "").lower() != who
                and m.get("to", "").lower() in (who, "all")
            ]

            if new_msgs:
                max_id = max(m.get("id", 0) for m in new_msgs)
                set_last_seen_id(storage_dir, who, max_id)
                if args.json:
                    print(json.dumps(new_msgs, ensure_ascii=False, indent=2))
                else:
                    for msg in new_msgs:
                        print(format_message(msg))
                        print("-" * 40)
                return 0

            if time.time() - t0 >= timeout:
                print("NO_MESSAGES")
                return 0

            time.sleep(poll_interval)
    finally:
        set_presence(storage_dir, who, False)

def cmd_presence(args):
    storage_dir = get_storage_dir(args.root)
    pres = get_presence_dict(storage_dir)
    if args.json:
        print(json.dumps(pres, ensure_ascii=False, indent=2))
    else:
        for who, data in pres.items():
            status = "listening" if data["active"] else "offline"
            since = f" (since {data['waiting_since']})" if data["waiting_since"] else ""
            print(f"{who.capitalize()}: {status}{since}")
    return 0

def run_selftest():
    import tempfile
    import shutil

    temp_dir = tempfile.mkdtemp(prefix="agent_chat_test_")
    print(f"Running self-test in {temp_dir}...")
    try:
        storage_dir = get_storage_dir(temp_dir)
        chat_file = os.path.join(storage_dir, "chat.jsonl")

        # 1. Cyrillic round-trip
        ru_text = "Привет, Клод! Проверка кодировки: съешь же ещё этих мягких французских булок, да выпей чаю. 123!"
        m1_id = post_message(storage_dir, "claude", "gemini", "ТестКодировки", ru_text)
        assert m1_id == 1, f"Expected id 1, got {m1_id}"
        msgs = read_all_messages(chat_file)
        assert len(msgs) == 1
        assert msgs[0]["text"] == ru_text, f"Text mismatch: {msgs[0]['text']}"
        assert msgs[0]["topic"] == "ТестКодировки"
        print("  [OK] 1. Cyrillic round-trip")

        # 2. Topic limit
        # Claude and Gemini exchange 9 more messages on "ТестКодировки" (total 10)
        for i in range(2, 11):
            s = "claude" if i % 2 == 1 else "gemini"
            t = "gemini" if s == "claude" else "claude"
            mid = post_message(storage_dir, s, t, "ТестКодировки", f"Ping {i}")
            assert mid == i, f"Expected id {i}, got {mid}"
        
        # 11th message should fail topic limit
        m11_id = post_message(storage_dir, "claude", "gemini", "ТестКодировки", "Overflow ping", max_per_topic=10)
        assert m11_id is None, "Expected topic limit refusal"
        
        # Designer intervenes
        d_id = post_message(storage_dir, "designer", "all", "ТестКодировки", "Designer guidance")
        assert d_id == 11, f"Expected id 11, got {d_id}"

        # Now Claude can post again
        m12_id = post_message(storage_dir, "claude", "gemini", "ТестКодировки", "Post-guidance ping", max_per_topic=10)
        assert m12_id == 12, f"Expected id 12, got {m12_id}"
        print("  [OK] 2. Topic limit & designer reset")

        # 3. Two threads waiting as claude / gemini
        # Reset seen IDs to test waiting
        set_last_seen_id(storage_dir, "claude", 12)
        set_last_seen_id(storage_dir, "gemini", 12)

        results = {"claude": [], "gemini": []}
        errors = []

        def wait_worker(who):
            try:
                set_presence(storage_dir, who, True)
                t_start = time.time()
                while time.time() - t_start < 5.0:
                    last_seen = get_last_seen_id(storage_dir, who)
                    all_m = read_all_messages(chat_file)
                    new_m = [
                        m for m in all_m
                        if m.get("id", 0) > last_seen
                        and m.get("from", "").lower() != who
                        and m.get("to", "").lower() in (who, "all")
                    ]
                    if new_m:
                        set_last_seen_id(storage_dir, who, max(m["id"] for m in new_m))
                        results[who].extend(new_m)
                        return
                    time.sleep(0.05)
            except Exception as e:
                errors.append((who, e))
            finally:
                set_presence(storage_dir, who, False)

        # 3a. Post addressed to claude -> wakes claude only
        t_claude = threading.Thread(target=wait_worker, args=("claude",))
        t_gemini = threading.Thread(target=wait_worker, args=("gemini",))
        t_claude.start()
        t_gemini.start()
        time.sleep(0.1)

        post_message(storage_dir, "designer", "claude", "Direct", "Only for Claude")
        t_claude.join(timeout=3.0)
        assert len(results["claude"]) == 1, "Claude should receive 1 message"
        assert results["claude"][0]["text"] == "Only for Claude"
        assert len(results["gemini"]) == 0, "Gemini should NOT receive Claude's message"
        t_gemini.join(timeout=3.0)
        print("  [OK] 3a. Direct message wakes only target")

        # 3b. Post addressed to all -> wakes both
        results["claude"] = []
        results["gemini"] = []
        t_claude2 = threading.Thread(target=wait_worker, args=("claude",))
        t_gemini2 = threading.Thread(target=wait_worker, args=("gemini",))
        t_claude2.start()
        t_gemini2.start()
        time.sleep(0.1)

        post_message(storage_dir, "designer", "all", "Broadcast", "Hello everyone")
        t_claude2.join(timeout=3.0)
        t_gemini2.join(timeout=3.0)
        assert len(results["claude"]) == 1 and results["claude"][0]["text"] == "Hello everyone", "Claude received all"
        assert len(results["gemini"]) == 1 and results["gemini"][0]["text"] == "Hello everyone", "Gemini received all"
        print("  [OK] 3b. 'all' broadcast wakes both")

        # 4. Concurrent posts stress test: 50 concurrent posts survive locking
        post_errors = []
        def concurrent_poster(idx):
            try:
                post_message(storage_dir, "designer", "all", f"Stress{idx%3}", f"Concurrent payload {idx}")
            except Exception as e:
                post_errors.append((idx, e))

        threads = [threading.Thread(target=concurrent_poster, args=(i,)) for i in range(50)]
        for t in threads:
            t.start()
        for t in threads:
            t.join()

        assert not post_errors, f"Concurrent post errors: {post_errors}"
        all_msgs = read_all_messages(chat_file)
        # Expected total: 12 + 1 (direct) + 1 (broadcast) + 50 = 64
        assert len(all_msgs) == 64, f"Expected 64 messages, found {len(all_msgs)}"
        # Verify strictly increasing IDs 1..64
        ids = [m["id"] for m in all_msgs]
        assert ids == list(range(1, 65)), f"Message IDs gap or ordering violation: {ids}"
        print("  [OK] 4. Lock survives 50 concurrent posts with zero gaps")

        print("ALL SELFTESTS PASSED")
        return 0
    finally:
        shutil.rmtree(temp_dir, ignore_errors=True)

def main():
    parser = argparse.ArgumentParser(description="CodexTactics Agent Chat")
    parser.add_argument("--root", help="Custom storage root directory (defaults to main checkout)")
    subparsers = parser.add_subparsers(dest="command")

    # post
    p_post = subparsers.add_parser("post", help="Post a message")
    p_post.add_argument("--as", dest="sender", required=True, choices=["claude", "gemini", "codex", "designer"])
    p_post.add_argument("--to", required=True, choices=["claude", "gemini", "codex", "all"])
    p_post.add_argument("--topic", default="")
    p_post.add_argument("--text")
    p_post.add_argument("--file")
    p_post.add_argument("--max-per-topic", type=int, default=10)

    # read
    p_read = subparsers.add_parser("read", help="Read messages")
    p_read.add_argument("--as", dest="sender", choices=["claude", "gemini", "codex", "designer"])
    p_read.add_argument("--since", type=int)
    p_read.add_argument("--last", type=int)
    p_read.add_argument("--json", action="store_true")

    # wait
    p_wait = subparsers.add_parser("wait", help="Wait for incoming messages")
    p_wait.add_argument("--as", dest="sender", required=True, choices=["claude", "gemini", "codex", "designer"])
    p_wait.add_argument("--timeout", type=float, default=1800.0)
    p_wait.add_argument("--poll-interval", type=float, default=2.0)
    p_wait.add_argument("--json", action="store_true")

    # presence
    p_pres = subparsers.add_parser("presence", help="Show active listeners")
    p_pres.add_argument("--json", action="store_true")

    # selftest
    subparsers.add_parser("selftest", help="Run automated test suite")

    args = parser.parse_args()
    if args.command == "post":
        return cmd_post(args)
    elif args.command == "read":
        return cmd_read(args)
    elif args.command == "wait":
        return cmd_wait(args)
    elif args.command == "presence":
        return cmd_presence(args)
    elif args.command == "selftest":
        return run_selftest()
    else:
        parser.print_help()
        return 1

if __name__ == "__main__":
    sys.exit(main() or 0)
