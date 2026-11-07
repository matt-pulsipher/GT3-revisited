#!/bin/sh
# Sets the game versions the relay announces and enforces (docs/dev/ONLINE.md "Versions and
# updates"), over SSH, without dropping sessions:
#
#   tools/gt3r-relay/set-versions.sh root@203.0.113.7 0.2.0                 # latest = min_online = 0.2.0
#   tools/gt3r-relay/set-versions.sh root@203.0.113.7 0.2.0 0.2.0 'https://github.com/<owner>/<repo>/releases/latest/download/{asset}'
#   tools/gt3r-relay/set-versions.sh root@203.0.113.7 0.2.0 0.0.0           # announce only: anyone may play
#
# latest: the newest release (older games show "version x.y.z is available").
# min_online: games below it cannot create or join sessions (default: latest). 0.0.0 = no minimum.
# url: where "Download update" goes (default: keep the one already set; the first run needs it).
#   {asset} becomes the player's package (gt3r-windows-x64.zip, gt3r-macos-arm64.zip,
#   gt3r-linux-x86_64.tar.gz, gt3r-android.apk, gt3r-windows-x64-vr.zip, gt3r-quest.apk); the release
#   notes page is the URL cut at /download/.
# Writes /etc/gt3r-relay/versions.conf (deploy.sh installs a relay that reads it), then
# `systemctl reload gt3r-relay`, and shows the relay's log line that confirms the new values.
set -eu

usage() { echo "usage: $0 user@host <latest> [min_online] [release_url]" >&2; exit 2; }
[ $# -ge 2 ] && [ $# -le 4 ] || usage
target=$1
latest=$2
min=${3:-$latest}
url=${4:-}

is_version() { echo "$1" | grep -Eq '^[0-9]{1,5}\.[0-9]{1,5}\.[0-9]{1,5}$'; }
is_version "$latest" || { echo "latest must be x.y.z (got '$latest')" >&2; exit 2; }
is_version "$min" || { echo "min_online must be x.y.z (got '$min')" >&2; exit 2; }
# min_online above latest would lock every game out.
if [ "$min" != 0.0.0 ] && [ "$(printf '%s\n%s\n' "$min" "$latest" | sort -t. -k1,1n -k2,2n -k3,3n | tail -n 1)" != "$latest" ]; then
    echo "min_online $min is above latest $latest" >&2
    exit 2
fi
if [ -n "$url" ]; then
    echo "$url" | grep -Eq '^https?://[!-~]+$' || { echo "release_url must be http(s):// without spaces" >&2; exit 2; }
    [ ${#url} -le 128 ] || { echo "release_url is longer than 128 characters" >&2; exit 2; }
    case "$url" in *[\'\"\$\`\\]*) echo "release_url must not contain quotes, \$, \` or \\" >&2; exit 2 ;; esac
fi

ssh -o ConnectTimeout=20 "$target" "LATEST='$latest' MIN='$min' URL='$url' sh -s" <<'REMOTE'
set -eu
SUDO=""; [ "$(id -u)" -eq 0 ] || SUDO=sudo
conf=/etc/gt3r-relay/versions.conf
if [ -z "$URL" ] && [ -f "$conf" ]; then
    URL=$(sed -n 's/^[[:space:]]*release_url[[:space:]]*=[[:space:]]*//p' "$conf" | tail -n 1)
fi
[ -n "$URL" ] || { echo "no release_url set yet: pass it as the 4th argument (see the header of set-versions.sh)" >&2; exit 2; }
$SUDO install -d -m 755 /etc/gt3r-relay
{
    echo "# gt3r-relay game versions (tools/gt3r-relay/set-versions.sh, $(date -u '+%Y-%m-%d %H:%M UTC'))"
    echo "latest = $LATEST"
    echo "min_online = $MIN"
    echo "release_url = $URL"
} | $SUDO tee "$conf.new" >/dev/null
$SUDO chmod 644 "$conf.new"
$SUDO mv "$conf.new" "$conf"
$SUDO systemctl reload gt3r-relay
sleep 1
$SUDO journalctl --namespace=gt3r-relay -u gt3r-relay --no-pager -n 20 -o cat | grep 'reload:' | tail -n 1
REMOTE
