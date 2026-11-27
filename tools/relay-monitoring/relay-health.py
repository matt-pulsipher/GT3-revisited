#!/usr/bin/env python3
"""Prints one JSON object that New Relic Flex reports as a GT3RelaySample event every
minute (newrelic-flex.yml). Runs as root on the relay host. Standard library only.

- Probe: one 64-byte datagram to the relay on 127.0.0.1 with rendezvous version 0xFFFF.
  The relay answers Error(Version) in its own header (so the reply also carries the
  server's protocol version) and counts nothing: no challenge, no version check.
- Stats: the newest "stats:" line from the journal. Since bd51e44 the unit logs in its
  own journal namespace (LogNamespace=gt3r-relay), so this reads
  `journalctl --namespace=gt3r-relay` and falls back to the default journal only when
  that namespace has no recent stats line. Cumulative counters become per-sample
  deltas, so NRQL sums give real totals across relay restarts.
- Players: register and join lines from that same namespace are appended as JSON to
  /var/log/gt3r-relay/players.log. The infrastructure agent's log forwarder tails that
  file (logging.d/gt3r-relay-players.yml). A line is kept only when the player is an
  8-hex daily tag; raw IPs (--log-ips) are dropped and never written.
- Host: failed systemd units, days a reboot has been owed, uptime.

    python3 tools/relay-monitoring/relay-health.py --from-journal testdata/namespace-journal.jsonl
        Parse a journalctl -o json sample (no probe, no host). Stdout is the stats
        object; stderr is one player JSON object per forwarded line.
"""
import argparse
import json
import os
import re
import socket
import struct
import subprocess
import sys
import time

PORT = 47631
STATE = "/var/lib/gt3r-monitoring/state.json"
JOURNAL_NAMESPACE = "gt3r-relay"
PLAYER_LOG = "/var/log/gt3r-relay/players.log"
PLAYER_STATE = "/var/lib/gt3r-monitoring/players.json"
PLAYER_LOG_MAX = 4 * 1024 * 1024
# First run only. After that the journal cursor moves forward one minute at a time.
PLAYER_BACKFILL = "-12h"

GAUGES = {
    "sessions": r"(\d+) sessions",
    "channels": r"(\d+) channels",
    "pendingJoins": r"(\d+) pending joins",
    "linksUsed": r"links (\d+)/\d+ in use",
    "linksCapacity": r"links \d+/(\d+) in use",
    "racing": r"(\d+) racing",
    "queued": r"(\d+) queued",
    "peakChannelKbps": r"peak channel ([\d.]+) kbit/s",
    "typicalRaceMin": r"typical race ([\d.]+) min",
}
COUNTERS = {
    "relayedDatagrams": r"(\d+) datagrams / \d+ KB relayed",
    "relayedKB": r"(\d+) KB relayed",
    "dropped": r"(\d+) dropped",
    "challenges": r"(\d+) challenges",
    "rateLimited": r"(\d+) rate limited",
    "versionChecks": r"(\d+) version checks",
    "versionChecksWindows": r"windows (\d+)",
    "versionChecksMacos": r"macos (\d+)",
    "versionChecksLinux": r"linux (\d+)",
    "versionChecksAndroid": r"android (\d+)",
    "refusedOutdated": r"(\d+) refused as outdated",
    "refusedOtherVersion": r"(\d+) joins refused as another version",
}

# Session codes are 8 characters of the relay's alphabet (no 0, 1, I, L, O). The tag is
# the 8-hex daily hash. Country is two letters, or ?? when the list has no range.
_CODE = r"[23456789ABCDEFGHJKMNPQRSTUVWXYZ]{8}"
_TAG = r"[0-9a-f]{8}"
_CC = r"[A-Z]{2}|\?\?"
_WHEN = r"(?P<logTime>\d{4}-\d{2}-\d{2} \d{2}:\d{2}:\d{2})"
REGISTERED_RE = re.compile(
    _WHEN + r" session (?P<session>" + _CODE + r") registered by (?P<playerTag>" + _TAG +
    r")(?: \((?P<country>" + _CC + r")\))?$"
)
JOIN_RE = re.compile(
    _WHEN + r" join (?P<session>" + _CODE + r") from (?P<playerTag>" + _TAG +
    r")(?: \((?P<country>" + _CC + r")\))?$"
)
IPV4_RE = re.compile(r"\d{1,3}(?:\.\d{1,3}){3}")

PLAYER_ATTRS = ("message", "event", "session", "playerTag", "country", "logTime", "logtype", "service")


def run(*cmd):
    return subprocess.run(cmd, capture_output=True, text=True, timeout=30).stdout


def probe(out):
    out.update(probeOk=0, probeMs=-1, serverProtocol=-1)
    try:
        with socket.socket(socket.AF_INET, socket.SOCK_DGRAM) as s:
            s.settimeout(2)
            t = time.monotonic()
            s.sendto((b"GT3S" + struct.pack("<HB", 0xFFFF, 4)).ljust(64, b"\0"), ("127.0.0.1", PORT))
            data, _ = s.recvfrom(2048)
            ms = (time.monotonic() - t) * 1000
    except OSError:
        return
    if data[:4] == b"GT3S" and len(data) >= 7:
        out.update(probeOk=1, probeMs=round(ms, 2), serverProtocol=struct.unpack_from("<H", data, 4)[0])


def relay_unit(out):
    props = dict(
        line.split("=", 1)
        for line in run("systemctl", "show", "gt3r-relay", "-p", "ActiveState", "-p", "NRestarts",
                        "-p", "MemoryCurrent").splitlines() if "=" in line)
    out["relayActive"] = int(props.get("ActiveState") == "active")
    out["relayRestarts"] = int(props.get("NRestarts") or 0)
    mem = props.get("MemoryCurrent", "")
    out["relayMemoryBytes"] = int(mem) if mem.isdigit() else -1


def message_text(entry):
    """journalctl -o json gives MESSAGE as a string, or as a byte array when it isn't UTF-8."""
    msg = entry.get("MESSAGE")
    if isinstance(msg, str):
        return msg
    if isinstance(msg, list) and msg and all(isinstance(b, int) for b in msg):
        return bytes(b & 0xFF for b in msg).decode("utf-8", "replace")
    return ""


def stats_journal_command(namespace):
    cmd = ["journalctl", "-u", "gt3r-relay", "--since", "-15min", "-g", " stats: ",
           "-o", "json", "-n", "1", "--no-pager",
           "--output-fields=MESSAGE,__REALTIME_TIMESTAMP"]
    if namespace:
        cmd[1:1] = ["--namespace", namespace]
    return cmd


def journal_entries(text):
    entries = []
    for line in text.splitlines():
        line = line.strip()
        if not line:
            continue
        try:
            entries.append(json.loads(line))
        except ValueError:
            continue
    return entries


def read_newest_stats_entry():
    """Namespace first. The default journal only still has the stats line on a host that
    has not picked up LogNamespace=gt3r-relay."""
    for namespace in (JOURNAL_NAMESPACE, None):
        entries = journal_entries(run(*stats_journal_command(namespace)))
        if entries and message_text(entries[-1]) and "__REALTIME_TIMESTAMP" in entries[-1]:
            return entries[-1]
    return None


def parse_stats_message(msg):
    """Absolute gauges and counters from one stats line. A field the line doesn't have is left out."""
    gauges = {}
    for name, rx in GAUGES.items():
        m = re.search(rx, msg)
        if m:
            gauges[name] = float(m.group(1)) if "." in m.group(1) else int(m.group(1))
    counters = {}
    for name, rx in COUNTERS.items():
        m = re.search(rx, msg)
        if m:
            counters[name] = int(m.group(1))
    return gauges, counters


def read_json(path):
    try:
        with open(path) as f:
            return json.load(f)
    except (OSError, ValueError):
        return {}


def write_json(path, obj):
    os.makedirs(os.path.dirname(path), exist_ok=True)
    tmp = path + ".tmp"
    with open(tmp, "w") as f:
        json.dump(obj, f)
    os.replace(tmp, path)


def apply_counters(out, counters, ts, state):
    """Per-minute deltas. The first sample (and a repeated read of the same line) is 0.
    A counter that went backwards means the relay restarted; that sample is the new total."""
    same_line = state.get("ts") == ts
    for name, value in counters.items():
        prev = state.get("counters", {}).get(name)
        if same_line or prev is None:
            out[name] = 0
        else:
            out[name] = value - prev if value >= prev else value
    return {"ts": ts, "counters": counters}


def stats(out, now):
    entry = read_newest_stats_entry()
    if not entry:
        out["statsAgeSeconds"] = 9999
        return
    msg = message_text(entry)
    ts = int(entry["__REALTIME_TIMESTAMP"]) / 1e6
    out["statsAgeSeconds"] = int(now - ts)
    gauges, counters = parse_stats_message(msg)
    out.update(gauges)
    write_json(STATE, apply_counters(out, counters, ts, read_json(STATE)))


def raw_ip(message):
    """An address in the line (--log-ips). Tags have no dots, brackets or '::'."""
    return bool(IPV4_RE.search(message) or "::" in message or "[" in message)


def player_record(message):
    """One forwarded log record, or None when the line isn't a tagged register/join.
    The keys are the New Relic Logs attributes (the forwarder parses this JSON)."""
    if not isinstance(message, str) or raw_ip(message):
        return None
    matched = REGISTERED_RE.match(message)
    event = "registered"
    if not matched:
        matched = JOIN_RE.match(message)
        event = "join"
    if not matched:
        return None
    tag = matched.group("playerTag")
    if not re.fullmatch(_TAG, tag):
        return None
    country = matched.group("country") or "??"
    if not re.fullmatch(_CC, country):
        return None
    return {
        "message": message,
        "event": event,
        "session": matched.group("session"),
        "playerTag": tag,
        "country": country,
        "logTime": matched.group("logTime"),
        "logtype": "gt3r-relay-player",
        "service": "gt3r-relay",
    }


def player_journal_command(cursor):
    # No -n: with a cursor, -n would keep only the newest lines and skip the ones in between.
    cmd = ["journalctl", "--namespace", JOURNAL_NAMESPACE, "-u", "gt3r-relay",
           "--no-pager", "-o", "json",
           "--output-fields=MESSAGE,__CURSOR,__REALTIME_TIMESTAMP"]
    if cursor:
        cmd += ["--after-cursor", cursor]
    else:
        cmd += ["--since", PLAYER_BACKFILL]
    return cmd


def journal_text(cmd):
    proc = subprocess.run(cmd, capture_output=True, text=True, timeout=30)
    return proc.returncode, proc.stdout, proc.stderr


def players_from_entries(entries):
    """Records to forward, plus the cursor of the last entry we saw (player line or not)."""
    records = []
    cursor = None
    for entry in entries:
        if entry.get("__CURSOR"):
            cursor = entry["__CURSOR"]
        rec = player_record(message_text(entry))
        if rec:
            records.append(rec)
    return records, cursor


def append_player_records(path, records, limit=PLAYER_LOG_MAX):
    if not records:
        return
    os.makedirs(os.path.dirname(path), exist_ok=True)
    try:
        if os.path.getsize(path) >= limit:
            os.replace(path, path + ".1")
    except OSError:
        pass
    with open(path, "a", encoding="utf-8") as f:
        for rec in records:
            f.write(json.dumps(rec, separators=(",", ":"), ensure_ascii=True) + "\n")


def ship_players(log_path=None, state_path=None):
    log_path = log_path or PLAYER_LOG
    state_path = state_path or PLAYER_STATE
    state = read_json(state_path)
    cursor = state.get("cursor") or None
    code, text, err = journal_text(player_journal_command(cursor))
    if code != 0 and cursor:
        # The cursor belongs to a journal that was vacuumed. Re-read a short window.
        code, text, err = journal_text(player_journal_command(None))
        cursor = None
    if code != 0:
        raise RuntimeError((err or "").strip() or f"journalctl exited {code}")
    records, new_cursor = players_from_entries(journal_entries(text))
    append_player_records(log_path, records)
    if new_cursor and new_cursor != cursor:
        state["cursor"] = new_cursor
        write_json(state_path, state)
    return records


def host(out, now):
    failed = [l.split()[0] for l in run("systemctl", "list-units", "--state=failed", "--no-legend",
                                         "--plain").splitlines() if l.strip()]
    out["failedUnitCount"] = len(failed)
    out["failedUnits"] = ",".join(failed)
    # unattended-upgrades installs kernels but never reboots; Debian has no
    # /run/reboot-required, so compare the running kernel with the newest in /boot.
    kernels = sorted((f[len("vmlinuz-"):] for f in os.listdir("/boot") if f.startswith("vmlinuz-")),
                     key=lambda v: [int(x) if x.isdigit() else x for x in re.split(r"(\d+)", v)])
    pending = os.path.exists("/run/reboot-required") or (kernels and kernels[-1] != os.uname().release)
    since_file = os.path.join(os.path.dirname(STATE), "reboot-required-since")
    if pending:
        if not os.path.exists(since_file):
            with open(since_file, "w") as f:
                f.write(str(int(now)))
        with open(since_file) as f:
            out["rebootPendingDays"] = int((now - int(f.read())) // 86400)
    else:
        if os.path.exists(since_file):
            os.remove(since_file)
        out["rebootPendingDays"] = 0
    out["rebootRequired"] = int(bool(pending))
    with open("/proc/uptime") as f:
        out["uptimeDays"] = int(float(f.read().split()[0]) // 86400)


def sample_from_stats_entries(entries, now, state=None):
    """Fold stats lines the way a minute-by-minute run would. `out` is the newest sample."""
    state = {} if state is None else state
    out = {"statsAgeSeconds": 9999}
    for entry in entries:
        if " stats: " not in message_text(entry) or "__REALTIME_TIMESTAMP" not in entry:
            continue
        ts = int(entry["__REALTIME_TIMESTAMP"]) / 1e6
        gauges, counters = parse_stats_message(message_text(entry))
        partial = {"statsAgeSeconds": int(now - ts)}
        partial.update(gauges)
        state = apply_counters(partial, counters, ts, state)
        out = partial
    return out, state


def dry_run(path):
    entries = journal_entries(open(path, encoding="utf-8").read())
    stats_entries = [e for e in entries if " stats: " in message_text(e) and "__REALTIME_TIMESTAMP" in e]
    if not stats_entries:
        print("no stats line in " + path, file=sys.stderr)
        return 1
    now = int(stats_entries[-1]["__REALTIME_TIMESTAMP"]) / 1e6
    out, _ = sample_from_stats_entries(entries, now, {})
    missing = [name for name in (*GAUGES, *COUNTERS) if name not in out]
    print(json.dumps(out))
    forwarded = 0
    for entry in entries:
        rec = player_record(message_text(entry))
        if rec:
            forwarded += 1
            print(json.dumps(rec), file=sys.stderr)
    if missing:
        print("missing fields: " + ", ".join(missing), file=sys.stderr)
        return 1
    print(f"players forwarded: {forwarded}", file=sys.stderr)
    return 0


def main(argv=None):
    parser = argparse.ArgumentParser(description="GT3 relay health sample for New Relic Flex")
    parser.add_argument("--from-journal", metavar="FILE",
                        help="dry-run: parse a journalctl --namespace=gt3r-relay -o json sample")
    args = parser.parse_args(argv)
    if args.from_journal:
        return dry_run(args.from_journal)
    now = time.time()
    os.makedirs(os.path.dirname(STATE), exist_ok=True)
    out = {}
    probe(out)
    relay_unit(out)
    try:
        stats(out, now)
    except (OSError, ValueError, subprocess.SubprocessError) as exc:
        out.setdefault("statsAgeSeconds", 9999)
        print(f"gt3r-relay stats: {exc}", file=sys.stderr)
    try:
        ship_players()
    except (OSError, ValueError, subprocess.SubprocessError, RuntimeError) as exc:
        print(f"gt3r-relay players: {exc}", file=sys.stderr)
    host(out, now)
    print(json.dumps(out))
    return 0


if __name__ == "__main__":
    sys.exit(main())
