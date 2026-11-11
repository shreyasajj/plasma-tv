// SPDX-FileCopyrightText: 2026 shreyasajj
//
// SPDX-License-Identifier: GPL-2.0-only OR GPL-3.0-only OR LicenseRef-KDE-Accepted-GPL

import QtQuick
import org.kde.kirigami as Kirigami
import org.kde.bigscreen.ui as BigscreenUI
import Flimmer

// Edit page of one source; a new one when sourceId is empty. The rows come
// from fieldsFor(type), so each kind of source only lists its fields there.
FocusScope {
    id: root
    property string sourceId
    // Stored values (Sources.source()) and the edits made here, keyed like the fields.
    property var stored: ({})
    property var draft: ({})
    readonly property string type: value("type") || "tvheadend"
    readonly property bool isNew: sourceId.length === 0
    readonly property var fields: fieldsFor(type)
    property string testMessage
    property bool testOk: false
    property bool testing: false
    property bool confirmRemove: false

    signal done
    // Main provides the on-screen keyboard: editText(title, value, mode, callback)
    signal editText(string title, string value, string mode, var callback)

    function open(id) {
        sourceId = id;
        stored = id.length > 0 ? Sources.source(id) : ({
                type: "tvheadend",
                enabled: true
            });
        draft = {};
        testMessage = "";
        testing = false;
        confirmRemove = false;
        rows.positionViewAtBeginning();
        Qt.callLater(focusFirstRow);
    }

    function focusFirstRow() {
        const first = rowRepeater.itemAt(0);
        if (first)
            first.forceActiveFocus();
    }

    // Field kinds: text, number, switch, secret (never shown once stored), choice.
    function fieldsFor(type) {
        let list = [];

        if (isNew)
            list.push({
                key: "type",
                kind: "choice",
                label: qsTr("Kind of source"),
                options: Sources.types,
                names: Sources.types.map(t => Sources.typeName(t))
            });

        list.push({
            key: "name",
            kind: "text",
            label: qsTr("Name"),
            placeholder: qsTr("e.g. Living room server")
        }, {
            key: "enabled",
            kind: "switch",
            label: qsTr("Enabled"),
            fallback: true
        });

        if (type === "tvheadend") {
            list.push({
                key: "host",
                kind: "text",
                label: qsTr("Address"),
                placeholder: qsTr("e.g. 192.168.1.10")
            }, {
                key: "port",
                kind: "number",
                label: qsTr("Port"),
                fallback: 9981
            }, {
                key: "https",
                kind: "switch",
                label: qsTr("Use HTTPS"),
                fallback: false
            });
        } else if (type === "m3u") {
            list.push({
                key: "playlistUrl",
                kind: "text",
                label: qsTr("Playlist (M3U)"),
                placeholder: qsTr("e.g. http://192.168.1.10:9191/output/m3u")
            }, {
                key: "xmltvUrl",
                kind: "text",
                label: qsTr("Guide (XMLTV)"),
                placeholder: qsTr("from the playlist")
            }, {
                key: "userAgent",
                kind: "text",
                label: qsTr("User-Agent"),
                placeholder: qsTr("Flimmer's own")
            }, {
                key: "headers",
                kind: "text",
                label: qsTr("Extra headers"),
                placeholder: qsTr("Name: value, separated by |")
            });
        }

        list.push({
            key: "username",
            kind: "text",
            label: qsTr("Username"),
            placeholder: qsTr("none (anonymous)")
        }, {
            key: "password",
            kind: "secret",
            label: qsTr("Password")
        });

        return list;
    }

    function value(key, fallback) {
        if (draft[key] !== undefined)
            return draft[key];
        if (stored[key] !== undefined)
            return stored[key];
        return fallback;
    }

    function setValue(key, v) {
        const copy = Object.assign({}, draft);
        copy[key] = v;
        draft = copy;
        testMessage = "";
    }

    function describe(field) {
        const v = value(field.key, field.fallback);

        switch (field.kind) {
        case "secret":
            if (draft[field.key] !== undefined)
                return draft[field.key].length > 0 ? "•".repeat(Math.min(draft[field.key].length, 16)) : qsTr("not set");
            return stored[field.key + "Set"] ? "••••••••" : qsTr("not set");
        case "switch":
            return v ? qsTr("On") : qsTr("Off");
        case "choice":
            return field.names[Math.max(0, field.options.indexOf(v))];
        default:
            return v !== undefined && String(v).length > 0 ? String(v) : (field.placeholder ?? "");
        }
    }

    function activate(field, step) {
        switch (field.kind) {
        case "switch":
            setValue(field.key, !value(field.key, field.fallback));
            break;
        case "choice":
            {
                const i = Math.max(0, field.options.indexOf(value(field.key, field.options[0])));
                const n = field.options.length;
                setValue(field.key, field.options[(i + (step ?? 1) + n) % n]);
                break;
            }
        case "secret":
            editText(field.label, "", "password", v => setValue(field.key, v));
            break;
        case "number":
            editText(field.label, String(value(field.key, field.fallback)), "number", v => {
                const n = parseInt(v);
                if (n > 0 && n < 65536)
                    setValue(field.key, n);
            });
            break;
        default:
            editText(field.label, String(value(field.key, "")), "text", v => setValue(field.key, v.trim()));
        }
    }

    function values() {
        const v = Object.assign({}, draft);
        v.id = sourceId;
        v.type = type;
        return v;
    }

    Connections {
        target: Tvh
        enabled: root.visible
        function onConnectionTestFinished(ok, message) {
            root.testing = false;
            root.testOk = ok;
            root.testMessage = message;
        }
    }

    Keys.onPressed: event => {
        if (Theme.isBack(event.key)) {
            root.done();
            event.accepted = true;
        }
    }

    // --- Fields ---------------------------------------------------------
    Rectangle {
        id: card
        x: Theme.margin
        y: 11
        width: 720
        height: parent.height - y - 32
        radius: Theme.radius + 5
        color: Theme.card
        border.color: Theme.outline

        Text {
            id: heading
            x: 32
            y: 21
            text: root.isNew ? qsTr("New source") : (root.value("name", "") || Sources.typeName(root.type))
            color: Theme.accent
            font.pixelSize: Theme.fontSmall
            font.weight: Font.DemiBold
            font.capitalization: Font.AllUppercase
            font.letterSpacing: 1
        }

        ListView {
            id: rows
            anchors.fill: parent
            anchors.margins: 16
            anchors.topMargin: heading.y + heading.height + 11
            clip: true
            spacing: 3
            model: root.fields.length
            boundsBehavior: Flickable.StopAtBounds
            interactive: false
            currentIndex: -1

            delegate: BigscreenUI.ButtonDelegate {
                id: row
                required property int index
                readonly property var field: root.fields[index]
                width: rows.width
                text: field.label
                description: root.describe(field)
                trailing: Kirigami.Icon {
                    source: row.field.kind === "switch" ? (root.value(row.field.key, row.field.fallback) ? "checkbox-checked" : "checkbox-unchecked") : row.field.kind === "choice" ? "go-next" : "document-edit"
                    implicitWidth: Kirigami.Units.iconSizes.smallMedium
                    implicitHeight: Kirigami.Units.iconSizes.smallMedium
                }

                onClicked: root.activate(field)
                onActiveFocusChanged: {
                    if (activeFocus)
                        rows.positionViewAtIndex(index, ListView.Contain);
                }

                Keys.onLeftPressed: event => {
                    if (field.kind === "choice")
                        root.activate(field, -1);
                    else
                        event.accepted = false;
                }
                Keys.onRightPressed: {
                    if (field.kind === "choice")
                        root.activate(field, 1);
                    else
                        testButton.forceActiveFocus();
                }
                Keys.onUpPressed: {
                    if (index > 0)
                        rowRepeater.itemAt(index - 1).forceActiveFocus();
                }
                Keys.onDownPressed: {
                    if (index < root.fields.length - 1)
                        rowRepeater.itemAt(index + 1).forceActiveFocus();
                }
            }

            // ListView creates every row here (few), so they can be focused by index.
            cacheBuffer: 10000
        }
    }

    // ListView has no itemAt(index); this gives the same lookup.
    QtObject {
        id: rowRepeater
        function itemAt(index) {
            return rows.itemAtIndex(index);
        }
    }

    // --- Actions --------------------------------------------------------
    Column {
        id: side
        anchors.left: card.right
        anchors.leftMargin: 32
        anchors.right: parent.right
        anchors.rightMargin: Theme.margin
        y: 11
        spacing: 15

        Text {
            width: parent.width
            visible: root.testMessage.length > 0 || root.testing
            text: root.testing ? qsTr("Testing connection…") : (root.testOk ? "✓ " : "✗ ") + root.testMessage
            color: root.testing ? Theme.textDim : (root.testOk ? Theme.success : Theme.danger)
            font.pixelSize: Theme.fontSmall
            wrapMode: Text.Wrap
        }

        TvButton {
            id: testButton
            width: parent.width
            text: qsTr("Test connection")
            icon.name: "network-connect"
            available: !root.testing
            onActivated: {
                root.testing = true;
                root.testMessage = "";
                Tvh.testConnection(root.values());
            }
            Keys.onLeftPressed: root.focusFirstRow()
            KeyNavigation.down: saveButton
        }
        TvButton {
            id: saveButton
            width: parent.width
            text: root.isNew ? qsTr("Add source") : qsTr("Save & connect")
            icon.name: "document-save"
            // Nothing to save yet, or the address (or another required field) is still missing.
            available: (root.isNew || Object.keys(root.draft).length > 0) && Sources.valid(root.values())
            onActivated: {
                Sources.save(root.values());
                root.done();
            }
            Keys.onLeftPressed: root.focusFirstRow()
            KeyNavigation.up: testButton
            KeyNavigation.down: root.isNew ? backButton : upButton
        }
        TvButton {
            id: upButton
            width: parent.width
            visible: !root.isNew
            text: qsTr("Move up")
            icon.name: "go-up"
            onActivated: Sources.move(root.sourceId, -1)
            Keys.onLeftPressed: root.focusFirstRow()
            KeyNavigation.up: saveButton
            KeyNavigation.down: downButton
        }
        TvButton {
            id: downButton
            width: parent.width
            visible: !root.isNew
            text: qsTr("Move down")
            icon.name: "go-down"
            onActivated: Sources.move(root.sourceId, 1)
            Keys.onLeftPressed: root.focusFirstRow()
            KeyNavigation.up: upButton
            KeyNavigation.down: removeButton
        }
        TvButton {
            id: removeButton
            width: parent.width
            visible: !root.isNew
            text: root.confirmRemove ? qsTr("Press again to remove") : qsTr("Remove source")
            icon.name: "edit-delete"
            onActivated: {
                if (!root.confirmRemove) {
                    root.confirmRemove = true;
                    return;
                }

                Sources.remove(root.sourceId);
                root.done();
            }
            onActiveFocusChanged: {
                if (!activeFocus)
                    root.confirmRemove = false;
            }
            Keys.onLeftPressed: root.focusFirstRow()
            KeyNavigation.up: downButton
            KeyNavigation.down: backButton
        }
        TvButton {
            id: backButton
            width: parent.width
            text: qsTr("Back")
            icon.name: "go-previous"
            onActivated: root.done()
            Keys.onLeftPressed: root.focusFirstRow()
            KeyNavigation.up: root.isNew ? saveButton : removeButton
        }

        Text {
            width: parent.width
            topPadding: 5
            text: qsTr("Changes are only kept with \"Save\". BACK returns without saving.")
            color: Theme.textFaint
            font.pixelSize: Theme.fontTiny
            wrapMode: Text.Wrap
        }
    }
}
