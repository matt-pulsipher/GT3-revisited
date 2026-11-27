#!/bin/sh
# Sets the game versions the relay announces and enforces (docs/dev/ONLINE.md "Versions and
# updates"), over SSH, without dropping sessions:
#
#   tools/gt3r-relay/set-versions.sh root@203.0.113.7 0.2.1                 # latest 0.2.1, min_online kept
#   tools/gt3r-relay/set-versions.sh root@203.0.113.7 0.3.0 0.3.0           # a new online protocol: older games update
#   tools/gt3r-relay/set-versions.sh root@203.0.113.7 0.2.0 0.2.0 'https://github.com/<owner>/<repo>/releases/latest/download/{asset}'
#   tools/gt3r-relay/set-versions.sh root@203.0.113.7 0.2.0 0.0.0           # announce only: anyone may play
#   tools/gt3r-relay/set-versions.sh --game example root@203.0.113.7 0.1.0 0.1.0 'https://...'  # another game
#
# --game <id>: another app on the engine (docs/dev/ONLINE.md "Other games on one relay"): sets the
#   versions of requests tagged with that id, in the file's [id] section. Without it: the main
#   game's. Either way the rest of the file is kept.
# latest: the newest release (older games show "version x.y.z is available").
# min_online: games below it cannot create or join sessions (default: keep the one already set;
#   the first run: latest). 0.0.0 = no minimum. Players join only hosts of their own x.y anyway,
#   so raise it only when the online protocol or the rendezvous changes.
# url: where "Download update" goes (default: keep the one already set; the first run needs it).
#   {asset} becomes the player's package (gt3r-windows-x64.zip, gt3r-macos-arm64.zip,
#   gt3r-linux-x86_64.tar.gz, gt3r-android.apk, gt3r-windows-x64-vr.zip, gt3r-quest.apk); the release
#   notes page is the URL cut at /download/.
# Writes /etc/gt3r-relay/versions.conf (deploy.sh installs a relay that reads it), then
# `systemctl reload gt3r-relay`, and shows the relay's log line that confirms the new values.
set -eu

usage() { echo "usage: $0 [--game <id>] user@host <latest> [min_online] [release_url]" >&2; exit 2; }
game=""
if [ "${1:-}" = --game ]; then
    [ $# -ge 2 ] || usage
    game=$2
    shift 2
    echo "$game" | grep -Eq '^[a-z0-9._-]{1,32}$' || { echo "a game id is 1 to 32 of a-z 0-9 . _ - (got '$game')" >&2; exit 2; }
fi
[ $# -ge 2 ] && [ $# -le 4 ] || usage
target=$1
latest=$2
min=${3:-}
url=${4:-}

is_version() { echo "$1" | grep -Eq '^[0-9]{1,5}\.[0-9]{1,5}\.[0-9]{1,5}$'; }
is_version "$latest" || { echo "latest must be x.y.z (got '$latest')" >&2; exit 2; }
[ -z "$min" ] || is_version "$min" || { echo "min_online must be x.y.z (got '$min')" >&2; exit 2; }
# min_online above latest would lock every game out (a kept one is checked by the relay: it refuses such a file).
if [ -n "$min" ] && [ "$min" != 0.0.0 ] && [ "$(printf '%s\n%s\n' "$min" "$latest" | sort -t. -k1,1n -k2,2n -k3,3n | tail -n 1)" != "$latest" ]; then
    echo "min_online $min is above latest $latest" >&2
    exit 2
fi
if [ -n "$url" ]; then
    echo "$url" | grep -Eq '^https?://[!-~]+$' || { echo "release_url must be http(s):// without spaces" >&2; exit 2; }
    [ ${#url} -le 128 ] || { echo "release_url is longer than 128 characters" >&2; exit 2; }
    case "$url" in *[\'\"\$\`\\]*) echo "release_url must not contain quotes, \$, \` or \\" >&2; exit 2 ;; esac
fi

ssh -o ConnectTimeout=20 "$target" "GAME='$game' LATEST='$latest' MIN='$min' URL='$url' sh -s" <<'REMOTE'
set -eu
SUDO=""; [ "$(id -u)" -eq 0 ] || SUDO=sudo
conf=/etc/gt3r-relay/versions.conf
# Lines of one section ("": the main game's, before the first [id]); and the file without it.
section() {
    [ -f "$conf" ] || return 0
    awk -v g="$1" '/^[[:space:]]*\[/ { s = $0; gsub(/^[[:space:]]*\[[[:space:]]*|[[:space:]]*\][[:space:]]*$/, "", s); cur = s; next } cur == g' "$conf"
}
others() {
    [ -f "$conf" ] || return 0
    awk -v g="$1" '/^[[:space:]]*\[/ { s = $0; gsub(/^[[:space:]]*\[[[:space:]]*|[[:space:]]*\][[:space:]]*$/, "", s); cur = s } cur != g' "$conf"
}
if [ -z "$URL" ]; then
    URL=$(section "$GAME" | sed -n 's/^[[:space:]]*release_url[[:space:]]*=[[:space:]]*//p' | tail -n 1)
fi
if [ -z "$MIN" ]; then
    MIN=$(section "$GAME" | sed -n 's/^[[:space:]]*min_online[[:space:]]*=[[:space:]]*//p' | tail -n 1)
    [ -n "$MIN" ] || MIN=$LATEST
fi
[ -n "$URL" ] || { echo "no release_url set yet: pass it as the last argument (see the header of set-versions.sh)" >&2; exit 2; }
$SUDO install -d -m 755 /etc/gt3r-relay
keys() {
    echo "latest = $LATEST"
    echo "min_online = $MIN"
    echo "release_url = $URL"
}
{
    if [ -z "$GAME" ]; then
        echo "# gt3r-relay game versions (tools/gt3r-relay/set-versions.sh, $(date -u '+%Y-%m-%d %H:%M UTC'))"
        keys
        rest=$(others "")
        [ -z "$rest" ] || printf '\n%s\n' "$rest"
    else
        others "$GAME" | sed -e :a -e '/^\n*$/{$d;N;ba' -e '}' # (trailing blank lines dropped)
        echo
        echo "[$GAME]"
        echo "# (set-versions.sh --game, $(date -u '+%Y-%m-%d %H:%M UTC'))"
        keys
    fi
} | $SUDO tee "$conf.new" >/dev/null
$SUDO chmod 644 "$conf.new"
$SUDO mv "$conf.new" "$conf"
$SUDO systemctl reload gt3r-relay
sleep 1
$SUDO journalctl --namespace=gt3r-relay -u gt3r-relay --no-pager -n 20 -o cat | grep 'reload:' | tail -n 1
REMOTE
