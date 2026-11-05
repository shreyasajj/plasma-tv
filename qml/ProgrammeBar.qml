// SPDX-FileCopyrightText: 2026 shreyasajj
//
// SPDX-License-Identifier: GPL-2.0-only OR GPL-3.0-only OR LicenseRef-KDE-Accepted-GPL

import QtQuick
import org.kde.kirigami as Kirigami

// How far into a programme the picture is, and what is kept to rewind into.
// Times are milliseconds since the epoch. Without a programme (startMs 0) the bar
// runs from the oldest kept moment to live.
Item {
    id: root
    property double startMs: 0
    property double stopMs: 0
    property double playingMs: 0
    property double liveMs: 0
    // Equal to liveMs when nothing is kept.
    property double oldestMs: liveMs

    readonly property bool hasProgramme: startMs > 0 && stopMs > startMs
    readonly property double rangeStart: hasProgramme ? startMs : oldestMs
    readonly property double rangeEnd: hasProgramme ? stopMs : liveMs

    // 0..1 along the bar.
    function at(ms) {
        if (rangeEnd <= rangeStart)
            return 1;
        return Math.max(0, Math.min(1, (ms - rangeStart) / (rangeEnd - rangeStart)));
    }

    implicitHeight: 10

    Rectangle {
        id: track
        anchors.fill: parent
        radius: height / 2
        color: Qt.rgba(1, 1, 1, 0.15)
        clip: true

        // Kept on disk, from the oldest kept moment up to live.
        Rectangle {
            x: root.at(root.oldestMs) * track.width
            width: Math.max(0, root.at(root.liveMs) * track.width - x)
            height: parent.height
            color: Qt.rgba(1, 1, 1, 0.4)
            visible: root.oldestMs < root.liveMs
        }

        // Watched so far, up to the moment playing now.
        Rectangle {
            width: root.at(root.playingMs) * track.width
            height: parent.height
            radius: height / 2
            color: Kirigami.Theme.highlightColor
        }
    }

    // Live.
    Rectangle {
        x: root.at(root.liveMs) * root.width - width / 2
        anchors.verticalCenter: parent.verticalCenter
        width: 3
        height: parent.height + 8
        radius: 1
        color: "white"
        visible: root.oldestMs < root.liveMs || root.playingMs < root.liveMs
    }
}
