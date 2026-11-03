// SPDX-FileCopyrightText: 2026 shreyasajj
//
// SPDX-License-Identifier: GPL-2.0-only OR GPL-3.0-only OR LicenseRef-KDE-Accepted-GPL

import QtQuick
import QtQuick.Controls as Controls
import org.kde.kirigami as Kirigami
import Flimmer

// Covers the video while a channel starts or the stream refills, with the relay's
// buffer level while it holds data back.
Item {
    id: root
    required property Item player
    // From TvhBackend.bufferState(), read while shown.
    property var buffer: ({})
    readonly property bool filling: buffer.filling === true && buffer.targetMs > 0

    visible: player.loading

    Timer {
        interval: 500
        repeat: true
        triggeredOnStart: true
        running: root.visible
        onTriggered: root.buffer = Tvh.bufferState()
    }

    // The last picture stays, dimmed.
    Rectangle {
        anchors.fill: parent
        color: Qt.rgba(0, 0, 0, 0.5)
    }

    Rectangle {
        anchors.centerIn: parent
        width: 533
        height: column.implicitHeight + 53
        radius: Theme.radius + 5
        color: Kirigami.Theme.backgroundColor
        opacity: 0.9

        Column {
            id: column
            anchors.centerIn: parent
            width: parent.width - 53
            spacing: 16

            Controls.BusyIndicator {
                anchors.horizontalCenter: parent.horizontalCenter
                running: root.visible
            }
            Text {
                width: parent.width
                horizontalAlignment: Text.AlignHCenter
                text: {
                    const what = root.player.startedOnce ? qsTr("Buffering…") : qsTr("Loading channel…");
                    if (!root.filling)
                        return what;
                    return qsTr("%1  %2 of %3 s").arg(what).arg(Math.floor(root.buffer.bufferedMs / 1000)).arg(Math.round(root.buffer.targetMs / 1000));
                }
                color: Kirigami.Theme.textColor
                font.pixelSize: Theme.fontNormal
                font.weight: Font.DemiBold
            }
            Rectangle {
                width: parent.width
                height: 8
                radius: 4
                color: Qt.rgba(1, 1, 1, 0.15)
                visible: root.filling

                Rectangle {
                    width: parent.width * Math.min(1, root.buffer.bufferedMs / Math.max(1, root.buffer.targetMs))
                    height: parent.height
                    radius: parent.radius
                    color: Kirigami.Theme.highlightColor
                }
            }
        }
    }
}
