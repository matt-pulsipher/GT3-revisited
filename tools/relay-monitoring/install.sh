#!/bin/sh
# Installs (or updates) New Relic monitoring on the relay host over SSH: the
# infrastructure agent for host metrics, relay-health.py through Flex for the relay
# itself, and player-log forwarding (tag + country only). See README.md here.
# Does not restart gt3r-relay.
#
#   tools/relay-monitoring/install.sh <relay host> < license_key
#
# The license (ingest) key comes on stdin so it stays out of shell history and process
# lists. Never the NRAK- user key: that one is only for setup-newrelic.py, from here.
set -eu
target=${1:-}
[ -n "$target" ] || { echo "usage: $0 user@host < license_key" >&2; exit 2; }
here=$(cd "$(dirname "$0")" && pwd)
IFS= read -r key || [ -n "$key" ]

# One shared connection for every step (one key approval), as in tools/gt3r-relay/deploy.sh.
ctl="/tmp/gt3r-ssh-%C"
opts="-o ControlMaster=auto -o ControlPath=$ctl -o ControlPersist=120 -o ConnectTimeout=20 -o ServerAliveInterval=10"
ssh $opts "$target" true
trap 'ssh $opts -O exit "$target" 2>/dev/null' EXIT
COPYFILE_DISABLE=1 tar --no-xattrs -C "$here" -cf - \
    relay-health.py newrelic-flex.yml host-setup.sh \
    gt3r-relay-players.yml gt3r-relay-players.conf gt3r-relay-players.parsers.conf \
  | ssh $opts "$target" 'rm -rf /tmp/gt3r-monitoring && mkdir /tmp/gt3r-monitoring && tar -xf - -C /tmp/gt3r-monitoring'
printf '%s\n' "$key" | ssh $opts "$target" 'sh /tmp/gt3r-monitoring/host-setup.sh'
