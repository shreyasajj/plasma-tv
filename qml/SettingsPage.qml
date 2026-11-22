// SPDX-FileCopyrightText: 2026 Jan Claußen <jan.claussen10@web.de>
//
// SPDX-License-Identifier: GPL-2.0-only OR GPL-3.0-only OR LicenseRef-KDE-Accepted-GPL

import QtQuick
import QtQuick.Templates as T
import org.kde.kirigami as Kirigami
import org.kde.bigscreen.ui as BigscreenUI
import Flimmer

FocusScope {
    id: root
    signal exitUp
    // Main provides the on-screen keyboard: editText(title, value, mode, callback)
    signal editText(string title, string value, string mode, var callback)

    // Draft values, committed with "Save"
    property string dProfile
    property int dDays: 3
    // -1 = Auto, 0 = Off, else seconds
    property int dBuffer: 2
    readonly property var bufferModes: [-1, 0, 2, 5, 10]
    property bool dStopInBackground: true
    property bool dConfirmQuit: true
    property string dScheme
    readonly property int bufferIndex: Math.max(0, bufferModes.indexOf(dBuffer))
    readonly property int schemeIndex: Math.max(0, Settings.colorSchemes.findIndex(s => s.id === dScheme))
    // The streaming profile only matters to TVHeadend.
    readonly property bool hasTvheadend: Sources.sources.some(s => s.type === "tvheadend")
    readonly property Item firstRow: sourceRepeater.count > 0 ? sourceRepeater.itemAt(0) : addSourceRow

    readonly property bool dirty: dProfile !== Settings.streamProfile || dDays !== Settings.guideDays || dBuffer !== Settings.playbackBuffer || dStopInBackground !== Settings.stopInBackground || dConfirmQuit !== Settings.confirmQuit || dScheme !== Settings.colorScheme

    function load() {
        dProfile = Settings.streamProfile;
        dDays = Settings.guideDays;
        dBuffer = Settings.playbackBuffer;
        dStopInBackground = Settings.stopInBackground;
        dConfirmQuit = Settings.confirmQuit;
        dScheme = Settings.colorScheme;
    }

    function setScheme(id) {
        dScheme = id;
        // Show the scheme right away; it is only stored by "Save".
        Settings.previewColorScheme(dScheme);
    }

    function values() {
        return {
            streamProfile: dProfile,
            guideDays: dDays,
            playbackBuffer: dBuffer,
            stopInBackground: dStopInBackground,
            confirmQuit: dConfirmQuit,
            colorScheme: dScheme
        };
    }

    function save() {
        Settings.save(values());
    }

    function editSource(id) {
        editor.open(id);
        editor.visible = true;
    }

    // The status line of a source on the Sources list.
    function sourceDescription(source) {
        if (!source.enabled)
            return qsTr("%1 · disabled").arg(Sources.typeName(source.type));
        const status = Tvh.sourceStatus.find(s => s.id === source.id);
        return status ? qsTr("%1 · %2").arg(Sources.typeName(source.type)).arg(status.text) : Sources.typeName(source.type);
    }

    Component.onCompleted: load()
    onVisibleChanged: {
        if (visible && !dirty) {
            load();
        } else if (!visible && dScheme !== Settings.colorScheme) {
            // Leaving without saving ends the preview.
            dScheme = Settings.colorScheme;
            Settings.previewColorScheme(dScheme);
        }
    }

    // Keeps the focused row in view; the form is taller than the page.
    function ensureVisible(item) {
        let parent = item;

        while (parent && parent !== form)
            parent = parent.parent;

        if (!parent)
            return;

        const top = item.mapToItem(formCard, 0, 0).y - 16;
        const bottom = top + item.height + 32;

        // Near the top, show the section header too.
        if (top < formView.contentY)
            formView.contentY = top < item.height ? 0 : top;
        else if (bottom > formView.contentY + formView.height)
            formView.contentY = Math.min(formView.contentHeight - formView.height, bottom - formView.height);
    }

    Connections {
        target: root.Window.window
        function onActiveFocusItemChanged() {
            root.ensureVisible(root.Window.activeFocusItem);
        }
    }

    // --- Form ---------------------------------------------------------
    Flickable {
        id: formView
        visible: !editor.visible
        x: Theme.margin
        y: 11
        width: 720
        height: parent.height - y - 32
        contentHeight: formCard.height
        boundsBehavior: Flickable.StopAtBounds
        clip: true
        Behavior on contentY {
            NumberAnimation {
                duration: Theme.animNormal
            }
        }

        Rectangle {
            id: formCard
            width: parent.width
            height: form.implicitHeight + 32
            radius: Theme.radius + 5
            color: Theme.card
            border.color: Theme.outline

            Column {
                id: form
                anchors.fill: parent
                anchors.margins: 16
                spacing: 3

                Text {
                    text: qsTr("Sources")
                    color: Theme.accent
                    font.pixelSize: Theme.fontSmall
                    font.weight: Font.DemiBold
                    font.capitalization: Font.AllUppercase
                    font.letterSpacing: 1
                    leftPadding: 16
                    topPadding: 5
                    bottomPadding: 5
                }

                Repeater {
                    id: sourceRepeater
                    model: Sources.sources

                    delegate: BigscreenUI.ButtonDelegate {
                        id: sourceRow
                        required property var modelData
                        required property int index
                        width: form.width
                        focus: sourceRow.index === 0
                        text: sourceRow.modelData.name
                        description: root.sourceDescription(sourceRow.modelData)
                        trailing: EditIcon {}
                        onClicked: root.editSource(sourceRow.modelData.id)
                        Keys.onUpPressed: {
                            if (sourceRow.index > 0)
                                sourceRepeater.itemAt(sourceRow.index - 1).forceActiveFocus();
                            else
                                root.exitUp();
                        }
                        Keys.onDownPressed: {
                            if (sourceRow.index < sourceRepeater.count - 1)
                                sourceRepeater.itemAt(sourceRow.index + 1).forceActiveFocus();
                            else
                                addSourceRow.forceActiveFocus();
                        }
                        KeyNavigation.right: saveButton
                    }
                }
                BigscreenUI.ButtonDelegate {
                    id: addSourceRow
                    width: parent.width
                    focus: sourceRepeater.count === 0
                    text: qsTr("Add source")
                    description: qsTr("TVHeadend, an M3U playlist, …")
                    trailing: Kirigami.Icon {
                        source: "list-add"
                        implicitWidth: Kirigami.Units.iconSizes.smallMedium
                        implicitHeight: Kirigami.Units.iconSizes.smallMedium
                    }
                    onClicked: root.editSource("")
                    Keys.onUpPressed: {
                        if (sourceRepeater.count > 0)
                            sourceRepeater.itemAt(sourceRepeater.count - 1).forceActiveFocus();
                        else
                            root.exitUp();
                    }
                    KeyNavigation.down: root.hasTvheadend ? profileRow : bufferRow
                    KeyNavigation.right: saveButton
                }

                Text {
                    text: qsTr("Playback & Appearance")
                    color: Theme.accent
                    font.pixelSize: Theme.fontSmall
                    font.weight: Font.DemiBold
                    font.capitalization: Font.AllUppercase
                    font.letterSpacing: 1
                    leftPadding: 16
                    topPadding: 16
                    bottomPadding: 5
                }

                BigscreenUI.ButtonDelegate {
                    id: profileRow
                    width: parent.width
                    visible: root.hasTvheadend
                    text: qsTr("Streaming profile")
                    description: root.dProfile.length > 0 ? root.dProfile : "pass"
                    trailing: EditIcon {}
                    onClicked: root.editText(text, root.dProfile, "text", v => root.dProfile = v.trim().length > 0 ? v.trim() : "pass")
                    KeyNavigation.up: addSourceRow
                    KeyNavigation.down: bufferRow
                    KeyNavigation.right: saveButton
                }
                BigscreenUI.ButtonDelegate {
                    id: bufferRow
                    width: parent.width
                    text: qsTr("Playback buffer")
                    description: root.dBuffer < 0 ? qsTr("Auto: starts instantly, waits longer after the connection stalls") : root.dBuffer === 0 ? qsTr("Off") : qsTr("%1 seconds behind live").arg(root.dBuffer)
                    trailing: Row {
                        spacing: 5
                        Kirigami.Icon {
                            source: "go-previous"
                            implicitWidth: Kirigami.Units.iconSizes.small
                            implicitHeight: Kirigami.Units.iconSizes.small
                            opacity: root.bufferIndex > 0 ? 1 : 0.3
                        }
                        Kirigami.Icon {
                            source: "go-next"
                            implicitWidth: Kirigami.Units.iconSizes.small
                            implicitHeight: Kirigami.Units.iconSizes.small
                            opacity: root.bufferIndex < root.bufferModes.length - 1 ? 1 : 0.3
                        }
                    }
                    onClicked: root.dBuffer = root.bufferModes[(root.bufferIndex + 1) % root.bufferModes.length]
                    Keys.onLeftPressed: root.dBuffer = root.bufferModes[Math.max(0, root.bufferIndex - 1)]
                    Keys.onRightPressed: root.dBuffer = root.bufferModes[Math.min(root.bufferModes.length - 1, root.bufferIndex + 1)]
                    KeyNavigation.up: root.hasTvheadend ? profileRow : addSourceRow
                    KeyNavigation.down: backgroundRow
                    KeyNavigation.right: saveButton
                }
                BigscreenUI.SwitchDelegate {
                    id: backgroundRow
                    width: parent.width
                    text: qsTr("Stop playback in background")
                    checked: root.dStopInBackground

                    onToggled: {
                        root.dStopInBackground = checked;
                        checked = Qt.binding(() => root.dStopInBackground);
                    }

                    KeyNavigation.up: bufferRow
                    KeyNavigation.down: confirmQuitRow
                    KeyNavigation.right: saveButton
                }
                BigscreenUI.SwitchDelegate {
                    id: confirmQuitRow
                    width: parent.width
                    text: qsTr("Ask to quit with BACK while watching")
                    checked: root.dConfirmQuit

                    onToggled: {
                        root.dConfirmQuit = checked;
                        checked = Qt.binding(() => root.dConfirmQuit);
                    }

                    KeyNavigation.up: backgroundRow
                    KeyNavigation.down: daysRow
                    KeyNavigation.right: saveButton
                }
                // Left/right change the value; too many choices for a ComboBoxDelegate.
                BigscreenUI.ButtonDelegate {
                    id: daysRow
                    width: parent.width
                    text: qsTr("Days of guide data")
                    description: root.dDays === 1 ? qsTr("1 day") : qsTr("%1 days").arg(root.dDays)
                    trailing: Row {
                        spacing: 5
                        Kirigami.Icon {
                            source: "go-previous"
                            implicitWidth: Kirigami.Units.iconSizes.small
                            implicitHeight: Kirigami.Units.iconSizes.small
                            opacity: root.dDays > 1 ? 1 : 0.3
                        }
                        Kirigami.Icon {
                            source: "go-next"
                            implicitWidth: Kirigami.Units.iconSizes.small
                            implicitHeight: Kirigami.Units.iconSizes.small
                            opacity: root.dDays < 14 ? 1 : 0.3
                        }
                    }
                    onClicked: root.dDays = root.dDays >= 14 ? 1 : root.dDays + 1
                    Keys.onLeftPressed: root.dDays = Math.max(1, root.dDays - 1)
                    Keys.onRightPressed: root.dDays = Math.min(14, root.dDays + 1)
                    KeyNavigation.up: confirmQuitRow
                    KeyNavigation.down: schemeRow
                }
                BigscreenUI.ComboBoxDelegate {
                    id: schemeRow
                    width: parent.width
                    text: qsTr("Color scheme")
                    model: Settings.colorSchemes.map(scheme => scheme.name)
                    currentIndex: root.schemeIndex

                    // The option dialog grows with the options; keep it on the screen.
                    Component.onCompleted: {
                        for (const object of data) {
                            if (object instanceof T.Dialog)
                                object.height = Qt.binding(() => Math.min(object.implicitHeight, object.parent.height * 0.9));
                        }
                    }

                    onActivated: index => {
                        root.setScheme(Settings.colorSchemes[index].id);
                        currentIndex = Qt.binding(() => root.schemeIndex);
                    }

                    KeyNavigation.up: daysRow
                    KeyNavigation.down: saveButton
                    KeyNavigation.right: saveButton
                }
            }
        }
    }

    component EditIcon: Kirigami.Icon {
        source: "document-edit"
        implicitWidth: Kirigami.Units.iconSizes.smallMedium
        implicitHeight: Kirigami.Units.iconSizes.smallMedium
    }

    // --- Status & actions --------------------------------------------
    Column {
        id: side
        visible: !editor.visible
        anchors.left: formView.right
        anchors.leftMargin: 32
        anchors.right: parent.right
        anchors.rightMargin: Theme.margin
        y: 11
        spacing: 15

        Rectangle {
            width: parent.width
            height: statusColumn.implicitHeight + 37
            radius: Theme.radius + 5
            color: Theme.card
            border.color: Theme.outline

            Column {
                id: statusColumn
                anchors.fill: parent
                anchors.margins: 19
                spacing: 8

                Row {
                    spacing: 9
                    Rectangle {
                        width: 12
                        height: 12
                        radius: 6
                        anchors.verticalCenter: parent.verticalCenter
                        color: Tvh.status === Tvh.Online ? Theme.success : Tvh.status === Tvh.Offline ? Theme.danger : Theme.warning
                    }
                    Text {
                        text: Tvh.statusText
                        color: Theme.text
                        font.pixelSize: Theme.fontNormal
                        font.weight: Font.DemiBold
                        width: side.width - 60
                        elide: Text.ElideRight
                    }
                }
                // With several sources, each one's state.
                Repeater {
                    model: Tvh.sourceStatus.length > 1 ? Tvh.sourceStatus : []
                    delegate: Text {
                        id: statusLine
                        required property var modelData
                        width: statusColumn.width
                        text: statusLine.modelData.name + " · " + statusLine.modelData.text
                        color: statusLine.modelData.status === Tvh.Offline ? Theme.danger : Theme.textDim
                        font.pixelSize: Theme.fontSmall
                        elide: Text.ElideRight
                    }
                }
                Text {
                    visible: Tvh.serverVersion.length > 0 && Tvh.sourceStatus.length <= 1
                    text: Tvh.serverName + " " + Tvh.serverVersion
                    color: Theme.textDim
                    font.pixelSize: Theme.fontSmall
                }
                Text {
                    text: Tvh.lastUpdate.getTime() > 0 ? qsTr("Guide updated %1 · %2 channels").arg(Qt.formatDateTime(Tvh.lastUpdate, "dd.MM. HH:mm")).arg(Tvh.channels.count) : qsTr("Guide not loaded yet")
                    color: Theme.textDim
                    font.pixelSize: Theme.fontSmall
                }
                Text {
                    width: parent.width
                    visible: Settings.secretStoreStatus.length > 0
                    text: Settings.secretStoreStatus
                    color: Theme.warning
                    font.pixelSize: Theme.fontTiny
                    wrapMode: Text.Wrap
                }
            }
        }

        TvButton {
            id: saveButton
            width: parent.width
            text: root.dirty ? qsTr("Save") : qsTr("Saved")
            icon.name: root.dirty ? "document-save" : "dialog-ok"
            available: root.dirty
            onActivated: root.save()
            KeyNavigation.left: root.firstRow
            Keys.onUpPressed: root.exitUp()
            KeyNavigation.down: refreshButton
        }
        TvButton {
            id: refreshButton
            width: parent.width
            text: qsTr("Reload channels & guide")
            icon.name: "view-refresh"
            available: Settings.configured && !Tvh.busy
            onActivated: Tvh.refresh()
            KeyNavigation.left: root.firstRow
            KeyNavigation.up: saveButton
            KeyNavigation.down: clearButton
        }
        TvButton {
            id: clearButton
            width: parent.width
            text: qsTr("Clear guide cache")
            icon.name: "edit-clear-all"
            onActivated: {
                Tvh.clearCache();
                Tvh.refresh();
            }
            KeyNavigation.left: root.firstRow
            KeyNavigation.up: refreshButton
            KeyNavigation.down: quitButton
        }
        TvButton {
            id: quitButton
            width: parent.width
            text: qsTr("Quit application")
            icon.name: "application-exit"
            onActivated: Qt.quit()
            KeyNavigation.left: root.firstRow
            KeyNavigation.up: clearButton
        }

        Text {
            width: parent.width
            topPadding: 5
            text: qsTr("Remote: arrows to move, OK to select, BACK to return, MENU for settings, GUIDE for the TV guide.")
            color: Theme.textFaint
            font.pixelSize: Theme.fontTiny
            wrapMode: Text.Wrap
        }
    }

    // Edit page of one source, over the rest of the page.
    SourceEditor {
        id: editor
        anchors.fill: parent
        visible: false
        onEditText: (title, value, mode, callback) => root.editText(title, value, mode, callback)
        onDone: {
            visible = false;
            root.firstRow.forceActiveFocus();
        }
    }
}
