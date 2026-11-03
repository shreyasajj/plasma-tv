// SPDX-FileCopyrightText: 2026 Jan Claußen <jan.claussen10@web.de>
//
// SPDX-License-Identifier: GPL-2.0-only OR GPL-3.0-only OR LicenseRef-KDE-Accepted-GPL

import QtQuick
import QtMultimedia
import Flimmer

// Video layer. Lives outside the scaled UI so it always renders at native resolution.
Item {
    id: root
    property string channelUuid
    property string error
    property bool awaitingUrl: false
    // Reconnect attempts for the current channel before an error is shown.
    property int retries: 0
    readonly property int maxRetries: 3
    readonly property bool active: channelUuid.length > 0
    readonly property alias mediaPlayer: player
    signal notice(string text)

    // The relay keeps the channel on disk: always while rewinding is on, and from a pause
    // with it off. Playing from an earlier moment means a new source with that offset.
    property bool paused: false
    // The stream can be played from disk; with rewinding off only after a pause.
    property bool held: false
    // How far behind live the current source started, and the time paused since.
    property double sourceBehindMs: 0
    property double pausedMs: 0
    property double pausedAt: 0
    // Updated by a timer while behind live, for the time shown on screen.
    property double behindMs: 0
    readonly property bool timeshifting: behindMs > 0
    readonly property bool rewindOn: Settings.rewindMinutes > 0

    // A plain pause (nothing kept) can't be caught up with, so it doesn't count.
    function currentBehindMs() {
        if (!held)
            return 0;
        return sourceBehindMs + pausedMs + (paused ? Date.now() - pausedAt : 0);
    }

    // The most that can be rewound right now.
    function windowMs() {
        return held ? Tvh.rewindWindowMs() : 0;
    }

    function togglePause() {
        if (!active || awaitingUrl)
            return;

        if (!paused) {
            player.pause();
            paused = true;
            pausedAt = Date.now();

            // With rewinding on everything is kept already; otherwise from now on, if it can be.
            held = rewindOn ? Tvh.rewindAvailable : (held || Tvh.holdStream());
            behindMs = currentBehindMs();

            return;
        }

        pausedMs += Date.now() - pausedAt;
        paused = false;

        // The player's own connection lost what came meanwhile: continue from disk.
        if (held && currentBehindMs() > 500) {
            playFrom(currentBehindMs());

            return;
        }

        player.play();
    }

    // Plays 'ms' behind live, within what was kept. 0 is live.
    function playFrom(ms) {
        const behind = Math.max(0, Math.min(ms, windowMs()));
        paused = false;
        pausedMs = 0;
        sourceBehindMs = behind;
        behindMs = behind;
        player.source = Tvh.timeshiftUrl(behind);
        player.play();
    }

    function goLive() {
        if (held)
            playFrom(0);
        else if (paused)
            togglePause();
    }

    function endTimeshift() {
        paused = false;
        held = false;
        sourceBehindMs = 0;
        pausedMs = 0;
        behindMs = 0;
    }

    function play(uuid) {
        if (uuid !== channelUuid)
            retries = 0;

        endTimeshift();
        reconnect.stop();
        player.stop();
        player.source = "";
        channelUuid = uuid;
        error = "";
        awaitingUrl = true;
        Tvh.requestStream(uuid);
    }

    // Manual retry from the error screen gets a fresh set of reconnect attempts.
    function retry() {
        retries = 0;
        reconnectNow();
    }

    function reconnectNow() {
        if (channelUuid.length > 0)
            play(channelUuid);
    }

    function scheduleReconnect(message) {
        if (retries < maxRetries) {
            retries++;
            reconnect.restart();
        } else {
            error = message;
        }
    }

    function stop() {
        endTimeshift();
        Tvh.stopStream();
        reconnect.stop();
        player.stop();
        player.source = "";
        channelUuid = "";
        error = "";
        awaitingUrl = false;
    }

    // Cycle helpers return a human readable description for the OSD.
    function trackName(meta, index) {
        const lang = meta ? meta.stringValue(MediaMetaData.Language) : "";
        const title = meta ? meta.stringValue(MediaMetaData.Title) : "";
        const parts = [lang, title].filter(s => s && s.length > 0);
        return parts.length > 0 ? parts.join(" – ") : qsTr("Track %1").arg(index + 1);
    }

    function cycleAudio() {
        const n = player.audioTracks.length;
        if (n < 2)
            return qsTr("Only one audio track");
        player.activeAudioTrack = (player.activeAudioTrack + 1) % n;
        return qsTr("Audio: %1").arg(trackName(player.audioTracks[player.activeAudioTrack], player.activeAudioTrack));
    }

    function cycleSubtitles() {
        const n = player.subtitleTracks.length;
        if (n === 0)
            return qsTr("No subtitles available");
        const next = player.activeSubtitleTrack + 1;
        player.activeSubtitleTrack = next >= n ? -1 : next;
        if (player.activeSubtitleTrack < 0)
            return qsTr("Subtitles off");
        return qsTr("Subtitles: %1").arg(trackName(player.subtitleTracks[player.activeSubtitleTrack], player.activeSubtitleTrack));
    }

    Rectangle {
        anchors.fill: parent
        color: "black"
        visible: root.active
    }

    VideoOutput {
        id: video
        anchors.fill: parent
        fillMode: VideoOutput.PreserveAspectFit
        visible: root.active
    }

    MediaPlayer {
        id: player
        videoOutput: video
        audioOutput: AudioOutput {
            id: audio
        }

        onErrorOccurred: (error, errorString) => {
            // Errors while a new stream is being requested belong to the cancelled one, and
            // the server may drop a paused live stream.
            if (!root.active || root.awaitingUrl || root.paused)
                return;
            // IPTV sources and busy tuners often fail once; reconnect with a fresh ticket.
            root.scheduleReconnect(errorString.length > 0 ? errorString : qsTr("Playback failed"));
        }
        onMediaStatusChanged: {
            if (mediaStatus === MediaPlayer.BufferedMedia)
                root.retries = 0;

            if (mediaStatus !== MediaPlayer.EndOfMedia || !root.active || root.paused)
                return;

            // Playing from disk only ends when nothing more is kept; continue with live TV.
            if (root.sourceBehindMs > 0) {
                root.play(root.channelUuid);

                return;
            }

            // A live stream never ends on its own; reconnect a few times.
            root.scheduleReconnect(qsTr("The stream ended"));
        }
    }

    Timer {
        interval: 500
        repeat: true
        running: root.paused || root.sourceBehindMs > 0
        onTriggered: root.behindMs = root.currentBehindMs()
    }

    Connections {
        target: Tvh
        // With rewinding on, the window becomes available once the format is known.
        function onRewindChanged() {
            if (root.rewindOn && root.active)
                root.held = root.held || Tvh.rewindAvailable;
        }
    }

    Timer {
        id: reconnect
        interval: 1500
        onTriggered: root.reconnectNow()
    }

    Connections {
        target: Tvh
        function onStreamReady(uuid, url) {
            if (uuid !== root.channelUuid)
                return;
            root.awaitingUrl = false;
            player.source = url;
            player.play();
        }
        function onStreamFailed(uuid, message) {
            if (uuid !== root.channelUuid)
                return;
            root.awaitingUrl = false;
            root.error = message;
        }
    }
}
