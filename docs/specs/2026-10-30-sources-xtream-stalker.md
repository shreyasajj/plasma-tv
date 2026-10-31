<!--
SPDX-FileCopyrightText: 2026 shreyasajj
SPDX-License-Identifier: CC-BY-SA-4.0
-->

# Sources: the source layer, Xtream Codes and Stalker

Status: approved 2026-10-30 · Based on `origin/main` at `59af8d2` · Branch
`feat/sources-xtream-stalker` (no pull request for now)

Flimmer gains a source layer. Each kind of IPTV source is its own class
behind one interface, and the app can hold several sources at once. On top
of it come two new source types: Xtream Codes (username and password) and
Stalker (MAG portals that identify the device by a MAC address).

Merging channels that several sources carry is feature 5 and is not part of
this branch. Until then, a channel carried by two sources is shown twice.

## What happens today

- **One source:** `ServerConfig` describes exactly one source, with a
  `Backend` enum of `Tvheadend` or `M3u`. `AppSettings` stores one set of
  `server/*` keys and one password in the wallet, under `server-password`.
- **One class for everything:** `TvhBackend` (741 lines) checks `isM3u()`
  in `refresh()`, `testConnection()`, `streamUrl()`, `requestStream()` and
  `canRecord()`. It fetches channels, the guide and streams, and records.
- **Channel and event IDs:** a channel's ID (`Channel::uuid`) only has to
  be unique within one server. Event IDs (`EpgEvent::id`, the SQLite
  primary key) are TVHeadend's event IDs, or numbers the XMLTV parser
  assigns.
- **Cache:** `EpgDatabase` keeps one `server` identity. A different server
  clears the whole cache.
- **Icons:** `ChannelModel::setBaseUrl()` resolves relative channel icons
  against one base URL.
- **Credentials:** `NetworkAuth` answers login challenges with the one
  active server's credentials, and only for that server's host.
- **QML:** `SettingsPage.qml` edits the single source through
  `Settings.backend`, `host`, `port`, `username`, `playlistUrl` and
  `xmltvUrl`. `EventDialog.qml` shows Record when `Tvh.canRecord` is true.

## Plan: four commits on the branch

Each commit builds, passes `ctest` and leaves the app working.

| # | Commit | Visible change |
| --- | --- | --- |
| 1 | `Source` interface; TVHeadend and M3U code moved into `TvheadendSource` and `M3uSource` | None |
| 2 | Several sources: prefixed IDs, a guide cache per source, a list of sources in settings, migration, a Sources list in Settings | Sources list |
| 3 | `XtreamSource`, plus `User-Agent` and extra headers for M3U | New source type and M3U fields |
| 4 | `StalkerSource` | New source type |

## 1. The source interface

```cpp
// src/sources/source.h
class Source : public QObject {
    Q_OBJECT
public:
    struct Capabilities { bool record = false; };

    // Where to fetch a stream from; TvhBackend relays it (StreamProxy).
    struct StreamTarget {
        QUrl url;
        QString username, password;         // answered to login challenges
        QList<QPair<QByteArray, QByteArray>> headers; // e.g. User-Agent
    };

    explicit Source(const SourceConfig &config, QObject *parent = nullptr);
    QString id() const;                     // "src-3f2a", the prefix of its channel IDs
    QString name() const;
    virtual Capabilities capabilities() const = 0;
    // Changes when the cached channels and guide of this source become invalid.
    virtual QString identity() const = 0;

    virtual void refresh(int guideDays) = 0;      // login, channels, then guide
    virtual void requestStream(const QString &channelId) = 0;
    virtual void test() = 0;                      // emits testFinished
    virtual void record(const EpgEvent &event);   // only with capabilities().record
    virtual void cancelRecording(const EpgEvent &event);

Q_SIGNALS:
    void serverInfo(const QString &name, const QString &version);
    void channelsReady(const QVector<Channel> &channels); // IDs without prefix
    void guideReady(const QVector<EpgEvent> &events);
    void streamReady(const QString &channelId, const Source::StreamTarget &target);
    void streamFailed(const QString &channelId, const QString &message);
    void recordingChanged(qint64 eventId, const QString &dvrUuid, const QString &dvrState);
    void failed(const QString &message);          // status Offline, retried later
    void notification(const QString &message);
    void testFinished(bool ok, const QString &message);
};
```

- **Moved code:**
  - `TvheadendSource` takes `api/serverinfo`, `api/channel/grid`, the
    paged `api/epg/events/grid`, `stream/channel/<uuid>`, and the DVR
    calls (`create_by_event`, `stop`, `cancel`).
  - `M3uSource` takes the playlist, the XMLTV guide (parsed on a worker
    thread, as today) and the stream addresses from the playlist.
  - Both keep using `TvhApi` and `ServerConfig` internally, so
    `test_serverconfig`, `test_m3u` and `test_xmltv` cover them unchanged.
- **`TvhBackend`** stays the QML singleton `Tvh`, with the same properties
  and invokables. It owns the sources and the coordination code: the
  generation counter, `m_store`, `m_database`, timers and `StreamProxy`. It
  no longer knows the source types. `isM3u()` and `configFromValues()` are
  removed.
- **Streams:** `TvhBackend::requestStream()` asks the channel's source for
  a `StreamTarget`. A target with credentials, headers or a password in the
  URL goes through `StreamProxy`. Anything else plays directly, as today.
  `StreamProxy::relay()` gains a `headers` argument.
- **Recording:** `canRecord` stays for QML. It is true when the selected
  channel's source can record. `EventDialog.qml` passes the channel, which
  makes it `Tvh.canRecordChannel(uuid)`.
- **`SourceConfig`** (`src/sources/sourceconfig.h`) holds `id`, `type`
  (`"tvheadend"`, `"m3u"`, `"xtream"`, `"stalker"`), `name`, `enabled`, a
  `QVariantMap fields` for the settings, and a `QVariantMap secrets` that
  only lives in memory.

After commit 1 there is still exactly one source, made from today's
settings. The app looks and behaves the same.

## 2. Several sources

### Channel and event IDs

- **Channel IDs:** every channel ID becomes `<source id>:<source's own
  ID>`, for example `src-3f2a:1234`. `TvhBackend` adds the prefix when a
  source reports channels, and removes it before calling the source. A
  `ChannelId` helper (`src/sources/channelid.h`) does both, so no other
  code splits strings.
- **Event IDs:** event IDs from different sources would collide. Each
  source has a stable index (0–1023, stored with the source), and events
  get `index << 42 | own ID`. TVHeadend event IDs are well under 2⁴², and
  the guide keeps today's integer key. The source maps the ID back for the
  DVR calls.
- **Channel numbers:** each source keeps its own numbers. Sources are
  merged in list order. A number already taken by an earlier source gets
  the next free number after the highest one in use. Typing a number on the
  remote keeps working.

### Guide cache per source

- `EpgDatabase` replaces its single `server` value with one identity per
  source, `meta` key `source/<id>/identity`.
- When a source's identity changes, or the source is removed, only the
  rows whose channel ID starts with `<id>:` are deleted.
- Channels load from the cache per source at start, as today.

### Icons and credentials

- **Icons:** each source returns absolute icon URLs. TVHeadend's
  `imagecache/12` is resolved against that source's base URL. That makes
  `ChannelModel::setBaseUrl()` unnecessary.
- **Credentials:** `NetworkAuth` holds a list of (host, username,
  password), one per source, in place of the single active server. The
  rule stays the same: credentials only go to the host they belong to.

### Settings and migration

- **Storage:** the sources are a list in `flimmer.conf`, as
  `sources/size` and `sources/<n>/{id,type,name,enabled,index,...fields}`.
  The list order is the order in Settings.
- **Secrets:** they are stored per source as one JSON object in the
  wallet, under `source/<id>/secret`. That covers passwords, the Stalker
  MAC address, serial number, both device IDs and the signature.
- **Migration:** on the first start with no `sources/` group, the
  existing `server/*` settings become the first source, named after its
  host or playlist.
  - The `server-password` wallet entry is copied to `source/<id>/secret`,
    then deleted.
  - `playback/lastChannel` gets the new prefix.
  - The old `server/*` keys are deleted after a successful copy, so a
    failed wallet write can be retried at the next start.
- **`AppSettings`** keeps the playback and appearance settings. The
  source settings move to a new `SourceSettings` singleton
  (`QML_NAMED_ELEMENT(Sources)`). It holds the list, add, update, move,
  remove, and loading and saving of secrets. The current properties
  `Settings.backend`, `host` and so on are removed from QML.

### Settings page

- **The list:** the connection part of `SettingsPage.qml` becomes a
  **Sources** list.
  - Each row shows the name, the type, and whether the source is enabled.
  - OK opens the source's edit page. "Add source" asks for the type, then
    opens an empty edit page.
- **The edit page:** it has the type's fields (see below), plus Name,
  Enabled, Test, Save, Move up, Move down and Remove. Remove asks for
  confirmation.
- **What stays:** today's TVHeadend and M3U form becomes the edit page for
  those two types. The on-screen keyboard (`editText`) stays as it is.
- **Status:** the status card shows each source's status, for example
  "TVHeadend · Connected" or "Xtream · Account expired".

## 3. Xtream Codes

### Fields

| Field | Required | Secret |
| --- | --- | --- |
| Server address (`http(s)://host:port`) | Yes | No |
| Username | Yes | No |
| Password | Yes | Yes |

### Requests

| Step | Request | Used for |
| --- | --- | --- |
| Log in | `player_api.php?username=U&password=P` | `user_info.auth`, `status`, `exp_date`, `max_connections`, `active_cons`; `server_info` for the name and time zone |
| Categories | `…&action=get_live_categories` | Stored with each channel for later filtering. Not shown yet. |
| Channels | `…&action=get_live_streams` | `stream_id`, `name`, `num`, `stream_icon`, `epg_channel_id`, `category_id` |
| Guide | `xmltv.php?username=U&password=P` | The existing `Xmltv` parser, mapped through `epg_channel_id` |
| Stream | `<server>/live/U/P/<stream_id>.ts` | Playback. `.m3u8` is used only when `allowed_output_formats` lacks `ts`. |

### Behaviour

- **Login errors:**
  - `auth` is `0`: "Wrong username or password".
  - `status` is not `Active`: "The account is <status>", or "The account
    expired on <date>" when `exp_date` is in the past.
- **Account in use:** the stream URL holds the password, so it always goes
  through `StreamProxy`. If a stream fails while `active_cons` is at least
  `max_connections`, the message is "Your account is already in use on
  another device". The app does not try to get around the limit.
- **Identity:** the server address plus the username. A different user on
  the same panel can have different channels.
- **Missing guide:** when `xmltv.php` fails, the channels still load and a
  notification says the guide is unavailable. Fetching
  `get_short_epg` channel by channel is left for later, because it means
  one request per channel.

### M3U: User-Agent and extra headers

`M3uSource` gains two optional fields:
- **User-Agent:** one line.
- **Extra headers:** one `Name: value` per line, entered with the on-screen
  keyboard.

Both are sent with the playlist, the guide and, through `StreamProxy`, the
stream. They are not secrets, so they go to `flimmer.conf`.

## 4. Stalker (MAG portals)

### Fields

Only the portal URL and the MAC address are required. An empty optional
field is left out of every request.

| Field | Required | Secret | Sent as |
| --- | --- | --- | --- |
| Portal URL | Yes | No | Base of every request |
| MAC address | Yes, `00:1A:79:XX:XX:XX` | Yes | `mac` cookie on every request |
| STB model | No, default `MAG250` | No | `stb_type` in `get_profile`; in `User-Agent` and `X-User-Agent`. Offered as a list (`MAG250`, `MAG254`, `MAG256`, `MAG322`, `MAG324`, `MAG420`, `MAG424`) plus free text |
| Serial number | No | Yes | `sn` in `get_profile` |
| Device ID 1 | No | Yes | `device_id` in `get_profile` |
| Device ID 2 | No | Yes | `device_id2` in `get_profile` |
| Signature | No | Yes | `signature` in `get_profile` |
| Username and password | No | Password only | `action=do_auth` after `get_profile` |

### Portal address

People paste the web address of the portal, which often ends in `/c/`.
`StalkerSource` removes a trailing `/c` or `/c/` and then tries these
paths in order, remembering the first that answers the handshake:
1. `<root>/portal.php`
2. `<root>/server/load.php`
3. `<root>/stalker_portal/server/load.php`

### Every request sends

- `Cookie: mac=<MAC>; stb_lang=en; timezone=<system time zone, e.g. Europe/Berlin>`
- `User-Agent: Mozilla/5.0 (QtEmbedded; U; Linux; C) AppleWebKit/533.3 (KHTML, like Gecko) <model> stbapp ver: 2 rev: 250 Safari/533.3`
- `X-User-Agent: Model: <model>; Link: WiFi`
- `Referer: <root>/c/`
- After the handshake: `Authorization: Bearer <token>`

### Steps

1. **Handshake:** `?type=stb&action=handshake&token=&JsHttpRequest=1-xml`
   returns `js.token`.
2. **Profile:** `?type=stb&action=get_profile&hd=1&stb_type=<model>&sn=…&device_id=…&device_id2=…&signature=…&auth_second_step=1&JsHttpRequest=1-xml`.
   Only the filled-in fields are sent.
3. **Login:** when a username is set,
   `?type=stb&action=do_auth&login=<user>&password=<password>`. A false
   `js` means "Wrong username or password".
4. **Genres:** `?type=itv&action=get_genres`. They are stored with the
   channels, like Xtream categories.
5. **Channels:** `?type=itv&action=get_all_channels` returns `js.data[]`
   with `id`, `name`, `number`, `cmd`, `logo` and `xmltv_id`.
6. **Guide:** `?type=itv&action=get_epg_info&period=<guide days × 24>`
   returns events per channel ID: `start_timestamp`, `stop_timestamp`,
   `name`, `descr` and `category`. Categories map to genres with the
   existing `Xmltv` category mapping.
7. **Stream:** `?type=itv&action=create_link&cmd=<cmd, URL encoded>&forced_storage=0&disable_ad=0&JsHttpRequest=1-xml`
   returns `js.cmd`. A leading `ffmpeg ` or `auto ` is removed. The link is
   short-lived, so it is requested on every channel change and relayed with
   the same `User-Agent`, because some portals check it on the stream too.
8. **Keeping the session alive:** while connected, the source sends
   `?type=watchdog&action=get_events&init=0&cur_play_type=1&event_active_id=0`
   every 2 minutes, as MAG boxes do. Otherwise many portals drop the token.
9. **Expired token:** an "Authorization failed" reply, or a `401`, repeats
   steps 1–3 once and then retries the request. A second failure shows the
   portal's message.

**Identity:** the portal root plus the MAC address.

## Tests

Recorded replies go in `tests/data/`. I have no Xtream or Stalker account
here, so the first fixtures are written from the documented formats, with
made-up hosts and credentials. Replies from your Stalker portal can replace
them later, once anonymised.

| Test | Covers |
| --- | --- |
| `test_channelid` | Adding and removing the prefix; event ID namespacing and mapping back; renumbering numbers that clash between sources |
| `test_sourcesettings` | Migration of the old `server/*` keys into the first source, `lastChannel` prefixed, old keys removed only after success; list order, moving and removing (with a fake secret store) |
| `test_epgdatabase` (extended) | Identity per source; clearing one source keeps the others |
| `test_xtream` | Parsing the login, categories and live streams; the stream URL, with `ts` preferred; the error messages (wrong login, expired, account in use); the guide channel map |
| `test_stalker` | Portal URL normalisation and path order; the headers and cookie; empty optional fields left out; parsing the handshake, profile, channels, genres, EPG and `create_link` (with the `ffmpeg ` prefix removed); re-login once after "Authorization failed" (with a fake network) |
| `test_m3u` (extended) | User-Agent and extra headers parsed from the field text |
| Existing tests | Keep passing unchanged after commit 1 |

The network parts of `XtreamSource` and `StalkerSource` are tested against
a local fake server (`QTcpServer`), like `test_streamproxy` in PR #5. I
build and run `ctest` in the CI's Ubuntu 26.10 image. You need to check
these on a device:
- your Stalker portal;
- the Sources settings page with the remote;
- the migration of your current settings.

## Out of scope

- Merging the same channel from several sources, and falling back to
  another source. That is feature 5.
- Catch-up and video on demand from Xtream or Stalker.
- Filtering by category or genre in the UI. The data is stored for it.
- Recording from sources other than TVHeadend. That is feature 6.

## Overlap with PR #5

This branch starts from `origin/main`, without PR #5. Both change
`TvhBackend::requestStream()` and `StreamProxy::relay()`. Whichever lands
second is rebased onto the other. The `headers` argument added here fits
next to PR #5's credentials and buffer mode.

## Decisions

- The branch is pushed to the fork and its workflow run there, without a pull request.
- The four commits are reviewed together at the end.
- Tests start with replies written from the documented formats; real replies from a Stalker
  portal can replace them later.
