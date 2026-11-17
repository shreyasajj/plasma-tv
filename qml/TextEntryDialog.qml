// SPDX-FileCopyrightText: 2026 Jan Claußen <jan.claussen10@web.de>
//
// SPDX-License-Identifier: GPL-2.0-only OR GPL-3.0-only OR LicenseRef-KDE-Accepted-GPL

import QtQuick
import QtQuick.Layouts
import QtQuick.Templates as T
import org.kde.bigscreen.ui as BigscreenUI

// Text input. Typing is done with a physical keyboard or Plasma's virtual
// keyboard (plasma-keyboard), which KWin shows for the focused field.
BigscreenUI.Dialog {
    id: root
    property string mode: "text"   // "text" | "password" | "number" | "offset" (signed minutes)
    property var callback: null
    property Item returnFocus: null

    // Keep clear of the lower half, where the virtual keyboard slides in.
    anchors.centerIn: undefined
    x: Math.round((parent.width - width) / 2)
    y: Math.round(parent.height * 0.12)

    standardButtons: T.Dialog.Ok | T.Dialog.Cancel
    openFocusItem: field

    function show(title, value, mode, callback) {
        root.title = title;
        root.mode = mode;
        root.callback = callback;
        root.returnFocus = Window.activeFocusItem;
        field.text = value;

        open();
    }

    onOpened: {
        field.selectAll();
        Qt.inputMethod.show();
    }

    onAccepted: {
        if (callback)
            callback(field.text);
    }

    onAboutToHide: {
        Qt.inputMethod.hide();
        callback = null;

        if (returnFocus)
            returnFocus.forceActiveFocus();
    }

    contentItem: ColumnLayout {
        spacing: 16

        BigscreenUI.TextField {
            id: field
            Layout.fillWidth: true
            echoMode: root.mode === "password" ? TextInput.Password : TextInput.Normal
            inputMethodHints: root.mode === "offset" ? Qt.ImhFormattedNumbersOnly : root.mode === "number" ? Qt.ImhDigitsOnly : root.mode === "password" ? Qt.ImhHiddenText | Qt.ImhSensitiveData | Qt.ImhNoAutoUppercase | Qt.ImhNoPredictiveText : Qt.ImhNoAutoUppercase | Qt.ImhNoPredictiveText | Qt.ImhUrlCharactersOnly
            validator: root.mode === "offset" ? offsetValidator : root.mode === "number" ? portValidator : null
            onAccepted: root.accept()
            Keys.onDownPressed: root.footer.forceActiveFocus()
            onActiveFocusChanged: {
                if (activeFocus)
                    Qt.inputMethod.show();
            }
        }

        BigscreenUI.Label {
            Layout.fillWidth: true
            text: qsTr("Type with Plasma's on-screen keyboard or a keyboard · OK/Enter confirms · BACK cancels")
            type: BigscreenUI.Label.Small
            color: Theme.textFaint
            wrapMode: Text.Wrap
        }
    }

    IntValidator {
        id: offsetValidator
        bottom: -1440
        top: 1440
    }

    IntValidator {
        id: portValidator
        bottom: 1
        top: 65535
    }
}
