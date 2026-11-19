// SPDX-FileCopyrightText: 2026 Jan Claußen <jan.claussen10@web.de>
//
// SPDX-License-Identifier: GPL-2.0-only OR GPL-3.0-only OR LicenseRef-KDE-Accepted-GPL

import QtQuick
import QtQuick.Layouts
import QtQuick.Templates as T
import org.kde.bigscreen.ui as BigscreenUI

// Text input. Typing is done with a physical keyboard or Plasma's virtual
// keyboard (plasma-keyboard), which KWin shows for the focused field.
// Once a letter is typed on a physical keyboard, the virtual one stays hidden
// until Flimmer closes; a button in the dialog brings it back.
BigscreenUI.Dialog {
    id: root
    property string mode: "text"   // "text" | "password" | "number"
    property var callback: null
    property Item returnFocus: null
    // Lives as long as the app: the dialog is created once, in Main.
    // Not "is a keyboard connected": remote dongles and CEC bridges register as keyboards too.
    property bool hardwareKeyboard: false

    function showVirtualKeyboard() {
        if (!hardwareKeyboard)
            Qt.inputMethod.show();
    }

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
        root.showVirtualKeyboard();
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
            inputMethodHints: root.mode === "number" ? Qt.ImhDigitsOnly : root.mode === "password" ? Qt.ImhHiddenText | Qt.ImhSensitiveData | Qt.ImhNoAutoUppercase | Qt.ImhNoPredictiveText : Qt.ImhNoAutoUppercase | Qt.ImhNoPredictiveText | Qt.ImhUrlCharactersOnly
            validator: root.mode === "number" ? portValidator : null
            onAccepted: root.accept()
            Keys.onDownPressed: (virtualKeyboardButton.visible ? virtualKeyboardButton : root.footer).forceActiveFocus()
            // The virtual keyboard commits text through the input method, and a remote sends
            // no letters, so a letter key press comes from a physical keyboard.
            Keys.onPressed: event => {
                event.accepted = false;

                if (!root.hardwareKeyboard && event.text.length > 0
                        && event.text.toLowerCase() !== event.text.toUpperCase()) {
                    root.hardwareKeyboard = true;
                    Qt.inputMethod.hide();
                }
            }
            onActiveFocusChanged: {
                if (activeFocus)
                    root.showVirtualKeyboard();
            }
        }

        TvButton {
            id: virtualKeyboardButton
            visible: root.hardwareKeyboard
            text: qsTr("Show on-screen keyboard")
            icon.name: "input-keyboard-virtual"
            Keys.onUpPressed: field.forceActiveFocus()
            Keys.onDownPressed: root.footer.forceActiveFocus()
            onActivated: {
                root.hardwareKeyboard = false;
                field.forceActiveFocus();
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
        id: portValidator
        bottom: 1
        top: 65535
    }
}
