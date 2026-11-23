# Relay monitoring

Optional New Relic monitoring for a relay host (the project's own relay uses it). The
account may be shared with other projects, so everything here is scoped to the
`gt3r-relay` entity and has its own policy and workflow. `<relay host>` below is the
relay's SSH target (`user@host`).

## What runs on the host

- **Infrastructure agent** (`newrelic-infra`, from New Relic's apt repo): CPU, memory,
  disk and network for the box, as entity `gt3r-relay` (custom attributes
  `project: gt3r-revisited`, `role: relay`). It sends no process samples and strips
  command lines. The only logs it forwards are the relay's register/join lines
  (below). It does not tail the system journal.
- **`relay-health.py` every minute through Flex**, reported as a `GT3RelaySample`
  event:
  - A UDP probe to `127.0.0.1:47631` with rendezvous version `0xFFFF`. The relay
    answers `Error(Version)` in its own header, so this checks the socket and the
    loop, reports the server's protocol version (`serverProtocol`), and counts
    nothing: no challenge, no version check.
  - The unit's state, restart count and memory (`relayActive`, `relayRestarts`,
    `relayMemoryBytes`).
  - The newest `stats:` line from the relay's journal namespace
    (`journalctl --namespace=gt3r-relay -u gt3r-relay`). Its gauges (sessions, links
    used/capacity, racing, queued, peak channel) are reported as they are. Its
    cumulative counters (relayed datagrams/KB, dropped, challenges, rate limited,
    version checks per platform, refused as outdated) become per-minute deltas,
    so `sum()` gives real totals across restarts. If that namespace has no stats
    line in the last 15 minutes, the default journal is tried once (a host that
    has not picked up `LogNamespace=` yet). No line: `statsAgeSeconds` is 9999 and
    the game-stat fields are left out. The probe and the unit fields still go out.
  - Failed units, days a reboot has been owed, and uptime.

The same run appends register and join lines to `/var/log/gt3r-relay/players.log`
as one JSON object per line. Fluent Bit (the agent's log forwarder) tails that
file and the JSON keys become Logs attributes. The agent's `systemd:` source
cannot name a journal namespace, which is why the script reads
`journalctl --namespace=gt3r-relay` and the forwarder tails the file.

A line is forwarded only when the player is the 8-hex daily tag. `--log-ips`
lines (raw addresses) are dropped and never written, and `host-setup.sh` leaves
the forwarder out when the unit's ExecStart has `--log-ips`. Session close,
queue and stats lines are not forwarded. Player lines do not carry platform or
game version; those stay the `versionChecks*` counters on `GT3RelaySample`.

Attributes on each forwarded log (these names, this casing):

| Attribute | Example | Meaning |
|---|---|---|
| `message` | `2026-10-25 12:00:12 session K7MNPQRS registered by a1b2c3d4 (BR)` | the relay line |
| `event` | `registered` or `join` | register, or a player joining |
| `session` | `K7MNPQRS` | session code |
| `playerTag` | `a1b2c3d4` | daily tag (keyed hash of the IP; new key each UTC day) |
| `country` | `BR`, or `??` | ISO country from `--geoip`, `??` when unknown |
| `logTime` | `2026-10-25 12:00:12` | UTC time printed by the relay |
| `logtype` | `gt3r-relay-player` | filter |
| `service` | `gt3r-relay` | filter |

The Logs timestamp is when the line was forwarded (the health script runs once a
minute). `logTime` is the relay's own clock. The first run after install also
copies matching lines from the last 12 hours; those get a forward-time timestamp,
so a per-hour chart of that window is wrong, but `uniqueCount(playerTag)` over a
day is not. A tag changes at UTC midnight, so a query longer than a day counts
one person once per day.

```sql
SELECT uniqueCount(playerTag) FROM Log
  WHERE logtype = 'gt3r-relay-player' SINCE 1 day ago
SELECT uniqueCount(playerTag) FROM Log
  WHERE logtype = 'gt3r-relay-player' FACET country SINCE 1 day ago
SELECT count(*) FROM Log WHERE logtype = 'gt3r-relay-player'
  FACET event SINCE 1 day ago
```

Volume is one line per register or join. At the relay's current traffic that is
megabytes a month, not gigabytes. The file is capped at 4 MB (one rotated copy).

Typical queries:

```sql
SELECT sum(versionChecksAndroid), sum(versionChecksWindows), sum(versionChecksMacos), sum(versionChecksLinux)
  FROM GT3RelaySample SINCE 1 week ago TIMESERIES 1 day      -- launches by platform
SELECT max(linksUsed), max(racing), max(queued) FROM GT3RelaySample SINCE 1 day ago TIMESERIES
SELECT sum(refusedOutdated) FROM GT3RelaySample SINCE 1 day ago   -- games told to update
```

## Install or update

```bash
tools/relay-monitoring/install.sh <relay host> < license_key
```

The license (ingest) key ends in `NRAL` and comes on stdin. It lands only in
`/etc/newrelic-infra.yml` (0600). The script uses New Relic's `newrelic-infra-sha256.gpg`
key. The plain `newrelic-infra.gpg` has a SHA1 binding signature that Debian 13's
`sqv` rejects. Re-run it after changing `relay-health.py` or the player-log
config. It restarts `newrelic-infra` only. It does not restart the relay, and
`deploy.sh` does not install monitoring.

To confirm the game-stat fields came back, on the server:

```bash
python3 -I /usr/local/lib/gt3r-monitoring/relay-health.py
journalctl --namespace=gt3r-relay -u gt3r-relay -n 1 -g 'stats:' -o cat --no-pager
```

The JSON must include `sessions` (and the other stats names), not only
`statsAgeSeconds: 9999`. In NRQL, `sessions` is absent while the collector is
still on the default journal:

```sql
SELECT latest(sessions), latest(statsAgeSeconds), latest(versionChecksAndroid)
  FROM GT3RelaySample SINCE 30 minutes ago
```

Player logs show up a minute or two after that restart (`logtype = 'gt3r-relay-player'`).
If they don't, the agent has no Fluent Bit binary. `install.sh` tries
`apt-get install fluent-bit` and continues either way. Check:

```bash
journalctl -u newrelic-infra --since -10min --no-pager | grep -i -E 'fluent|gt3r-relay-players'
ls -l /var/log/gt3r-relay/players.log /usr/bin/fluent-bit /opt/td-agent-bit/bin/td-agent-bit \
  /var/db/newrelic-infra/newrelic-integrations/logging/fluent-bit
```

`--geoip` and the monthly country-list timer are already on the unit `deploy.sh`
installed with the journal namespace (bd51e44). This monitoring update does not
redeploy the relay. Check, and only if a line is wrong run
`tools/gt3r-relay/deploy.sh <relay host> --when-idle`
(restarts the relay once it has no sessions):

```bash
ssh <relay host> "systemctl cat gt3r-relay | grep '^ExecStart='"
ssh <relay host> "systemctl is-enabled gt3r-relay-geoip.timer && systemctl list-timers gt3r-relay-geoip.timer --no-pager"
```

ExecStart must contain `--geoip /var/lib/gt3r-relay/country.csv` and must not
contain `--log-ips`. The timer should be `enabled`.

The "Relay stats line missing" alert (statsAgeSeconds over 3 minutes, which is
how this outage looked: samples still arrived) is installed by re-running
`setup-newrelic.py` after the host fix. Do the host install first.

If the stats line's format changes in `tools/gt3r-relay/main.cpp`, check the
patterns at the top of `relay-health.py`. A field it can't find is simply left
out of the sample. Parser check, from this repo, no server needed:

```bash
python3 tools/relay-monitoring/test_relay_health.py
python3 tools/relay-monitoring/relay-health.py --from-journal \
  tools/relay-monitoring/testdata/namespace-journal.jsonl
```

The sample is `journalctl --namespace=gt3r-relay -u gt3r-relay -o json` shaped:
one JSON object per line, `MESSAGE` holding the relay's own `YYYY-MM-DD HH:MM:SS`
prefix plus the text. Stdout is the Flex object (second stats line's gauges, and
counters as the delta from the first). Stderr is one player JSON object per
forwarded line.

## Alerts

`setup-newrelic.py` builds the "GT3R relay" policy and the "GT3R relay email"
workflow, matching by name and updating in place:

```bash
NEW_RELIC_API_KEY=NRAK-... NEW_RELIC_ACCOUNT_ID=<account id> ALERT_EMAIL=you@example.com python3 tools/relay-monitoring/setup-newrelic.py
```

`NEW_RELIC_API_KEY` is a User API key, used from here only, never on the server.

| Condition | Fires when |
|---|---|
| Relay host not reporting | no host metrics for 10 min |
| Relay host disk filling / memory | disk > 80/90%, memory > 95% for 15 min |
| Relay down | `gt3r-relay.service` not active for 3 min |
| Relay not answering | the local probe gets no answer for 5 min |
| Relay restarting | more than 2 restarts in 30 min |
| Relay memory near its cap | over 200 MB of the unit's 256 MB `MemoryMax` for 15 min |
| Link slots nearly full | over 80% of `--relay-links` in use for 10 min |
| Lobbies waiting for slots | any lobby queued for 10 min ("Relay busy") |
| No players reaching the relay | no version checks for 2 hours |
| Relay health check silent | no `GT3RelaySample` for 15 min |
| Relay stats line missing | `statsAgeSeconds` over 3 minutes (no fresh `stats:` line; game-stat fields absent) |
| Relay host failed unit / reboot overdue | a failed unit for 15 min; a reboot owed for 14+ days |

Nothing here can send UDP from the internet: New Relic's free ping monitors are
HTTP only, and its scripted monitors cost checks. Version checks stand in for
outside reachability. Every game launch sends one, so if they stop while the
relay is fine locally, players can't reach it (DNS, the firewall, routing). Since
2026-10-21 there has been no hour without one (about 120 an hour, mostly
Android). Before that, quiet stretches of up to 9 hours were normal. If traffic
falls back, lengthen or drop that condition.
