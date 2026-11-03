// SPDX-FileCopyrightText: 2026 Jan Claußen <jan.claussen10@web.de>
//
// SPDX-License-Identifier: GPL-2.0-only OR GPL-3.0-only OR LicenseRef-KDE-Accepted-GPL

import QtQuick
import QtQuick.Layouts
import org.kde.kirigami as Kirigami
import org.kde.bigscreen.ui as BigscreenUI
import Flimmer

// On-screen display while watching: info bar, zapping, channel list overlay.
FocusScope {
    id: root
    required property Item player
    signal leave(string page)

    // OSD sits on top of video, so use Plasma's complementary (dark) colors.
    Kirigami.Theme.colorSet: Kirigami.Theme.Complementary
    Kirigami.Theme.inherit: false

    // Channel chosen with up/down whose stream hasn't been started yet.
    property string pendingUuid
    readonly property string shownUuid: pendingUuid.length > 0 ? pendingUuid : player.channelUuid
    readonly property int channelIndex: {
        Tvh.channels.count;
        return Tvh.channels.indexOfUuid(shownUuid);
    }
    readonly property var channel: {
        Tvh.now;
        Tvh.epgRevision;
        return Tvh.channels.get(channelIndex);
    }
    // Counts every second while the info bar or the jump bar is shown.
    property double clockMs: Date.now()
    // The moment on screen, and the oldest one that can be rewound to.
    readonly property double playingMs: player.paused ? player.scrubAtMs : clockMs - player.behindMs
    readonly property double oldestMs: player.held ? clockMs - player.windowMs() : clockMs
    // The programme being watched, which is an earlier one after rewinding past its start.
    readonly property var nowEvent: {
        Tvh.now;
        Tvh.epgRevision;
        return Tvh.eventAt(shownUuid, pendingUuid.length > 0 ? clockMs : playingMs);
    }
    readonly property var nextEvent: {
        Tvh.now;
        Tvh.epgRevision;
        return Tvh.nextEvent(shownUuid, nowEvent.start ?? Date.now());
    }
    property bool infoShown: false

    // Behind live after the presses of the last 0.6 s, which are played as one jump.
    property double jumpTargetMs: 0
    readonly property int rewindStepMs: 10000
    readonly property int forwardStepMs: 30000

    function play(uuid) {
        zapDelay.stop();
        pendingUuid = "";
        player.play(uuid);
        Settings.lastChannel = uuid;
        showInfo();
    }

    // Up/down only update the info bar; the stream starts once the user stops
    // pressing, so zapping through channels doesn't open a stream for each one.
    function zap(delta) {
        const n = Tvh.channels.count;
        if (n === 0)
            return;
        const i = channelIndex < 0 ? 0 : (channelIndex + delta + n) % n;
        pendingUuid = Tvh.channels.uuidAt(i);
        zapDelay.restart();
        showInfo();
    }

    // Stays on the whole time while paused.
    function showInfo() {
        infoShown = true;
        jumpBar.visible = false;

        if (player.paused)
            infoTimer.stop();
        else
            infoTimer.restart();
    }

    // Left, Right, Rewind and Fast Forward: the chosen moment while paused, a jump otherwise.
    function moveInTime(deltaMs) {
        if (player.paused) {
            if (player.scrubBy(deltaMs))
                showInfo();
            else if (deltaMs < 0 && player.rewindOn && !player.held)
                flash(qsTr("Rewind isn't available for this channel"));
        } else {
            queueJump(deltaMs);
        }
    }

    function togglePause() {
        player.togglePause();
        showInfo();
    }

    // The pause buttons have the focus.
    readonly property bool inPauseButtons: playHereButton.activeFocus || toLiveButton.activeFocus || recordButton.activeFocus

    // From the pause buttons back to the bar, where Left and Right move in time.
    function focusBar() {
        // The overlay is a focus scope that would hand the keys back to the button.
        playHereButton.focus = false;
        toLiveButton.focus = false;
        recordButton.focus = false;
        root.forceActiveFocus();
    }

    // "2:30" or "1:02:30".
    function formatBehind(ms) {
        const total = Math.round(ms / 1000);
        const h = Math.floor(total / 3600);
        const m = Math.floor(total / 60) % 60;
        const sec = String(total % 60).padStart(2, "0");
        return h > 0 ? h + ":" + String(m).padStart(2, "0") + ":" + sec : m + ":" + sec;
    }

    // Rewind (negative) or move forward; presses close together add up.
    function queueJump(deltaMs) {
        if (!player.held) {
            if (deltaMs < 0 && player.rewindOn)
                flash(qsTr("Rewind isn't available for this channel"));
            return;
        }
        if (deltaMs < 0 && !player.rewindOn)
            return;

        const from = jumpTimer.running ? jumpTargetMs : player.currentBehindMs();
        // With rewinding off, only the pause can be caught up with.
        const oldest = player.rewindOn ? player.windowMs() : player.currentBehindMs();
        jumpTargetMs = Math.max(0, Math.min(oldest, from - deltaMs));
        jumpTimer.restart();
        // One bar at a time at the bottom.
        infoShown = false;
        jumpBar.show();
    }

    // "−2:30 behind live · 6:10 kept", or "6:10 kept" while live; empty with nothing kept.
    function keptText(behindMs) {
        if (!player.held)
            return "";
        const kept = qsTr("%1 kept").arg(formatBehind(clockMs - oldestMs));
        return behindMs > 0 ? qsTr("−%1 behind live").arg(formatBehind(behindMs)) + "  ·  " + kept : kept;
    }

    function flash(text) {
        feedback.show(text, "");
        showInfo();
    }

    function openZapList() {
        zapList.currentIndex = Math.max(0, channelIndex);
        zapList.positionViewAtIndex(zapList.currentIndex, ListView.Center);
        zapPanel.shown = true;
        zapList.forceActiveFocus();
        zapTimer.restart();
    }

    function closeZapList() {
        zapTimer.stop();
        zapPanel.shown = false;
        // The overlay is a focus scope that remembers the list as its focused
        // child; without clearing that, focusing the overlay would hand the keys
        // straight back to the hidden list.
        zapList.focus = false;
        // Only reclaim focus while watching; the overlay may already be hidden.
        if (root.visible)
            root.forceActiveFocus();
    }

    onVisibleChanged: {
        if (!visible) {
            zapTimer.stop();
            zapPanel.shown = false;
            zapList.focus = false;
        }
    }

    Keys.onPressed: event => {
        // Keys the pause buttons didn't use stay with them, so the arrows don't zap or jump.
        if (inPauseButtons) {
            event.accepted = true;
            return;
        }

        switch (event.key) {
        case Qt.Key_Up:
        case Qt.Key_ChannelUp:
        case Qt.Key_PageUp:
            zap(1);
            break;
        case Qt.Key_Down:
            // While paused, Down goes to the buttons under the bar.
            if (player.paused) {
                showInfo();
                playHereButton.forceActiveFocus();
            } else {
                zap(-1);
            }
            break;
        case Qt.Key_ChannelDown:
        case Qt.Key_PageDown:
            zap(-1);
            break;
        case Qt.Key_Left:
            // While paused with something kept, Left and Right move the chosen moment.
            if (player.paused && player.held)
                moveInTime(-rewindStepMs);
            else
                openZapList();
            break;
        case Qt.Key_Right:
            if (player.paused && player.held)
                moveInTime(forwardStepMs);
            else
                leave("guide");
            break;
        case Qt.Key_AudioRewind:
        case Qt.Key_R:
            moveInTime(-rewindStepMs);
            break;
        case Qt.Key_AudioForward:
        case Qt.Key_F:
            moveInTime(forwardStepMs);
            break;
        case Qt.Key_Info:
        case Qt.Key_I:
            if (infoShown)
                infoShown = false;
            else
                showInfo();
            break;
        case Qt.Key_AudioCycleTrack:
        case Qt.Key_A:
            flash(player.cycleAudio());
            break;
        case Qt.Key_Subtitle:
        case Qt.Key_S:
            flash(player.cycleSubtitles());
            break;
        case Qt.Key_MediaPlay:
        case Qt.Key_MediaPause:
        case Qt.Key_MediaTogglePlayPause:
        case Qt.Key_P:
            togglePause();
            break;
        case Qt.Key_MediaStop:
        case Qt.Key_X:
            player.stop();
            leave("channels");
            break;
        default:
            if (Theme.isAccept(event.key) && !event.isAutoRepeat) {
                if (player.error.length > 0)
                    player.retry();
                else
                    togglePause();
            } else if (Theme.isBack(event.key)) {
                if (player.scrubbed)
                    player.resetScrub();
                else if (Settings.confirmQuit)
                    quitMenu.open();
                else if (infoShown)
                    infoShown = false;
                else
                    leave("channels");
            } else {
                return;
            }
        }
        event.accepted = true;
    }

    MouseArea {
        anchors.fill: parent
        onClicked: root.infoShown ? (root.infoShown = false) : root.showInfo()
        onDoubleClicked: root.leave("channels")
    }

    Timer {
        id: infoTimer
        interval: 6000
        onTriggered: root.infoShown = false
    }
    Timer {
        interval: 1000
        repeat: true
        triggeredOnStart: true
        running: root.visible && (root.infoShown || jumpBar.visible)
        onTriggered: root.clockMs = Date.now()
    }
    Timer {
        id: jumpTimer
        interval: 600
        onTriggered: root.player.playFrom(root.jumpTargetMs)
    }
    Timer {
        id: zapTimer
        interval: 15000
        onTriggered: root.closeZapList()
    }
    Timer {
        id: zapDelay
        interval: 450
        onTriggered: {
            if (root.pendingUuid === root.player.channelUuid && root.player.error.length === 0) {
                root.pendingUuid = "";
            } else {
                root.play(root.pendingUuid);
            }
        }
    }

    // --- Starting or refilling: under everything else ----------------------------
    LoadingScreen {
        anchors.fill: parent
        player: root.player
    }

    // --- Error ------------------------------------------------------------
    Rectangle {
        anchors.centerIn: parent
        visible: root.player.error.length > 0 && !zapPanel.shown
        width: 667
        height: errorColumn.implicitHeight + 53
        radius: Theme.radius + 5
        color: Kirigami.Theme.backgroundColor
        opacity: 0.95

        BigscreenUI.PlaceholderMessage {
            id: errorColumn
            anchors.centerIn: parent
            width: parent.width - 53
            iconName: "dialog-error"
            text: qsTr("Playback problem")
            explanation: root.player.error + "\n\n" + qsTr("Press OK to retry, BACK to return")
        }
    }

    // --- Short feedback (audio track, …) ------------------------------------
    BigscreenUI.Toast {
        id: feedback
        anchors.bottom: undefined
        anchors.top: parent.top
        anchors.topMargin: 43
        timeout: 3000
    }

    Connections {
        target: root.player
        function onNotice(text) {
            root.flash(text);
        }
        function onPausedChanged() {
            if (!root.player.paused && root.inPauseButtons)
                root.focusBar();
            root.showInfo();
        }
    }

    // --- BACK: quit (BACK again keeps watching) --------------------------------
    BigscreenUI.Dialog {
        id: quitMenu
        title: qsTr("Quit Live TV?")
        openFocusItem: quitButton
        // Only as wide as the question; the default width is meant for longer content.
        width: Math.max(implicitHeaderWidth, implicitContentWidth + leftPadding + rightPadding)

        onAboutToHide: {
            if (root.visible)
                root.forceActiveFocus();
        }

        contentItem: RowLayout {
            spacing: 11
            BigscreenUI.KeyNavigationGroup.orientation: Qt.Horizontal

            TvButton {
                id: quitButton
                text: qsTr("Quit")
                icon.name: "application-exit"
                onActivated: {
                    // Also deletes the recording made while paused.
                    root.player.stop();
                    Qt.quit();
                }
            }
        }
    }

    // --- Position while jumping ---------------------------------------------
    Rectangle {
        id: jumpBar
        function show() {
            visible = true;
            jumpBarTimer.restart();
        }

        anchors.horizontalCenter: parent.horizontalCenter
        anchors.bottom: parent.bottom
        anchors.bottomMargin: 43
        width: Math.min(parent.width - 2 * Theme.margin, 1067)
        height: 80
        radius: Theme.radius
        color: Kirigami.Theme.backgroundColor
        opacity: 0.9
        visible: false

        Timer {
            id: jumpBarTimer
            interval: 3000
            onTriggered: jumpBar.visible = false
        }

        // Behind live where the presses lead to, while they are added up.
        readonly property double shownBehindMs: jumpTimer.running ? root.jumpTargetMs : root.player.behindMs
        readonly property var event: {
            Tvh.epgRevision;
            return Tvh.eventAt(root.player.channelUuid, root.clockMs - shownBehindMs);
        }

        ProgrammeBar {
            anchors.left: parent.left
            anchors.right: jumpText.left
            anchors.margins: 27
            anchors.verticalCenter: parent.verticalCenter
            startMs: jumpBar.event.start ?? 0
            stopMs: jumpBar.event.stop ?? 0
            playingMs: root.clockMs - jumpBar.shownBehindMs
            liveMs: root.clockMs
            oldestMs: root.oldestMs
        }
        Text {
            id: jumpText
            anchors.right: parent.right
            anchors.rightMargin: 27
            anchors.verticalCenter: parent.verticalCenter
            text: jumpBar.shownBehindMs > 0 ? root.keptText(jumpBar.shownBehindMs) : qsTr("Live") + "  ·  " + root.keptText(0)
            color: Kirigami.Theme.textColor
            font.pixelSize: Theme.fontNormal
            font.weight: Font.DemiBold
        }
    }

    // --- Paused / behind live ------------------------------------------------
    BigscreenUI.Badge {
        anchors.right: parent.right
        anchors.top: parent.top
        anchors.margins: Theme.margin
        visible: root.player.paused
        type: BigscreenUI.Label.Heading
        text: qsTr("❚❚  Paused")
    }

    // Watching behind live: a quiet hint with the time behind, it stays on the whole time.
    Row {
        anchors.right: parent.right
        anchors.top: parent.top
        anchors.margins: Theme.margin
        spacing: 8
        visible: root.player.timeshifting && !root.player.paused
        opacity: 0.5

        Text {
            anchors.verticalCenter: parent.verticalCenter
            text: "−" + root.formatBehind(root.player.behindMs)
            color: "white"
            font.pixelSize: Theme.fontSmall
        }
        Kirigami.Icon {
            implicitWidth: 32
            implicitHeight: 32
            // Breeze has no clock with an arrow turning back. Drawn white, as it is only shown on video.
            source: Qt.resolvedUrl("../data/icons/symbolic/behind-live.svg")
        }
    }

    // --- Info bar ---------------------------------------------------------
    Item {
        id: info
        anchors.left: parent.left
        anchors.right: parent.right
        anchors.bottom: parent.bottom
        // Room for the pause buttons and their hint.
        height: root.player.paused ? 373 : 253
        opacity: root.infoShown && !zapPanel.shown ? 1 : 0
        visible: opacity > 0
        Behavior on opacity {
            NumberAnimation {
                duration: Theme.animNormal
            }
        }

        Rectangle {
            anchors.fill: parent
            gradient: Gradient {
                GradientStop {
                    position: 0
                    color: "transparent"
                }
                GradientStop {
                    position: 0.45
                    color: Qt.rgba(0, 0, 0, 0.7)
                }
                GradientStop {
                    position: 1
                    color: Qt.rgba(0, 0, 0, 0.9)
                }
            }
        }

        ChannelLogo {
            id: osdLogo
            anchors.left: parent.left
            anchors.leftMargin: Theme.margin
            anchors.bottom: parent.bottom
            anchors.bottomMargin: 48
            width: 147
            height: 88
            source: root.channel.iconUrl ?? ""
            name: root.channel.name ?? ""
        }

        Column {
            anchors.left: osdLogo.right
            anchors.leftMargin: 27
            anchors.right: clock.left
            anchors.rightMargin: 27
            anchors.bottom: parent.bottom
            anchors.bottomMargin: 43
            spacing: 7

            Text {
                text: (root.channel.number > 0 ? root.channel.number + "  " : "") + (root.channel.name ?? "")
                color: Kirigami.Theme.highlightColor
                font.pixelSize: Theme.fontNormal
                font.weight: Font.DemiBold
            }
            Text {
                width: parent.width
                text: root.nowEvent.title ?? qsTr("No programme information")
                color: "white"
                font.pixelSize: Theme.fontTitle
                font.weight: Font.Bold
                elide: Text.ElideRight
            }
            Row {
                spacing: 16
                width: parent.width
                // Without guide data it still shows what is kept.
                visible: root.nowEvent.start !== undefined || root.player.held
                Text {
                    id: rangeText
                    anchors.verticalCenter: parent.verticalCenter
                    text: root.nowEvent.start !== undefined ? Theme.formatRange(root.nowEvent.start, root.nowEvent.stop) : ""
                    color: "#d0d6df"
                    font.pixelSize: Theme.fontSmall
                }
                ProgrammeBar {
                    anchors.verticalCenter: parent.verticalCenter
                    width: parent.width - rangeText.width - leftText.width - 32
                    startMs: root.nowEvent.start ?? 0
                    stopMs: root.nowEvent.stop ?? 0
                    // While zapping, the other channel's programme as it is now.
                    playingMs: root.pendingUuid.length > 0 ? root.clockMs : root.playingMs
                    liveMs: root.clockMs
                    oldestMs: root.pendingUuid.length > 0 ? root.clockMs : root.oldestMs
                }
                Text {
                    id: leftText
                    anchors.verticalCenter: parent.verticalCenter
                    text: root.nowEvent.stop !== undefined ? qsTr("%1 min left").arg(Theme.minutesLeft(root.nowEvent.stop, root.pendingUuid.length > 0 ? root.clockMs : root.playingMs)) : ""
                    color: "#d0d6df"
                    font.pixelSize: Theme.fontSmall
                }
            }
            Text {
                width: parent.width
                visible: text.length > 0
                text: root.pendingUuid.length > 0 ? "" : root.keptText(root.clockMs - root.playingMs)
                color: "#d0d6df"
                font.pixelSize: Theme.fontSmall
            }
            Text {
                width: parent.width
                visible: root.nextEvent.title !== undefined
                text: qsTr("Next  %1  %2").arg(Theme.formatTime(root.nextEvent.start ?? 0)).arg(root.nextEvent.title ?? "")
                color: "#aab3c0"
                font.pixelSize: Theme.fontSmall
                elide: Text.ElideRight
            }

            // --- While paused: play, back to live, record ---
            Row {
                spacing: 11
                topPadding: 11
                visible: root.player.paused

                TvButton {
                    id: playHereButton
                    text: qsTr("Play from here")
                    icon.name: "media-playback-start"
                    KeyNavigation.right: toLiveButton.visible ? toLiveButton : null
                    Keys.onUpPressed: root.focusBar()
                    Keys.onPressed: event => {
                        if (Theme.isBack(event.key)) {
                            root.focusBar();
                            event.accepted = true;
                        }
                    }
                    onActivated: root.togglePause()
                }
                TvButton {
                    id: toLiveButton
                    visible: root.player.held
                    text: qsTr("Return to live")
                    icon.name: "media-skip-forward"
                    KeyNavigation.left: playHereButton
                    KeyNavigation.right: recordButton
                    Keys.onUpPressed: root.focusBar()
                    Keys.onPressed: event => {
                        if (Theme.isBack(event.key)) {
                            root.focusBar();
                            event.accepted = true;
                        }
                    }
                    onActivated: {
                        root.player.goLive();
                        root.flash(qsTr("Live"));
                    }
                }
                // Recording the rewind window comes with the recording feature.
                TvButton {
                    id: recordButton
                    visible: root.player.held
                    text: qsTr("Record")
                    icon.name: "media-record"
                    available: false
                    KeyNavigation.left: toLiveButton
                    Keys.onUpPressed: root.focusBar()
                    Keys.onPressed: event => {
                        if (Theme.isBack(event.key)) {
                            root.focusBar();
                            event.accepted = true;
                        }
                    }
                }
            }
            Text {
                visible: root.player.paused
                text: {
                    if (root.inPauseButtons)
                        return qsTr("↑ back to the bar");
                    const parts = [];
                    if (root.player.held)
                        parts.push(qsTr("←/→ move"));
                    parts.push(qsTr("↓ buttons"), qsTr("OK play"));
                    if (root.player.scrubbed)
                        parts.push(qsTr("BACK reset"));
                    return parts.join("   ");
                }
                color: "#aab3c0"
                font.pixelSize: Theme.fontSmall
            }
        }

        Text {
            id: clock
            anchors.right: parent.right
            anchors.rightMargin: Theme.margin
            anchors.bottom: parent.bottom
            anchors.bottomMargin: 48
            text: Theme.formatTime(Tvh.now)
            color: "white"
            font.pixelSize: Theme.fontHuge
            font.weight: Font.Light
        }
    }

    // --- Channel list overlay ----------------------------------------------
    Rectangle {
        id: zapPanel
        property bool shown: false
        width: 507
        height: parent.height
        x: shown ? 0 : -width
        // Visible as soon as it opens (before the slide-in has moved it), so
        // the list can take the keyboard focus right away.
        visible: shown || x > -width
        color: Kirigami.Theme.backgroundColor
        Behavior on x {
            NumberAnimation {
                duration: Theme.animNormal
                easing.type: Easing.OutCubic
            }
        }

        Text {
            id: zapTitle
            x: 32
            y: 32
            text: qsTr("Channels")
            color: Kirigami.Theme.textColor
            font.pixelSize: Theme.fontLarge
        }

        ListView {
            id: zapList
            anchors.top: zapTitle.bottom
            anchors.topMargin: 16
            anchors.bottom: parent.bottom
            anchors.bottomMargin: 21
            x: 16
            width: parent.width - 32
            clip: true
            model: Tvh.channels
            spacing: 5
            keyNavigationEnabled: true
            highlightMoveDuration: 120
            highlightRangeMode: ListView.ApplyRange
            preferredHighlightBegin: height * 0.3
            preferredHighlightEnd: height * 0.6

            Keys.onPressed: event => {
                zapTimer.restart();
                if (Theme.isAccept(event.key) && !event.isAutoRepeat) {
                    root.play(Tvh.channels.uuidAt(currentIndex));
                    root.closeZapList();
                } else if (Theme.isBack(event.key) || event.key === Qt.Key_Left) {
                    root.closeZapList();
                } else if (event.key === Qt.Key_PageUp || event.key === Qt.Key_ChannelUp) {
                    currentIndex = Math.max(0, currentIndex - 8);
                } else if (event.key === Qt.Key_PageDown || event.key === Qt.Key_ChannelDown) {
                    currentIndex = Math.min(count - 1, currentIndex + 8);
                } else if (event.key === Qt.Key_Right) {
                    // swallow
                } else {
                    return;
                }
                event.accepted = true;
            }

            delegate: Rectangle {
                id: zapRow
                required property int index
                required property string uuid
                required property int number
                required property string name
                required property url iconUrl
                required property string nowTitle
                required property double nowProgress
                readonly property bool current: ListView.isCurrentItem
                readonly property bool playing: uuid === root.player.channelUuid

                width: ListView.view.width
                height: 67
                radius: Theme.radiusSmall
                color: current ? Kirigami.Theme.highlightColor : "transparent"

                Text {
                    id: zapNumber
                    anchors.left: parent.left
                    anchors.leftMargin: 11
                    anchors.verticalCenter: parent.verticalCenter
                    width: 40
                    horizontalAlignment: Text.AlignRight
                    text: zapRow.number > 0 ? zapRow.number : ""
                    color: zapRow.current ? Kirigami.Theme.highlightedTextColor : Kirigami.Theme.disabledTextColor
                    font.pixelSize: Theme.fontSmall
                }
                ChannelLogo {
                    id: zapLogo
                    anchors.left: zapNumber.right
                    anchors.leftMargin: 13
                    anchors.verticalCenter: parent.verticalCenter
                    width: 73
                    height: 44
                    source: zapRow.iconUrl
                    name: zapRow.name
                }
                Column {
                    anchors.left: zapLogo.right
                    anchors.leftMargin: 13
                    anchors.right: parent.right
                    anchors.rightMargin: 16
                    anchors.verticalCenter: parent.verticalCenter
                    spacing: 4
                    Row {
                        spacing: 8
                        width: parent.width
                        Kirigami.Icon {
                            visible: zapRow.playing
                            width: 17
                            height: 17
                            anchors.verticalCenter: parent.verticalCenter
                            source: "media-playback-start"
                            isMask: true
                            color: zapRow.current ? Kirigami.Theme.highlightedTextColor : Kirigami.Theme.highlightColor
                        }
                        Text {
                            width: parent.width - (zapRow.playing ? 25 : 0)
                            text: zapRow.name
                            color: zapRow.current ? Kirigami.Theme.highlightedTextColor : Kirigami.Theme.textColor
                            font.pixelSize: Theme.fontNormal
                            font.weight: Font.DemiBold
                            elide: Text.ElideRight
                        }
                    }
                    Text {
                        width: parent.width
                        text: zapRow.nowTitle
                        color: zapRow.current ? Kirigami.Theme.highlightedTextColor : Kirigami.Theme.disabledTextColor
                        font.pixelSize: Theme.fontSmall
                        elide: Text.ElideRight
                    }
                }

                MouseArea {
                    anchors.fill: parent
                    onClicked: {
                        root.play(zapRow.uuid);
                        root.closeZapList();
                    }
                }
            }
        }
    }
}
