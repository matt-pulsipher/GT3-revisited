#!/usr/bin/env python3
"""Proves the Flex stats parser and the player-log filter against a journalctl --namespace
sample (testdata/namespace-journal.jsonl). No network, no journald. Standard library only.

    python3 tools/relay-monitoring/test_relay_health.py
"""
import importlib.util
import json
import subprocess
import sys
import tempfile
from pathlib import Path

HERE = Path(__file__).resolve().parent
SAMPLE = HERE / "testdata" / "namespace-journal.jsonl"
SCRIPT = HERE / "relay-health.py"

spec = importlib.util.spec_from_file_location("relay_health", SCRIPT)
rh = importlib.util.module_from_spec(spec)
spec.loader.exec_module(rh)


def load_sample():
    return rh.journal_entries(SAMPLE.read_text(encoding="utf-8"))


def stats_messages(entries):
    return [rh.message_text(e) for e in entries if " stats: " in rh.message_text(e)]


def test_parse_stats_absolutes():
    first, second = stats_messages(load_sample())
    gauges, counters = rh.parse_stats_message(first)
    assert gauges == {
        "sessions": 2, "channels": 4, "pendingJoins": 1, "linksUsed": 10, "linksCapacity": 300,
        "racing": 3, "queued": 1, "peakChannelKbps": 12, "typicalRaceMin": 8.5,
    }, gauges
    assert counters == {
        "relayedDatagrams": 1000, "relayedKB": 80, "dropped": 2, "challenges": 5, "rateLimited": 1,
        "versionChecks": 40, "versionChecksWindows": 10, "versionChecksMacos": 5,
        "versionChecksLinux": 3, "versionChecksAndroid": 22, "refusedOutdated": 4,
    }, counters
    # Every pattern has to hit. A renamed stats field would show up here.
    assert set(gauges) == set(rh.GAUGES)
    assert set(counters) == set(rh.COUNTERS)
    g2, c2 = rh.parse_stats_message(second)
    assert g2["sessions"] == 3 and g2["linksUsed"] == 15 and g2["typicalRaceMin"] == 9.0
    assert c2["versionChecksAndroid"] == 33 and c2["relayedKB"] == 100


def test_dry_run_deltas_and_players():
    proc = subprocess.run(
        [sys.executable, str(SCRIPT), "--from-journal", str(SAMPLE)],
        capture_output=True, text=True, check=False)
    assert proc.returncode == 0, proc.stderr
    out = json.loads(proc.stdout)
    assert out["statsAgeSeconds"] == 0
    assert out["sessions"] == 3 and out["channels"] == 6 and out["pendingJoins"] == 0
    assert out["linksUsed"] == 15 and out["linksCapacity"] == 300
    assert out["racing"] == 4 and out["queued"] == 0
    assert out["peakChannelKbps"] == 20 and out["typicalRaceMin"] == 9.0
    assert out["relayedDatagrams"] == 300 and out["relayedKB"] == 20
    assert out["dropped"] == 1 and out["challenges"] == 3 and out["rateLimited"] == 0
    assert out["versionChecks"] == 15
    assert out["versionChecksWindows"] == 2 and out["versionChecksMacos"] == 1
    assert out["versionChecksLinux"] == 1 and out["versionChecksAndroid"] == 11
    assert out["refusedOutdated"] == 2
    missing = [name for name in (*rh.GAUGES, *rh.COUNTERS) if name not in out]
    assert not missing, missing
    players = [json.loads(line) for line in proc.stderr.splitlines() if line.startswith("{")]
    assert len(players) == 4, proc.stderr
    by_tag = {p["playerTag"]: p for p in players}
    assert set(by_tag) == {"a1b2c3d4", "deadbeef", "cafebabe", "0badf00d"}
    assert by_tag["a1b2c3d4"]["event"] == "registered" and by_tag["a1b2c3d4"]["country"] == "BR"
    assert by_tag["a1b2c3d4"]["session"] == "K7MNPQRS"
    assert by_tag["deadbeef"]["event"] == "join" and by_tag["deadbeef"]["country"] == "US"
    assert by_tag["cafebabe"]["country"] == "??"
    assert by_tag["0badf00d"]["country"] == "??" and by_tag["0badf00d"]["event"] == "registered"
    for rec in players:
        assert set(rec) == set(rh.PLAYER_ATTRS)
        assert rh.IPV4_RE.search(json.dumps(rec)) is None
        assert "::" not in json.dumps(rec) and "[" not in rec["message"]
    assert "203.0.113" not in proc.stderr and "2001:db8" not in proc.stderr
    assert "players forwarded: 4" in proc.stderr
    return out, players


def test_stats_reads_namespace_first():
    entries = load_sample()
    newest = [e for e in entries if " stats: " in rh.message_text(e)][-1]
    line = json.dumps(newest)
    calls = []

    def fake(*cmd):
        calls.append(cmd)
        if "--namespace" in cmd:
            return line + "\n"
        raise AssertionError("default journal read even though the namespace had a stats line: " + " ".join(cmd))

    rh.run = fake
    with tempfile.TemporaryDirectory() as tmp:
        rh.STATE = str(Path(tmp) / "state.json")
        out = {}
        now = int(newest["__REALTIME_TIMESTAMP"]) / 1e6 + 30
        rh.stats(out, now)
    assert out["sessions"] == 3 and out["statsAgeSeconds"] == 30
    assert out["relayedDatagrams"] == 0  # first sample: no baseline yet
    cmd = calls[0]
    assert cmd[cmd.index("--namespace") + 1] == "gt3r-relay"
    assert len(calls) == 1


def test_stats_falls_back_to_default_journal():
    entries = load_sample()
    newest = [e for e in entries if " stats: " in rh.message_text(e)][-1]
    line = json.dumps(newest)
    calls = []

    def fake(*cmd):
        calls.append(cmd)
        if "--namespace" in cmd:
            return ""
        return line + "\n"

    rh.run = fake
    with tempfile.TemporaryDirectory() as tmp:
        rh.STATE = str(Path(tmp) / "state.json")
        out = {}
        rh.stats(out, int(newest["__REALTIME_TIMESTAMP"]) / 1e6)
    assert out["linksCapacity"] == 300 and out["versionChecksAndroid"] == 0
    assert "--namespace" in calls[0]
    assert "--namespace" not in calls[1]


def test_ship_players_drops_ips_and_remembers_cursor():
    sample = SAMPLE.read_text(encoding="utf-8")
    calls = []

    def fake(cmd):
        calls.append(cmd)
        if len(calls) == 1:
            return 0, sample, ""
        return 0, "", ""

    rh.journal_text = fake
    with tempfile.TemporaryDirectory() as tmp:
        log_path = str(Path(tmp) / "players.log")
        state_path = str(Path(tmp) / "players.json")
        first = rh.ship_players(log_path, state_path)
        text = Path(log_path).read_text(encoding="utf-8")
        assert len(first) == 4
        assert "203.0.113" not in text and "2001:db8" not in text
        assert "a1b2c3d4" in text and "deadbeef" in text
        second = rh.ship_players(log_path, state_path)
        assert second == []
        assert Path(log_path).read_text(encoding="utf-8") == text
    assert calls[0][calls[0].index("--namespace") + 1] == "gt3r-relay"
    assert "--since" in calls[0] and "-12h" in calls[0]
    assert "--after-cursor" in calls[1]
    assert calls[1][calls[1].index("--after-cursor") + 1].startswith("s=ns1;")


def test_message_byte_array_and_rotation():
    raw = b"2026-10-25 12:00:12 session K7MNPQRS registered by a1b2c3d4 (BR)"
    text = rh.message_text({"MESSAGE": list(raw)})
    rec = rh.player_record(text)
    assert rec["playerTag"] == "a1b2c3d4" and rec["country"] == "BR"
    with tempfile.TemporaryDirectory() as tmp:
        path = str(Path(tmp) / "sub" / "players.log")
        rh.append_player_records(path, [rec], limit=80)
        rh.append_player_records(path, [rec], limit=80)
        assert Path(path).is_file()
        # The rotated file holds the first write; the live file holds the second.
        assert Path(path + ".1").is_file()
        assert "a1b2c3d4" in Path(path).read_text(encoding="utf-8")


def test_forwarder_config_and_unit():
    yml = (HERE / "gt3r-relay-players.yml").read_text(encoding="utf-8")
    conf = (HERE / "gt3r-relay-players.conf").read_text(encoding="utf-8")
    parsers = (HERE / "gt3r-relay-players.parsers.conf").read_text(encoding="utf-8")
    assert "gt3r-relay-players.conf" in yml and "gt3r-relay-players.parsers.conf" in yml
    assert "Parser            gt3r_player_json" in conf
    assert "Name   gt3r_player_json" in parsers and "Format json" in parsers
    assert "Name              tail" in conf and "/var/log/gt3r-relay/players.log" in conf
    assert "[OUTPUT]" not in conf
    unit = (HERE.parent / "gt3r-relay" / "gt3r-relay.service").read_text(encoding="utf-8")
    exec_line = next(line for line in unit.splitlines() if line.startswith("ExecStart="))
    assert "--geoip /var/lib/gt3r-relay/country.csv" in exec_line
    assert "--log-ips" not in exec_line
    assert "LogNamespace=gt3r-relay" in unit
    install = (HERE / "install.sh").read_text(encoding="utf-8")
    for name in ("gt3r-relay-players.yml", "gt3r-relay-players.conf", "gt3r-relay-players.parsers.conf"):
        assert name in install


def main():
    test_parse_stats_absolutes()
    out, players = test_dry_run_deltas_and_players()
    test_stats_reads_namespace_first()
    test_stats_falls_back_to_default_journal()
    test_ship_players_drops_ips_and_remembers_cursor()
    test_message_byte_array_and_rotation()
    test_forwarder_config_and_unit()
    fields = ", ".join(name for name in (*rh.GAUGES, *rh.COUNTERS))
    print("stats fields:", fields)
    print("sample deltas:", json.dumps(out, sort_keys=True))
    print("player attributes:", ", ".join(rh.PLAYER_ATTRS))
    print("sample player:", json.dumps(players[0], sort_keys=True))
    print("ok")


if __name__ == "__main__":
    main()
