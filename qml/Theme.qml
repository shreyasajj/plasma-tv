// SPDX-FileCopyrightText: 2026 Jan Claußen <jan.claussen10@web.de>
//
// SPDX-License-Identifier: GPL-2.0-only OR GPL-3.0-only OR LicenseRef-KDE-Accepted-GPL

pragma Singleton
import QtQuick
import Flimmer
import org.kde.kirigami as Kirigami
import org.kde.bigscreen.ui as BigscreenUI

// Sizes for a 10-foot UI designed on a 1280x720 canvas, in the units of
// Bigscreen UI (the window is scaled by 1.5, see main.cpp). Colors come from the
// active Plasma color scheme via Kirigami.Theme (Breeze Dark by default).
QtObject {
    // An item inside the window that provides Kirigami's theme. Kirigami only
    // resolves colors for items that live in a window, so Main assigns it.
    property Item source: null
    readonly property var kt: source ? source.Kirigami.Theme : null

    readonly property color background: kt ? kt.backgroundColor : "#202326"
    readonly property color text: kt ? kt.textColor : "#fcfcfc"
    readonly property color accent: kt ? kt.highlightColor : "#3daee9"
    readonly property color onAccent: kt ? kt.highlightedTextColor : "#fcfcfc"
    readonly property color textFaint: kt ? kt.disabledTextColor : "#a1a9b1"
    readonly property color focusRing: kt ? kt.focusColor : "#3daee9"
    readonly property color success: kt ? kt.positiveTextColor : "#27ae60"
    readonly property color warning: kt ? kt.neutralTextColor : "#f67400"
    readonly property color danger: kt ? kt.negativeTextColor : "#da4453"
    readonly property color recording: danger

    readonly property color backgroundTop: Kirigami.ColorUtils.tintWithAlpha(background, accent, 0.10)
    readonly property color surface: Kirigami.ColorUtils.tintWithAlpha(background, text, 0.05)
    readonly property color surfaceRaised: Kirigami.ColorUtils.tintWithAlpha(background, text, 0.10)
    readonly property color surfaceHover: Kirigami.ColorUtils.tintWithAlpha(background, accent, 0.28)
    readonly property color card: Kirigami.ColorUtils.tintWithAlpha(background, text, 0.04)
    readonly property color outline: Kirigami.ColorUtils.tintWithAlpha(background, text, 0.16)
    readonly property color accentDeep: Kirigami.ColorUtils.tintWithAlpha(background, accent, 0.55)
    readonly property color textDim: Kirigami.ColorUtils.tintWithAlpha(text, background, 0.30)

    readonly property int radius: 11
    readonly property int radiusSmall: 7
    readonly property int margin: 48
    readonly property int spacing: 16

    readonly property int fontTiny: BigscreenUI.Units.smallFontPixelSize
    readonly property int fontSmall: 16
    readonly property int fontNormal: BigscreenUI.Units.defaultFontPixelSize
    readonly property int fontLarge: 24
    readonly property int fontTitle: 35
    readonly property int fontHuge: 56

    readonly property int animFast: 120
    readonly property int animNormal: 220

    function genreColor(genre) {
        switch (genre) {
        case 0x10:
            return "#8e5cd9";  // movie
        case 0x20:
            return "#3a86ff";  // news
        case 0x30:
            return "#2ec4b6";  // show
        case 0x40:
            return "#4caf50";  // sports
        case 0x50:
            return "#ffbe0b";  // children
        case 0x60:
            return "#ff5d8f";  // music
        case 0x70:
            return "#f77f00";  // arts
        case 0x80:
            return "#90a4ae";  // social
        case 0x90:
            return "#00b4d8";  // education
        case 0xA0:
            return "#c0ca33";  // leisure
        default:
            return outline;
        }
    }

    function formatTime(ms) {
        Settings.guideTimeZone;
        return Settings.formatGuideTime(ms);
    }

    function formatRange(startMs, stopMs) {
        return formatTime(startMs) + " – " + formatTime(stopMs);
    }

    function formatDuration(ms) {
        const minutes = Math.round(ms / 60000);
        if (minutes < 60)
            return minutes + " min";
        const h = Math.floor(minutes / 60);
        const m = minutes % 60;
        return m === 0 ? h + " h" : h + " h " + m + " min";
    }

    function formatDay(ms) {
        Settings.guideTimeZone;
        return Settings.formatGuideDay(ms);
    }

    function minutesLeft(stopMs, nowMs) {
        return Math.max(0, Math.round((stopMs - nowMs) / 60000));
    }

    // Keys that a TV remote, a keyboard or CEC may send for the same intent.
    function isAccept(key) {
        return key === Qt.Key_Return || key === Qt.Key_Enter || key === Qt.Key_Select || key === Qt.Key_Space;
    }
    function isBack(key) {
        return key === Qt.Key_Back || key === Qt.Key_Escape || key === Qt.Key_Backspace || key === Qt.Key_Exit;
    }
}
