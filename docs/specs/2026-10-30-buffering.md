<!--
SPDX-FileCopyrightText: 2026 shreyasajj
SPDX-License-Identifier: CC-BY-SA-4.0
-->

# Buffering: playback buffer, silent reconnect and rewinding live TV

Status: approved 2026-10-30 · Based on `origin/main` at `59af8d2`

Short network drops and slow IPTV servers should not freeze the picture or
show an error. Playback starts instantly, and the app builds a buffer only
after it has seen the connection stall. Optionally, the last minutes of the
channel are kept on disk so the viewer can rewind live TV.

The work has two steps. Step 1 (buffer and silent reconnect) is useful on its
own. Step 2 (rewind) builds on it.

## Decisions taken from the proposal review

| Question | Answer | What this spec does |
| --- | --- | --- |
| 5-second default buffer, or Off? | "Default should be instant, but can it tell how much buffer is needed from network feedback?" | New default **Auto**: no delay at start; the buffer grows after each stall (0 → 2 → 5 → 10 s). Off, 2, 5 and 10 s stay available. |
| Is rewinding live TV wanted (about 1.8 GB per 30 min)? | "Make it configurable." Later: default Off; stop when running out of space, and make that configurable. | Step 2 is built, with a length setting, **Off by default**. Rewinding stops when free disk space falls under a limit the viewer sets. |
| Should TVHeadend stop using tickets so reconnects work? | "Don't worry about that right now." | Relayed with credentials, no ticket (1.1). |

## What happens today

- `TvhBackend::requestStream()` hands the player one of three addresses: the
  server's own URL (no login), a TVHeadend ticket URL, or a `StreamProxy`
  address (M3U on the playlist's host with a username, or TVHeadend without
  ticket support).
- `StreamProxy` copies bytes from upstream to the player. It has a 2 MB
  read-ahead limit but no deliberate delay, and when upstream ends it closes
  the player's connection.
- `PlayerView.qml` then sees an error or the end of the stream. It waits
  1.5 s and requests the stream again, up to 3 times. That takes several
  seconds and the picture is black meanwhile.
- Pausing calls `StreamRecorder::start()`. This opens a **second** upstream
  connection and writes it to `~/Videos/TV recordings/Paused - ….ts`. Resuming
  plays that file through a small local HTTP server. Many IPTV accounts allow
  only one connection, so the second connection can fail or end the first.

## Step 1: playback buffer and silent reconnect

### 1.1 Every stream goes through the relay

`requestStream()` always returns a `StreamProxy` address. The only exception
is HLS (`.m3u8`), described below. The relay sends credentials only where it
does today: to the server's own host. The changes to the address handling:

- **TVHeadend with a username:** the relay fetches the plain
  `stream/channel/<uuid>?profile=…` URL with the stored credentials, instead
  of first asking for a ticket. A ticket only lasts a few minutes, so a
  reconnect to a ticket URL would fail. This is the code path that older
  servers already use today.
- **TVHeadend without a username and M3U:** relayed without credentials.
- **Credentials in an M3U URL** (`http://user:pass@host/…`): the relay
  removes them from the URL and sends them as login data. FFmpeg then no
  longer prints them to the log.
- **HLS:** a URL whose path ends in `.m3u8` goes to the player directly, as
  today. FFmpeg already buffers HLS segment by segment, and a relayed
  playlist would break relative segment addresses. The relay also checks
  the type of the reply. If a URL without `.m3u8` turns out to be an HLS
  playlist (`application/vnd.apple.mpegurl` or `audio/mpegurl`), the relay
  answers `302` with the final address. If that host needs credentials, it
  passes the playlist through unchanged, as it does today.

### 1.2 The buffer

A new class, `StreamBuffer` (`src/streambuffer.{h,cpp}`), does the buffering
without any network code, so it can be unit tested. `StreamProxy` keeps one
`StreamBuffer` for each player connection.

- **Measuring seconds.** Most live IPTV is MPEG-TS. The buffer splits the data
  into 188-byte packets and reads the PCR timestamps. The length of the
  buffer is the last PCR received minus the last PCR sent. Servers often
  send the first seconds faster than real time, so byte counts or wall-clock
  time would be wrong. If the data is not MPEG-TS, or has no PCR within
  1 MB, the buffer uses the average byte rate of the last 10 seconds.
- **Filling.** Before sending, and again after the buffer has run empty,
  the relay holds data until the buffer reaches its *target*. Then it sends
  as fast as the player reads.
- **Target by mode** (setting `playback/buffer`):

  | Mode | Target at start | After the buffer runs empty |
  | --- | --- | --- |
  | Auto (default) | 0 s, plays instantly | Grows to the next step: 2 s, then 5 s, then 10 s, which is the most |
  | Off | 0 s | 0 s, so data is passed on as it arrives |
  | 2 / 5 / 10 s | that many seconds | the same number of seconds |

  In Auto, the first stall on a channel is still visible. After it, the
  picture runs that many seconds behind live and the next stall of that
  length is hidden. The target resets on a channel change. It never shrinks
  during playback, because shrinking would mean skipping video.
- **Memory.** The buffer stops reading from upstream at 1.5 × target, with a
  minimum of 4 MB. That is 10 s at 20 Mbit/s ≈ 38 MB at most, usually under
  15 MB.

### 1.3 Silent reconnect

When upstream fails after the player has already received data, the relay
keeps the connection to the player open and reconnects by itself:

- **What counts as a failure:** the reply ends or fails, or no data arrives
  for 4 s. The current `setTransferTimeout(30000)` stays for the first
  response only.
- **Retries:** 5 attempts, after 0.5, 1, 2, 2 and 2 s. The player keeps
  playing from the buffer while the relay reconnects. With 0 s buffered, the
  picture freezes but no error is shown.
- **Joining the streams:** before reconnecting, the buffer drops a partial
  TS packet at its end. On the new connection, it skips bytes until it finds
  the packet sync byte (`0x47`) at a 188-byte rhythm. FFmpeg treats the gap
  as a discontinuity and carries on. Non-TS streams are appended as they
  come.
- **Giving up:** a `401`/`403`/`404`, or 5 failed attempts, closes the
  player's connection. `PlayerView.qml`'s existing reconnect (1.5 s, 3 tries,
  fresh `requestStream()`) then runs as the last resort. It needs no changes.

### 1.4 Settings

- `AppSettings` gains `playbackBuffer`, stored as `playback/buffer`:
  `-1` = Auto (default), `0` = Off, `2`, `5`, `10`. Other values are clamped
  to the nearest of these.
- The Settings page gains the row **Playback buffer** below "Stop playback in
  background". Its values are *Auto*, *Off*, *2 seconds*, *5 seconds* and
  *10 seconds*, and Left/Right change them like the "Guide days" row. The
  description reads "Starts instantly and waits longer only after the
  connection stalls" for Auto, and "The picture runs N seconds behind live"
  for fixed values.
- `relay()` takes the buffer mode as an argument, so `StreamProxy` does not
  read `AppSettings` and stays testable.

### 1.5 Not changed in Step 1

Pausing still uses `StreamRecorder` and its second connection until Step 2
replaces it. Recording on the TVHeadend server is unchanged.

## Step 2: rewinding live TV

### 2.1 Behaviour

- Setting **Rewind live TV**: *Off* (default), *15 minutes*, *30 minutes*,
  *1 hour* and *2 hours*, stored as `playback/rewindMinutes`. The
  description shows the disk use at 8 Mbit/s, for example "about 1.8 GB for
  30 minutes".
- When it is on, the current channel is written to disk from the moment it
  starts. Changing the channel discards the old channel's file, so only one
  channel is ever kept.
- **Keys while watching:**

  | Key | Action |
  | --- | --- |
  | Rewind | back 10 s |
  | Fast Forward | forward 30 s; at the live edge it does nothing and shows "Live" |
  | Left / Right while paused | back 10 s / forward 30 s |
  | Play/Pause, OK | pause and resume, as today |

  Left and Right while playing keep their current jobs (channel list,
  guide). Key presses within 0.6 s are added up into one jump, and a bar
  shows the position in the rewind window, such as "−2:30".
- When behind live, the existing behind-live icon stays visible, with the
  time behind live next to it.
- Rewinding past the start of the window stops at the oldest data.

### 2.2 One upstream connection, kept on disk

The relay already has every byte, so it writes them to the rewind file. The
separate download in `StreamRecorder` goes away. This also fixes the
second-connection problem for pausing.

- **Session.** `StreamProxy` keeps a *session* for each relay token: the
  upstream connection with its reconnect logic (1.3), plus the optional
  rewind file. Player connections attach to a session. If the player
  disconnects, the session ends after 5 s. The exception is a session that
  is recording for rewind or a pause: it lives until `PlayerView` plays
  another channel or stops. A new `StreamProxy::endSession(token)`, called
  through `TvhBackend`, ends it.
- **Rewind file.** A new class, `TimeshiftBuffer` (`src/timeshiftbuffer.{h,cpp}`),
  writes the aligned TS packets into 1-minute chunk files in
  `QStandardPaths::CacheLocation + "/timeshift/"`. Each chunk keeps an index
  of PCR time to file offset, one entry per second. Chunks older than the
  rewind length are deleted. Leftover chunks from a crash are removed at
  start. Before each new chunk, it checks the free space with `QStorageInfo`.
  When free space would fall under the limit, rewinding stops with the
  notice "Rewind stopped: the disk is nearly full". Live playback continues.
  The limit is the setting **Keep free disk space**: *1 GB* (default), *2 GB*,
  *5 GB* or *10 GB*, stored as `playback/rewindMinFreeGb`. The row is only
  shown while Rewind live TV is on.
- **Playing from the past.** The relay address takes an offset:
  `http://127.0.0.1:<port>/<token>?behind=<ms>`. The relay serves from the
  file position for that time and then follows the file as it grows, as
  `StreamRecorder::serve()` does today. `behind=0` or no offset is live,
  through the Step 1 buffer. A jump means `PlayerView` sets a new source
  address. A live stream can't be seeked in Qt Multimedia, so this is the
  only way to jump.
- **Pausing.**
  - Rewind off: pausing starts a pause-only file in the same cache folder
    from the paused moment. Resuming plays it with `behind=<pause length>`.
    Changing the channel or stopping deletes it.
  - Rewind on: pausing only pauses the player. The session keeps writing,
    and resuming continues from the same place.
- **StreamRecorder** is removed. `PlayerView` keeps `paused` and
  `timeshifting` (now meaning "behind > 0") and gains `behindMs`. Paused
  files are no longer written to `~/Videos`, and the existing leftover
  cleanup of `Paused - *.ts` there stays for one release.

## Tests (added to `tests/CMakeLists.txt`)

| Test | Covers |
| --- | --- |
| `test_streambuffer` | TS packet alignment and resync after junk; PCR duration, including PCR wrap-around; byte-rate fallback for non-TS data; holding data until the target is reached; Auto growing 0→2→5→10 s after each time it runs empty; the memory limit; dropping a partial packet before a reconnect |
| `test_streamproxy` | A local fake server (`QTcpServer`) serves the stream. It checks pass-through, that credentials go only to the server's host, removal of credentials from the URL, 404 for an unknown token, the 302 for HLS, reconnect after a dropped upstream while the player connection stays open, and closing the connection after 5 failures or a 401 |
| `test_timeshiftbuffer` (Step 2) | Rotating chunks and deleting by length; looking up a file position for `behind=<ms>`; reading as the file grows; leftover cleanup; stopping when the disk is full (simulated with a small quota hook) |
| `test_serverconfig` (extended) | Clamping the new setting values, if the parsing lives there |

QML behaviour (keys, the jump bar) and real playback can't be covered by
these tests. I'll build and run `ctest` in the CI's Ubuntu 26.10 image. The
playback check on a Bigscreen device against a TVHeadend server and an M3U
source needs you, because I have no display or stream here.

## Pull requests

1. **Step 1:** `StreamBuffer`, the relay for all streams, silent reconnect,
   and the Playback buffer setting.
2. **Step 2:** `TimeshiftBuffer`, sessions, rewind keys and the setting, and
   `StreamRecorder` removed. It starts from `origin/main` once PR 1 is merged.

## Out of scope

- Shrinking the delay in Auto by playing slightly faster. Qt Multimedia's
  playback rate on live streams is unreliable, and it would change the
  audio pitch.
- Rewind for HLS streams.
- Keeping rewind data across channel changes or app restarts.
- Recording to `~/Videos`, Nextcloud or a NAS. That is feature 6.

