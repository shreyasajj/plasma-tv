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

    // Pausing records the channel to a file; resuming plays that file, so the
    // programme continues where it was paused.
    property bool paused: false
    property bool timeshifting: false   // playing the recording instead of live TV

    function togglePause() {
        if (!active || awaitingUrl)
            return;

        if (!paused) {
            player.pause();
            paused = true;

            if (!timeshifting && !recorder.recording) {
                recorder.start(player.source, Tvh.channels.get(Tvh.channels.indexOfUuid(channelUuid)).name ?? "");
            }

            return;
        }

        paused = false;

        // Without a recording, the paused live stream simply continues.
        if (timeshifting || !recorder.recording) {
            player.play();

            return;
        }

        timeshifting = true;
        player.source = recorder.url;
        player.play();
    }

    function endTimeshift() {
        if (timeshifting) {
            player.stop();
            player.source = "";
        }

        recorder.stop();
        timeshifting = false;
        paused = false;
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
            // E.g. an Xtream account used on another device; replaces the message if so.
            Tvh.explainStreamError(channelUuid);
        }
    }

    function stop() {
        endTimeshift();
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

            // The recording only ends when it broke off; continue with live TV.
            if (root.timeshifting) {
                root.play(root.channelUuid);

                return;
            }

            // A live stream never ends on its own; reconnect a few times.
            root.scheduleReconnect(qsTr("The stream ended"));
        }
    }

    StreamRecorder {
        id: recorder
        onFailed: message => root.notice(message)
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
