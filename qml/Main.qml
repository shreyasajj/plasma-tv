// SPDX-FileCopyrightText: 2026 Jan Claußen <jan.claussen10@web.de>
//
// SPDX-License-Identifier: GPL-2.0-only OR GPL-3.0-only OR LicenseRef-KDE-Accepted-GPL

import QtQuick
import QtMultimedia
import QtQuick.Controls as QQC2
import org.kde.kirigami as Kirigami
import org.kde.bigscreen.ui as BigscreenUI
import Flimmer

QQC2.ApplicationWindow {
    id: window
    property var bridgeControl
    property bool startWindowed: false
    // "home": navigation + pages, "watch": fullscreen video with OSD
    property string mode: "home"
    property string page: Settings.configured ? "channels" : "settings"
    // The guide and settings pages are only built once they are first shown, then kept.
    readonly property Item guidePage: guideLoader.item
    readonly property Item settingsPage: settingsLoader.item
    property string numberEntry: ""
    property bool cursorVisible: true
    // Channel stopped because the application lost focus; resumed when it returns.
    property string backgroundResumeUuid

    width: 1067
    height: 600
    visible: true
    visibility: startWindowed ? Window.Windowed : Window.FullScreen
    title: qsTr("Flimmer")
    color: "black"

    // A remote control can't click anything, so never leave keyboard focus on
    // nothing or on a hidden item: put it back where the user currently is.
    function focusUsable(item) {
        return item && item.visible && item !== contentItem && item !== keyRoot && item !== home;
    }

    function ensureFocus() {
        if (!active || focusUsable(activeFocusItem))
            return;
        if (textEntry.visible)
            textEntry.forceActiveFocus();
        else if (eventDialog.visible)
            eventDialog.forceActiveFocus();
        else if (mode === "watch")
            overlay.forceActiveFocus();
        else
            pageItem(page).forceActiveFocus();
        // A focus scope may still route focus to a hidden child; fall back to
        // something that always works.
        if (!focusUsable(activeFocusItem)) {
            if (mode === "watch")
                overlay.closeZapList();
            else
                nav.forceActiveFocus();
        }
    }

    onActiveFocusItemChanged: Qt.callLater(ensureFocus)

    // Only the application as a whole counts: switching between pages,
    // dialogs or the guide inside Flimmer never stops playback.
    readonly property bool appActive: Qt.application.state === Qt.ApplicationActive

    onAppActiveChanged: {
        if (!appActive) {
            if (Settings.stopInBackground && player.active)
                backgroundTimer.restart();
            return;
        }
        backgroundTimer.stop();
        if (backgroundResumeUuid.length > 0) {
            player.play(backgroundResumeUuid);
            backgroundResumeUuid = "";
        }
    }

    function pageItem(name) {
        return name === "guide" ? guidePage : name === "settings" ? settingsPage : channelsPage;
    }

    function goPage(name, focusContent) {
        mode = "home";
        page = name;
        if (focusContent)
            pageItem(name).forceActiveFocus();
        else
            nav.forceActiveFocus();
    }

    function watch(uuid) {
        if (!uuid)
            return;
        if (uuid !== player.channelUuid || player.error.length > 0)
            player.play(uuid);
        Settings.lastChannel = uuid;
        mode = "watch";
        overlay.forceActiveFocus();
        overlay.showInfo();
    }

    function leaveWatch(target) {
        const index = Tvh.channels.indexOfUuid(player.channelUuid);
        if (target === "guide") {
            goPage("guide", true);
            guidePage.showChannel(player.channelUuid);
        } else {
            goPage("channels", true);
            if (index >= 0)
                channelsPage.selectIndex(index);
        }
    }

    function commitNumber() {
        const number = parseInt(numberEntry);
        numberEntry = "";
        const index = Tvh.channels.indexOfNumber(number);
        if (index < 0) {
            toast.show(qsTr("Channel %1 not found").arg(number), "dialog-warning");
            return;
        }
        const uuid = Tvh.channels.uuidAt(index);
        if (mode === "watch")
            overlay.play(uuid);
        else if (page === "guide") {
            guidePage.selectIndex(index);
            guidePage.forceActiveFocus();
        } else {
            goPage("channels", true);
            channelsPage.selectIndex(index);
        }
    }

    Binding {
        target: Theme
        property: "source"
        value: stage
    }

    Timer {
        id: backgroundTimer
        // Short focus changes (notifications, dialogs) shouldn't interrupt the stream.
        interval: 250
        onTriggered: {
            if (window.appActive || !player.active)
                return;
            window.backgroundResumeUuid = player.channelUuid;
            player.stop();
        }
    }

    ScreenSaverInhibitor {
        active: player.active && window.mode === "watch"
    }

    PlayerView {
        id: player
        anchors.fill: parent
    }

    function publishBridgeState() {
        const state = player.error.length > 0 ? "error"
            : !player.active ? "idle"
            : player.paused ? "paused"
            : player.mediaPlayer.playbackState === MediaPlayer.PlayingState ? "playing" : "buffering";
        bridgeControl.publish(player.channelUuid, state, Tvh.busy);
    }

    Connections {
        target: window.bridgeControl
        function onFocusRequested() {
            window.show();
            window.raise();
            window.requestActivate();
        }
        function onRequested(command, uuid) {
            if (command === "select_channel") {
                // A previous focus-loss resume must not replace the requested channel.
                window.backgroundResumeUuid = "";
                backgroundTimer.stop();
                window.watch(uuid);
            } else if (command === "stop") {
                window.backgroundResumeUuid = "";
                player.stop();
            } else if ((command === "pause" && !player.paused)
                       || (command === "play" && player.paused)) {
                if (player.awaitingUrl) {
                    window.bridgeControl.acknowledge(false);
                    return;
                }
                player.togglePause();
            } else {
                window.bridgeControl.acknowledge(false);
                return;
            }
            window.publishBridgeState();
            window.bridgeControl.acknowledge(true);
        }
    }

    Timer {
        interval: 500
        repeat: true
        running: true
        onTriggered: window.publishBridgeState()
    }

    Component.onDestruction: bridgeControl.setReady(false)

    // The whole UI is laid out on a 1280x720 canvas and scaled to the screen.
    Item {
        id: stage
        width: 1280
        height: 720
        anchors.centerIn: parent
        scale: Math.min(window.width / width, window.height / height)

        FocusScope {
            id: keyRoot
            anchors.fill: parent
            focus: true

            // Global remote keys; reached when the focused item doesn't handle a key.
            Keys.onPressed: event => {
                const k = event.key;
                if (k >= Qt.Key_0 && k <= Qt.Key_9 && Tvh.channels.count > 0) {
                    window.numberEntry = (window.numberEntry + event.text).slice(-4);
                    numberTimer.restart();
                } else if (k === Qt.Key_Menu || k === Qt.Key_Settings || k === Qt.Key_F2) {
                    window.goPage("settings", true);
                } else if (k === Qt.Key_Guide || k === Qt.Key_G || k === Qt.Key_F3) {
                    window.goPage("guide", true);
                    if (player.active)
                        guidePage.showChannel(player.channelUuid);
                } else if (k === Qt.Key_F11) {
                    window.visibility = window.visibility === Window.FullScreen ? Window.Windowed : Window.FullScreen;
                } else if (k === Qt.Key_MediaStop) {
                    player.stop();
                } else if (Theme.isBack(k) && window.mode === "home") {
                    if (!nav.activeFocus)
                        nav.forceActiveFocus();
                    else if (player.active)
                        window.watch(player.channelUuid);
                } else {
                    return;
                }
                event.accepted = true;
            }

            // --- Home -------------------------------------------------------
            FocusScope {
                id: home
                anchors.fill: parent
                visible: window.mode === "home"
                focus: true

                Rectangle {
                    anchors.fill: parent
                    // A playing channel shines through behind the menus.
                    opacity: player.active ? 0.9 : 1
                    gradient: Gradient {
                        GradientStop {
                            position: 0
                            color: Theme.backgroundTop
                        }
                        GradientStop {
                            position: 0.35
                            color: Theme.background
                        }
                        GradientStop {
                            position: 1
                            color: Theme.background
                        }
                    }
                }

                NavBar {
                    id: nav
                    width: parent.width
                    currentPage: window.page
                    onPageSelected: name => window.page = name
                    onEnterPage: window.pageItem(window.page).forceActiveFocus()
                }

                ChannelsPage {
                    id: channelsPage
                    y: nav.height
                    width: parent.width
                    height: parent.height - nav.height
                    visible: window.page === "channels"
                    onExitUp: nav.forceActiveFocus()
                    onWatch: uuid => window.watch(uuid)
                    onOpenSettings: window.goPage("settings", true)
                    onOpenGuide: uuid => {
                        window.goPage("guide", true);
                        guidePage.showChannel(uuid);
                    }
                }

                Loader {
                    id: guideLoader
                    y: nav.height
                    width: parent.width
                    height: parent.height - nav.height
                    visible: window.page === "guide"
                    active: window.page === "guide" || item !== null

                    sourceComponent: GuidePage {
                        onExitUp: nav.forceActiveFocus()
                        onWatch: uuid => window.watch(uuid)
                        onOpenSettings: window.goPage("settings", true)
                        onOpenEvent: ev => eventDialog.show(ev)
                    }
                }

                Loader {
                    id: settingsLoader
                    y: nav.height
                    width: parent.width
                    height: parent.height - nav.height
                    visible: window.page === "settings"
                    active: window.page === "settings" || item !== null

                    sourceComponent: SettingsPage {
                        onExitUp: nav.forceActiveFocus()
                        onEditText: (title, value, mode, callback) => textEntry.show(title, value, mode, callback)
                    }
                }
            }

            // --- Watching -----------------------------------------------------
            WatchOverlay {
                id: overlay
                anchors.fill: parent
                player: player
                visible: window.mode === "watch"
                onLeave: target => window.leaveWatch(target)
            }

            EventDialog {
                id: eventDialog
                onWatch: uuid => window.watch(uuid)
            }

            TextEntryDialog {
                id: textEntry
            }
        }

        // Channel number being typed on the remote
        Rectangle {
            anchors.right: parent.right
            anchors.top: parent.top
            anchors.margins: 43
            visible: window.numberEntry.length > 0
            width: Math.max(133, numberText.implicitWidth + 53)
            height: 100
            radius: Theme.radius
            color: Theme.background
            border.color: Theme.accent
            border.width: 2
            z: 200
            Text {
                id: numberText
                anchors.centerIn: parent
                text: window.numberEntry
                color: Theme.text
                font.pixelSize: Theme.fontHuge
                font.weight: Font.Light
            }
        }

        BigscreenUI.Toast {
            id: toast
            anchors.bottomMargin: 48
            timeout: 4000
            z: 300
        }
    }

    Timer {
        id: numberTimer
        interval: 1600
        onTriggered: window.commitNumber()
    }

    Connections {
        target: Tvh
        function onNotification(message) {
            toast.show(message, "dialog-information");
        }
    }

    Connections {
        target: Settings
        function onSaved(serverChanged) {
            toast.show(qsTr("Settings saved"), "dialog-ok");
        }
    }

    // Hide the mouse pointer when it isn't used – this is a TV.
    MouseArea {
        anchors.fill: parent
        z: 1000
        hoverEnabled: true
        acceptedButtons: Qt.NoButton
        cursorShape: window.cursorVisible ? Qt.ArrowCursor : Qt.BlankCursor
        onPositionChanged: {
            window.cursorVisible = true;
            cursorTimer.restart();
        }
        Timer {
            id: cursorTimer
            interval: 3000
            running: true
            onTriggered: window.cursorVisible = false
        }
    }

    // Starts the channel watched last once it is known and the password is unlocked.
    property bool startPending: Settings.configured

    function startLastChannel() {
        if (!startPending || !Settings.credentialsReady || Tvh.channels.indexOfUuid(Settings.lastChannel) < 0)
            return;

        startPending = false;
        window.watch(Settings.lastChannel);
    }

    Connections {
        target: Tvh.channels
        enabled: window.startPending
        function onCountChanged() {
            window.startLastChannel();
        }
    }

    Connections {
        target: Settings
        enabled: window.startPending
        function onCredentialsReadyChanged() {
            // After the backend has applied the unlocked credentials, or the stream request
            // would go out without them.
            Qt.callLater(window.startLastChannel);
        }
    }

    Component.onCompleted: {
        publishBridgeState();
        bridgeControl.setReady(true);
        if (Settings.configured) {
            window.goPage("channels", true);
            const index = Tvh.channels.indexOfUuid(Settings.lastChannel);

            if (index >= 0)
                channelsPage.selectIndex(index);

            startLastChannel();
        } else {
            window.goPage("settings", true);
        }
    }
}
