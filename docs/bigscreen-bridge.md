<!--
SPDX-FileCopyrightText: 2026 shreyasajj
SPDX-License-Identifier: CC-BY-SA-4.0
-->

# Local Bigscreen control

Flimmer owns the session-bus name `io.github.jclsn.Flimmer.Control` and exports
`Execute(string JSON) -> string JSON` at `/io/github/jclsn/Flimmer/Control`, using
the same interface name. A second launch asks this endpoint to focus the existing
window and exits. Plasma's external bridge confirms activation through KWin on
Wayland; a local focus request alone cannot bypass compositor focus policy.

Every request needs a nonempty string `id` (maximum 128 characters). Messages are
limited to 16 KiB and unknown fields are rejected. Completed requests are cached
(up to 128); retrying the same request ID returns the same acknowledgement without
repeating the action. Reusing an ID with different data fails. Transient
`not_ready` failures are retriable with the original ID.

```json
{"id":"state-1","command":"get_state"}
{"id":"browse-1","command":"list_channels","offset":0,"limit":50}
{"id":"focus-1","command":"focus"}
{"id":"channel-1","command":"select_channel","channel_id":"channel-<64 hex digits from list_channels>"}
{"id":"pause-1","command":"pause"}
{"id":"play-1","command":"play"}
{"id":"stop-1","command":"stop"}
```

Responses contain the request `id`, `ok`, and either `result` or `error` with a
`code` and `message`. Channel selection returns `delivery: player_accepted` only
after Main.qml accepts the operation. This acknowledges delivery, not successful
network playback. Poll `get_state` for `playing`, `paused`, `buffering`, `idle` or
`error`. Player commands are advertised dynamically: play resumes a paused player,
pause pauses a playing player, and stop clears the channel. Resume uses the normal
timeshift recorder. Seek and volume are not advertised by this endpoint.

The state includes protocol version 1, QML readiness, `catalogue_loading`, content
types, capabilities and media. The catalogue supplies `items`, `total`,
`next_offset` (null on the last page), and `loading`; pages contain at most 50
channels. A missing selection while the catalogue loads returns `not_ready`, so
the bridge can deliver a cold-start request once channels arrive.

Each channel supplies `id`, `name`, `number`, `current` and `next`. Programme
`start`/`stop` values are Unix seconds. They describe the broadcast schedule,
not a seekable playback position. Stream URLs, settings, credentials and raw
player error strings never appear in this API. Provider artwork is omitted:
credentials can appear in a logo URL path as well as its query. An authenticated
image proxy is required before enabling it.

## Stable channel identities

Automation IDs are opaque SHA-256 identifiers and independent of displayed
channel numbers. Provider IDs (including TVHeadend UUIDs) are namespaced by the
persisted source ID. M3U control identities are stored separately from the legacy
channel/guide keys, keeping existing guide matching and saved channels intact:

- A unique `tvg-id` or `channel-id` survives reordering, renumbering and renaming.
- Duplicate explicit IDs use unique channel names to disambiguate.
- Entries without an explicit ID use a unique supplied name. Renaming changes it.
- Identical ID/name pairs and unnamed entries without an ID are excluded from
  remote selection. The app can still select them with its existing UI.

Adding/removing duplicates can change automation IDs for that group. Old IDs then
fail explicitly instead of selecting another channel. Stream tokens and URLs are
never identity inputs. EPG cache schema 5 persists control IDs; older caches are
refetched, while source settings and saved last-channel keys are preserved.

## Build and verification

The normal Flatpak manifest declares only the narrow control bus name. For a
user-only development build against an installed Flimmer and KDE SDK 6.11:

```sh
git submodule update --init
sh packaging/flatpak/build-bridge-flatpak.sh --install
```

The helper builds the pinned QtKeychain dependency, the app and all Qt tests,
then packages a fresh export tree. It records the previous Flatpak commit in
`~/.cache/flimmer-bridge-build/base-commit`. The normal YAML remains the clean
production build. No system packages or sudo are needed for the helper.

Qt tests cover stable IDs, legacy parser/guide behavior, cache persistence,
EPG fields, pagination, strict schemas, replay handling, negotiated capabilities
and delayed catalogue readiness. The companion bridge repository provides
`scripts/fixture-flimmer.py`: it generates a synthetic MPEG-TS channel and XMLTV
feed, runs the real TV application in a separate profile, checks authenticated
browsing and playback/pause/timeshift resume, and restores the normal profile.
It needs ffmpeg and an active Plasma session. It does not verify availability of
the user's external IPTV provider.
