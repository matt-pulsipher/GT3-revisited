#!/bin/sh
# Runs on the relay host; install.sh copies this folder to /tmp/gt3r-monitoring and runs
# it with the New Relic license (ingest) key on stdin.
set -eu
IFS= read -r key || [ -n "$key" ]
case $key in *NRAL) ;; *) echo "expected a license (ingest) key ending in NRAL on stdin" >&2; exit 2 ;; esac
. /etc/os-release

install -d -m 0755 /etc/apt/keyrings
# The -sha256 key: newrelic-infra.gpg has a SHA1 binding signature, which Debian 13's sqv
# has rejected since 2026-02-01 ("Signing key ... is not bound").
curl -fsSL https://download.newrelic.com/infrastructure_agent/gpg/newrelic-infra-sha256.gpg \
  | gpg --dearmor --yes -o /etc/apt/keyrings/newrelic-infra.gpg
echo "deb [signed-by=/etc/apt/keyrings/newrelic-infra.gpg] https://download.newrelic.com/infrastructure_agent/linux/apt/ $VERSION_CODENAME main" \
  > /etc/apt/sources.list.d/newrelic-infra.list

# Before the package, so its first start already has a key.
install -m 0600 /dev/null /etc/newrelic-infra.yml
cat > /etc/newrelic-infra.yml <<CFG
license_key: $key
display_name: gt3r-relay
custom_attributes:
  project: gt3r-revisited
  role: relay
  environment: production
enable_process_metrics: false
strip_command_line: true
CFG

apt-get update -q
DEBIAN_FRONTEND=noninteractive apt-get install -y -q --no-install-recommends newrelic-infra
# Log forwarding uses the agent's Fluent Bit. The infra package only Recommends it, and this
# script installs the agent without recommends, so ask for the package by name. A missing
# package is a warning: the agent may already bundle a binary, and the health sample must
# still be installed.
if ! DEBIAN_FRONTEND=noninteractive apt-get install -y -q --no-install-recommends fluent-bit; then
  echo "warning: apt did not install fluent-bit; player logs forward only if the agent already has one" >&2
fi
# There's no Docker here; the package's container integration would only log errors.
rm -f /etc/newrelic-infra/integrations.d/docker-config.yml

install -d -m 0755 /usr/local/lib/gt3r-monitoring /var/lib/gt3r-monitoring /var/log/gt3r-relay \
  /etc/newrelic-infra/logging.d
install -m 0755 /tmp/gt3r-monitoring/relay-health.py /usr/local/lib/gt3r-monitoring/
install -m 0644 /tmp/gt3r-monitoring/newrelic-flex.yml /etc/newrelic-infra/integrations.d/gt3r-relay-health.yml
install -m 0644 /tmp/gt3r-monitoring/gt3r-relay-players.conf /usr/local/lib/gt3r-monitoring/
install -m 0644 /tmp/gt3r-monitoring/gt3r-relay-players.parsers.conf /usr/local/lib/gt3r-monitoring/
# Player JSON only (daily tag + country). Never install the forwarder for a unit that logs IPs.
exec_line=$(grep '^ExecStart=' /etc/systemd/system/gt3r-relay.service 2>/dev/null || true)
case $exec_line in
  *--log-ips*)
    echo "warning: gt3r-relay ExecStart has --log-ips; player log forwarding not installed" >&2
    rm -f /etc/newrelic-infra/logging.d/gt3r-relay-players.yml
    ;;
  *)
    install -m 0644 /tmp/gt3r-monitoring/gt3r-relay-players.yml /etc/newrelic-infra/logging.d/gt3r-relay-players.yml
    ;;
esac
case $exec_line in
  *"--geoip /var/lib/gt3r-relay/country.csv"*) echo "geoip: enabled in gt3r-relay.service" ;;
  *) echo "warning: gt3r-relay ExecStart has no --geoip; countries stay ?? until tools/gt3r-relay/deploy.sh" >&2 ;;
esac
if systemctl is-enabled gt3r-relay-geoip.timer >/dev/null 2>&1; then
  echo "geoip timer: $(systemctl is-enabled gt3r-relay-geoip.timer)"
else
  echo "warning: gt3r-relay-geoip.timer is not enabled; tools/gt3r-relay/deploy.sh wires update-geoip.sh" >&2
fi
# Empty until relay-health.py appends. 0640: root reads it; the lines have tags, not addresses.
[ -f /var/log/gt3r-relay/players.log ] || install -m 0640 /dev/null /var/log/gt3r-relay/players.log
systemctl enable newrelic-infra >/dev/null 2>&1
systemctl restart newrelic-infra
python3 -I /usr/local/lib/gt3r-monitoring/relay-health.py
echo "newrelic-infra: $(systemctl is-active newrelic-infra)"
rm -rf /tmp/gt3r-monitoring
