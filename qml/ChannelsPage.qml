// SPDX-FileCopyrightText: 2026 Jan Claußen <jan.claussen10@web.de>
//
// SPDX-License-Identifier: GPL-2.0-only OR GPL-3.0-only OR LicenseRef-KDE-Accepted-GPL

import QtQuick
import org.kde.kirigami as Kirigami
import org.kde.bigscreen.ui as BigscreenUI
import Flimmer

// Live TV: channel list on the left, details of the highlighted channel on the right.
FocusScope {
    id: root
    signal exitUp
    signal watch(string uuid)
    signal openGuide(string uuid)
    signal openSettings

    property alias currentIndex: list.currentIndex
    readonly property bool empty: Tvh.channels.count === 0

    function selectIndex(index) {
        if (index >= 0 && index < list.count)
            list.positionViewAtIndex(index, ListView.Center);
        list.currentIndex = index;
    }

    readonly property var current: {
        Tvh.now;
        Tvh.epgRevision;  // re-evaluate as time passes or data changes
        return Tvh.channels.get(list.currentIndex);
    }
    readonly property var upcoming: {
        Tvh.now;
        Tvh.epgRevision;
        if (!current.uuid)
            return [];
        return Tvh.eventsForChannel(current.uuid, Date.now(), Date.now() + 12 * 3600 * 1000).slice(1, 4);
    }

    ListView {
        id: list
        x: Theme.margin - 11
        width: 640
        height: parent.height
        topMargin: 11
        bottomMargin: 32
        leftMargin: 11
        rightMargin: 11
        model: Tvh.channels
        focus: !root.empty
        clip: true
        spacing: 9
        visible: !root.empty
        keyNavigationEnabled: true
        highlightMoveDuration: 160
        highlightRangeMode: ListView.ApplyRange
        preferredHighlightBegin: height * 0.3
        preferredHighlightEnd: height * 0.62
        cacheBuffer: 800

        // Channels may arrive after the page is shown (first start, no cache yet).
        onCountChanged: {
            if (currentIndex < 0 && count > 0)
                root.selectIndex(Math.max(0, Tvh.channels.indexOfUuid(Settings.lastChannel)));
        }

        Keys.onPressed: event => {
            if (event.key === Qt.Key_Up && currentIndex <= 0) {
                root.exitUp();
            } else if (event.key === Qt.Key_PageUp || event.key === Qt.Key_ChannelUp) {
                currentIndex = Math.max(0, currentIndex - 6);
            } else if (event.key === Qt.Key_PageDown || event.key === Qt.Key_ChannelDown) {
                currentIndex = Math.min(count - 1, currentIndex + 6);
            } else if (event.key === Qt.Key_Right) {
                if (root.current.uuid)
                    root.openGuide(root.current.uuid);
            } else if (Theme.isAccept(event.key) && !event.isAutoRepeat) {
                if (root.current.uuid)
                    root.watch(root.current.uuid);
            } else {
                return;
            }
            event.accepted = true;
        }

        delegate: Item {
            id: row
            required property int index
            required property string uuid
            required property int number
            required property string name
            required property url iconUrl
            required property string nowTitle
            required property double nowStop
            required property double nowProgress
            required property int nowGenre
            required property bool nowRecording
            readonly property bool focused: ListView.isCurrentItem && list.activeFocus

            width: ListView.view.width - 21
            height: 77
            z: focused ? 2 : 1

            Rectangle {
                id: card
                anchors.fill: parent
                radius: Theme.radius
                color: row.focused ? Theme.surfaceHover : (row.ListView.isCurrentItem ? Theme.surfaceRaised : Theme.surface)
                border.width: row.focused ? 2 : 0
                border.color: Theme.focusRing
                scale: row.focused ? 1.03 : 1
                Behavior on scale {
                    NumberAnimation {
                        duration: Theme.animFast
                    }
                }
                Behavior on color {
                    ColorAnimation {
                        duration: Theme.animFast
                    }
                }

                Rectangle {
                    width: 4
                    radius: 2
                    anchors.left: parent.left
                    anchors.leftMargin: 7
                    anchors.verticalCenter: parent.verticalCenter
                    height: parent.height - 27
                    color: Theme.genreColor(row.nowGenre)
                    visible: row.nowTitle.length > 0
                }

                Text {
                    id: numberLabel
                    anchors.left: parent.left
                    anchors.leftMargin: 16
                    anchors.verticalCenter: parent.verticalCenter
                    width: 48
                    horizontalAlignment: Text.AlignRight
                    text: row.number > 0 ? row.number : ""
                    color: row.focused ? Theme.text : Theme.textFaint
                    font.pixelSize: Theme.fontLarge
                    font.weight: Font.Light
                }

                ChannelLogo {
                    id: logo
                    anchors.left: numberLabel.right
                    anchors.leftMargin: 16
                    anchors.verticalCenter: parent.verticalCenter
                    width: 88
                    height: 53
                    source: row.iconUrl
                    name: row.name
                }

                Column {
                    anchors.left: logo.right
                    anchors.leftMargin: 19
                    anchors.right: remaining.left
                    anchors.rightMargin: 13
                    anchors.verticalCenter: parent.verticalCenter
                    spacing: 4

                    Text {
                        width: parent.width
                        text: row.name
                        color: Theme.text
                        font.pixelSize: Theme.fontNormal
                        font.weight: Font.DemiBold
                        elide: Text.ElideRight
                    }
                    Row {
                        width: parent.width
                        spacing: 7
                        Rectangle {
                            visible: row.nowRecording
                            width: 9
                            height: 9
                            radius: 5
                            color: Theme.recording
                            anchors.verticalCenter: parent.verticalCenter
                        }
                        Text {
                            width: parent.width - (row.nowRecording ? 16 : 0)
                            text: row.nowTitle.length > 0 ? row.nowTitle : qsTr("No programme information")
                            color: row.nowTitle.length > 0 ? Theme.textDim : Theme.textFaint
                            font.pixelSize: Theme.fontSmall
                            elide: Text.ElideRight
                        }
                    }
                    ThinProgress {
                        width: parent.width
                        height: 3
                        value: row.nowProgress
                        visible: row.nowTitle.length > 0
                        fillColor: row.focused ? Theme.accent : Theme.textFaint
                    }
                }

                Text {
                    id: remaining
                    anchors.right: parent.right
                    anchors.rightMargin: 19
                    anchors.verticalCenter: parent.verticalCenter
                    visible: row.nowTitle.length > 0
                    text: qsTr("%1 min").arg(Theme.minutesLeft(row.nowStop, Tvh.now))
                    color: Theme.textFaint
                    font.pixelSize: Theme.fontSmall
                }
            }

            MouseArea {
                anchors.fill: parent
                onClicked: {
                    list.forceActiveFocus();
                    if (list.currentIndex === row.index)
                        root.watch(row.uuid);
                    list.currentIndex = row.index;
                }
            }
        }
    }

    // Details of the highlighted channel
    Item {
        id: details
        visible: !root.empty && root.current.uuid !== undefined
        anchors.left: list.right
        anchors.leftMargin: 37
        anchors.right: parent.right
        anchors.rightMargin: Theme.margin
        anchors.top: parent.top
        anchors.topMargin: 11
        anchors.bottom: parent.bottom
        anchors.bottomMargin: 32

        Rectangle {
            anchors.fill: parent
            radius: Theme.radius + 5
            gradient: Gradient {
                GradientStop {
                    position: 0
                    color: Theme.surface
                }
                GradientStop {
                    position: 1
                    color: Theme.card
                }
            }
            border.color: Theme.outline
        }

        Column {
            anchors.fill: parent
            anchors.margins: 32
            anchors.bottomMargin: 110   // room for the key hints
            spacing: 15
            clip: true

            Row {
                spacing: 21
                ChannelLogo {
                    width: 147
                    height: 88
                    source: root.current.iconUrl ?? ""
                    name: root.current.name ?? ""
                }
                Column {
                    anchors.verticalCenter: parent.verticalCenter
                    spacing: 3
                    Text {
                        text: root.current.number > 0 ? qsTr("Channel %1").arg(root.current.number) : ""
                        color: Theme.accent
                        font.pixelSize: Theme.fontSmall
                        font.weight: Font.DemiBold
                        font.capitalization: Font.AllUppercase
                        font.letterSpacing: 1
                    }
                    Text {
                        width: details.width - 64 - 168
                        text: root.current.name ?? ""
                        color: Theme.text
                        font.pixelSize: Theme.fontTitle
                        font.weight: Font.DemiBold
                        elide: Text.ElideRight
                    }
                }
            }

            Item {
                width: 1
                height: 5
            }

            Row {
                spacing: 11
                visible: (root.current.nowTitle ?? "").length > 0
                Rectangle {
                    width: nowLabel.implicitWidth + 19
                    height: 27
                    radius: 5
                    color: root.current.nowRecording ? Theme.recording : Theme.accent
                    Text {
                        id: nowLabel
                        anchors.centerIn: parent
                        text: root.current.nowRecording ? qsTr("● REC") : qsTr("NOW")
                        color: Theme.onAccent
                        font.pixelSize: Theme.fontTiny
                        font.weight: Font.Bold
                        font.letterSpacing: 1.5
                    }
                }
                Text {
                    anchors.verticalCenter: parent.verticalCenter
                    text: root.current.nowStart ? Theme.formatRange(root.current.nowStart, root.current.nowStop) + "  ·  " + qsTr("%1 min left").arg(Theme.minutesLeft(root.current.nowStop, Tvh.now)) : ""
                    color: Theme.textDim
                    font.pixelSize: Theme.fontSmall
                }
            }

            Text {
                width: parent.width
                text: (root.current.nowTitle ?? "").length > 0 ? root.current.nowTitle : qsTr("No programme information available")
                color: Theme.text
                font.pixelSize: (root.current.nowTitle ?? "").length > 0 ? Theme.fontTitle : Theme.fontLarge
                font.weight: Font.Bold
                wrapMode: Text.Wrap
                maximumLineCount: 2
                elide: Text.ElideRight
            }

            Text {
                width: parent.width
                visible: text.length > 0
                text: root.current.nowSubtitle ?? ""
                color: Theme.textDim
                font.pixelSize: Theme.fontNormal
                elide: Text.ElideRight
            }

            BigscreenUI.ProgressBar {
                width: parent.width
                value: root.current.nowProgress ?? 0
                visible: (root.current.nowTitle ?? "").length > 0
            }

            Text {
                width: parent.width
                visible: text.length > 0
                text: root.current.nowSummary ?? ""
                color: Theme.textDim
                font.pixelSize: Theme.fontSmall
                lineHeight: 1.2
                wrapMode: Text.Wrap
                maximumLineCount: 5
                elide: Text.ElideRight
            }

            Item {
                width: 1
                height: 5
            }

            Text {
                visible: root.upcoming.length > 0
                text: qsTr("Up next")
                color: Theme.accent
                font.pixelSize: Theme.fontSmall
                font.weight: Font.DemiBold
                font.capitalization: Font.AllUppercase
                font.letterSpacing: 1
            }

            Repeater {
                model: root.upcoming
                delegate: Row {
                    required property var modelData
                    spacing: 16
                    width: parent.width
                    Text {
                        width: 67
                        text: Theme.formatTime(modelData.start)
                        color: Theme.textDim
                        font.pixelSize: Theme.fontNormal
                        font.features: {
                            "tnum": 1
                        }
                    }
                    Rectangle {
                        width: 4
                        height: 19
                        radius: 2
                        anchors.verticalCenter: parent.verticalCenter
                        color: Theme.genreColor(modelData.genre)
                    }
                    Text {
                        width: parent.width - 107
                        text: modelData.title
                        color: Theme.text
                        font.pixelSize: Theme.fontNormal
                        elide: Text.ElideRight
                    }
                }
            }
        }

        Row {
            anchors.left: parent.left
            anchors.bottom: parent.bottom
            anchors.margins: 27
            spacing: 24
            Repeater {
                model: [
                    {
                        key: "OK",
                        label: qsTr("Watch")
                    },
                    {
                        key: "▶",
                        label: qsTr("Guide")
                    },
                    {
                        key: "0-9",
                        label: qsTr("Channel number")
                    }
                ]
                delegate: Row {
                    required property var modelData
                    spacing: 8
                    Rectangle {
                        width: Math.max(29, keyText.implicitWidth + 13)
                        height: 24
                        radius: 5
                        color: "transparent"
                        border.color: Theme.textFaint
                        border.width: 1
                        Text {
                            id: keyText
                            anchors.centerIn: parent
                            text: modelData.key
                            color: Theme.textDim
                            font.pixelSize: Theme.fontTiny
                            font.weight: Font.DemiBold
                        }
                    }
                    Text {
                        anchors.verticalCenter: parent.verticalCenter
                        text: modelData.label
                        color: Theme.textFaint
                        font.pixelSize: Theme.fontTiny
                    }
                }
            }
        }
    }

    // Empty / first-run states
    Column {
        anchors.centerIn: parent
        visible: root.empty
        spacing: 19
        width: 733

        BigscreenUI.BusyIndicator {
            anchors.horizontalCenter: parent.horizontalCenter
            visible: Tvh.status === Tvh.Connecting || Tvh.busy
            running: visible
        }
        BigscreenUI.PlaceholderMessage {
            width: parent.width
            focus: root.empty
            iconName: Tvh.status === Tvh.Connecting || Tvh.busy ? "" : Tvh.status === Tvh.Offline ? "network-disconnect" : "video-television"
            text: Tvh.status === Tvh.Unconfigured ? qsTr("Welcome!") : Tvh.status === Tvh.Offline ? qsTr("Can't reach the server") : Tvh.status === Tvh.Online && !Tvh.busy ? qsTr("No channels found") : qsTr("Loading channels…")
            explanation: Tvh.status === Tvh.Unconfigured ? qsTr("Add a source of channels in Settings.") : Tvh.status === Tvh.Offline ? Tvh.statusText : Tvh.status === Tvh.Online && !Tvh.busy ? qsTr("The server has no enabled channels for this user.") : ""
            helpfulAction: Kirigami.Action {
                text: qsTr("Open Settings")
                icon.name: "configure"
                visible: Tvh.status === Tvh.Unconfigured || Tvh.status === Tvh.Offline
                onTriggered: root.openSettings()
            }
            Keys.onUpPressed: root.exitUp()
        }
    }
}
