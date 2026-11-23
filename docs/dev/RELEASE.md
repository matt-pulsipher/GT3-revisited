# Releasing

The CI on `master` builds every platform; a version tag's run drafts the GitHub
Release. A release needs the project's relay (online play and the update check read
its versions file) and the Android release key in the repository's secrets
(`ANDROID_KEYSTORE_BASE64`, `ANDROID_KEYSTORE_PASSWORD`); without the key the run
builds no APKs. `<relay host>` below is the relay's SSH target.

## Checklist

1. **Version.** Bump `project(GT3Revisited VERSION x.y.z)` in `CMakeLists.txt`
   (the game, the macOS bundle and Android's versionName / versionCode read it).
   The minor version goes up only when online play between the old and the new
   game breaks (physics, race rules, net messages); otherwise the patch version.
2. **Verify locally.** Build and test on macOS, Windows and Linux; for anything
   touching Android, a real device. Which tests: [TESTING.md](TESTING.md).
3. **Merge** everything for the release into `master`; the tree must be clean.
   Fixes go to `master` first.
4. **Commit and tag** the version on `master` (`git tag vx.y.z`), push the commit and the tag.
5. **Relay first, if needed.** If the rendezvous protocol or the relay changed:
   `tools/gt3r-relay/deploy.sh <relay host>` (keeps the versions file). Old games
   are refused until they update, so do this together with publishing. If the relay's
   `stats:` line changed, check the patterns in `tools/relay-monitoring/relay-health.py`
   and re-run its `install.sh` ([relay monitoring](../../tools/relay-monitoring/README.md)).
6. **Publish.** The tag's CI run drafts the GitHub Release (packages,
   one stable-named file per platform, `SHA256SUMS`, attestations). Check that every platform's
   stable asset is there (`gt3r-windows-x64.zip`, `gt3r-windows-x64-vr.zip`,
   `gt3r-macos-arm64.zip`, `gt3r-linux-x86_64.tar.gz`, `gt3r-android.apk`,
   `gt3r-quest.apk`; no APKs without the Android key), replace the generated
   notes, check that the draft's tag is `vx.y.z`, publish.
7. **Announce on the relay.** Online play between versions is only safe when nothing
   that both games compute changed (race rules, physics, net messages): when in doubt,
   min_online = latest, as below.
   `tools/gt3r-relay/set-versions.sh <relay host> x.y.z [min_online]`
   (min_online defaults to latest, so older games must update to play online; pass
   the release URL as the 4th argument if the versions file was ever lost).
8. **Check.** A build of the new version with `GT3R_UPDATE_CHECK=1` and an
   isolated data folder (`portable.ini`) logs "Update check: this is x.y.z;
   latest x.y.z"; an older build shows the update dialog.
