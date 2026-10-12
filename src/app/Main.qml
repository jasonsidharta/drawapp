import QtCore
import QtQuick
import QtQuick.Controls
import QtQuick.Dialogs
import QtQuick.Layouts
import DrawApp

ApplicationWindow {
    id: window
    width: 1280
    height: 800
    minimumWidth: 360
    minimumHeight: 320
    visible: true
    title: (canvas.modified ? "*" : "") + canvas.documentTitle + " — DrawApp (prototype)"

    readonly property var swatches: ["#141414", "#ffffff", "#e53935", "#fb8c00", "#fdd835",
                                     "#43a047", "#1e88e5", "#8e24aa"]

    // Action to run once unsaved changes are saved or discarded; null when nothing is pending.
    property var pendingAction: null
    // Action to run after a Save As chosen from the unsaved-changes prompt succeeds.
    property var afterSaveAs: null
    property bool closeConfirmed: false

    function showError(message) {
        errorDialog.text = message
        errorDialog.open()
    }

    // Runs action now, or after the user saves or discards unsaved changes. Cancel drops it.
    function whenChangesHandled(action) {
        if (!canvas.modified) {
            action()
            return
        }
        pendingAction = action
        unsavedDialog.open()
    }

    // Saves to the current file, or asks for one. Runs then() only after a successful save.
    function saveDocument(then) {
        if (canvas.hasFile) {
            const error = canvas.saveFile()
            if (error) {
                showError(error)
                return
            }
            if (then)
                then()
        } else {
            saveAsDocument(then)
        }
    }

    function saveAsDocument(then) {
        afterSaveAs = then || null
        saveDialog.open()
    }

    function newDocument() { whenChangesHandled(() => canvas.newDocument()) }
    function openDocument() { whenChangesHandled(() => openDialog.open()) }

    onClosing: (close) => {
        if (canvas.modified && !closeConfirmed) {
            close.accepted = false
            whenChangesHandled(() => {
                // Still modified here means the user chose Discard (a save would have cleared it).
                if (canvas.modified)
                    canvas.discardForClose()
                closeConfirmed = true
                window.close()
            })
        }
    }

    function showRecoveryMessages() {
        if (canvas.recoveryWarning) {
            showError(canvas.recoveryWarning)
            canvas.clearRecoveryWarning()
        }
        if (canvas.recoveryOffered && !recoveryDialog.visible)
            recoveryDialog.open()
    }

    Connections {
        target: canvas
        function onRecoveryChanged() { Qt.callLater(window.showRecoveryMessages) }
    }

    header: ToolBar {
        Flow {
            width: parent.width
            spacing: 6
            padding: 4

            ToolButton {
                text: "File"
                onClicked: fileMenu.open()
                Menu {
                    id: fileMenu
                    y: parent.height
                    MenuItem { text: "New   (Ctrl+N)"; onTriggered: window.newDocument() }
                    MenuItem { text: "Open…   (Ctrl+O)"; onTriggered: window.openDocument() }
                    MenuItem { text: "Save   (Ctrl+S)"; onTriggered: window.saveDocument(null) }
                    MenuItem { text: "Save As…   (Ctrl+Shift+S)"; onTriggered: window.saveAsDocument(null) }
                    MenuSeparator {}
                    MenuItem { text: "Export PNG…   (Ctrl+Shift+E)"; onTriggered: exportDialog.open() }
                }
            }
            ToolButton {
                text: "Undo"
                enabled: canvas.canUndo
                onClicked: canvas.undo()
                ToolTip.visible: hovered
                ToolTip.text: (canvas.undoText ? "Undo " + canvas.undoText.toLowerCase() : "Undo") + " (Ctrl+Z)"
            }
            ToolButton {
                text: "Redo"
                enabled: canvas.canRedo
                onClicked: canvas.redo()
                ToolTip.visible: hovered
                ToolTip.text: (canvas.redoText ? "Redo " + canvas.redoText.toLowerCase() : "Redo") + " (Ctrl+Y)"
            }

            ToolSeparator {}

            ButtonGroup { id: toolGroup }

            ToolButton {
                text: "Brush"
                checkable: true
                checked: canvas.tool === CanvasItem.Brush
                ButtonGroup.group: toolGroup
                onClicked: canvas.tool = CanvasItem.Brush
                ToolTip.visible: hovered
                ToolTip.text: "Brush (B)"
            }
            ToolButton {
                text: "Eraser"
                checkable: true
                checked: canvas.tool === CanvasItem.Eraser
                ButtonGroup.group: toolGroup
                onClicked: canvas.tool = CanvasItem.Eraser
                ToolTip.visible: hovered
                ToolTip.text: "Eraser (E)"
            }

            ToolSeparator {}

            Label { text: "Size"; height: sizeSlider.height; verticalAlignment: Text.AlignVCenter }
            Slider {
                id: sizeSlider
                width: 160
                from: 1
                to: 256
                stepSize: 1
                value: canvas.brushSize
                onMoved: canvas.brushSize = Math.round(value)
            }
            Label {
                text: canvas.brushSize + " px"
                width: 48
                height: sizeSlider.height
                verticalAlignment: Text.AlignVCenter
            }

            ToolSeparator {}

            Repeater {
                model: window.swatches
                delegate: Rectangle {
                    required property string modelData
                    width: 26
                    height: 26
                    y: 4
                    radius: 4
                    color: modelData
                    border.width: Qt.colorEqual(canvas.brushColor, modelData) ? 3 : 1
                    border.color: Qt.colorEqual(canvas.brushColor, modelData) ? "#2979ff" : "#808080"
                    MouseArea {
                        anchors.fill: parent
                        onClicked: {
                            canvas.brushColor = parent.modelData
                            canvas.tool = CanvasItem.Brush
                        }
                    }
                }
            }
            ToolButton {
                text: "More…"
                onClicked: {
                    colorDialog.selectedColor = canvas.brushColor
                    colorDialog.open()
                }
            }
            Rectangle {
                width: 26
                height: 26
                y: 4
                radius: 13
                color: canvas.brushColor
                border.color: "#808080"
            }

            ToolSeparator {}

            ToolButton { text: "−"; onClicked: canvas.zoomOut(); ToolTip.visible: hovered; ToolTip.text: "Zoom out (Ctrl+-)" }
            ToolButton {
                text: Math.round(canvas.zoom * 100) + "%"
                onClicked: canvas.zoomToActualSize()
                ToolTip.visible: hovered
                ToolTip.text: "Actual size (Ctrl+1)"
            }
            ToolButton { text: "+"; onClicked: canvas.zoomIn(); ToolTip.visible: hovered; ToolTip.text: "Zoom in (Ctrl+=)" }
            ToolButton { text: "Fit"; onClicked: canvas.fitToView(); ToolTip.visible: hovered; ToolTip.text: "Fit to window (Ctrl+0)" }

            ToolSeparator {}

            ToolButton {
                text: "Clear"
                onClicked: canvas.clearCanvas()
                ToolTip.visible: hovered
                ToolTip.text: "Clear the canvas (can be undone)"
            }
        }
    }

    CanvasItem {
        id: canvas
        anchors.fill: parent
        focus: true
        enabled: !unsavedDialog.visible && !errorDialog.visible && !recoveryDialog.visible
    }

    footer: ToolBar {
        Label {
            anchors.fill: parent
            anchors.leftMargin: 8
            verticalAlignment: Text.AlignVCenter
            elide: Text.ElideRight
            text: canvas.inputStatus + "   ·   Canvas " + canvas.documentSize.width + " × "
                  + canvas.documentSize.height + " px"
                  + (canvas.recoveryStatus ? "   ·   " + canvas.recoveryStatus : "")
                  + "   ·   Wheel: zoom   ·   Middle-drag or Space+drag: pan"
        }
    }

    ColorDialog {
        id: colorDialog
        onAccepted: {
            canvas.brushColor = selectedColor
            canvas.tool = CanvasItem.Brush
        }
    }

    FileDialog {
        id: openDialog
        title: "Open project"
        fileMode: FileDialog.OpenFile
        nameFilters: ["DrawApp project (*.drawapp)", "All files (*)"]
        currentFolder: StandardPaths.writableLocation(StandardPaths.DocumentsLocation)
        onAccepted: {
            const error = canvas.openFile(selectedFile)
            if (error)
                window.showError(error)
        }
    }

    FileDialog {
        id: saveDialog
        title: "Save project as"
        fileMode: FileDialog.SaveFile
        defaultSuffix: "drawapp"
        nameFilters: ["DrawApp project (*.drawapp)"]
        currentFolder: StandardPaths.writableLocation(StandardPaths.DocumentsLocation)
        onAccepted: {
            const then = window.afterSaveAs
            window.afterSaveAs = null
            const error = canvas.saveFileAs(selectedFile)
            if (error) {
                window.showError(error)
                return
            }
            if (then)
                then()
        }
        // Cancelling Save As also cancels whatever was waiting for the save.
        onRejected: window.afterSaveAs = null
    }

    FileDialog {
        id: exportDialog
        title: "Export PNG"
        fileMode: FileDialog.SaveFile
        defaultSuffix: "png"
        nameFilters: ["PNG image (*.png)"]
        currentFolder: StandardPaths.writableLocation(StandardPaths.PicturesLocation)
        onAccepted: {
            const error = canvas.exportPng(selectedFile)
            if (error)
                window.showError(error)
        }
    }

    Dialog {
        id: unsavedDialog
        anchors.centerIn: parent
        modal: true
        title: "Save changes?"
        focus: true
        standardButtons: Dialog.Save | Dialog.Discard | Dialog.Cancel
        Label {
            text: "\"" + canvas.documentTitle + "\" has unsaved changes."
            focus: true
            Keys.onReturnPressed: unsavedDialog.accept()
            Keys.onEnterPressed: unsavedDialog.accept()
        }
        onAccepted: {
            const action = window.pendingAction
            window.pendingAction = null
            window.saveDocument(action)
        }
        onDiscarded: {
            const action = window.pendingAction
            window.pendingAction = null
            unsavedDialog.close()
            if (action)
                action()
        }
        onRejected: window.pendingAction = null
        onClosed: canvas.forceActiveFocus()
    }

    Dialog {
        id: recoveryDialog
        property bool decided: false
        anchors.centerIn: parent
        width: Math.min(window.width - 32, 520)
        modal: true
        focus: true
        title: "Recover unsaved artwork?"
        footer: DialogButtonBox {
            Button { text: "Restore"; DialogButtonBox.buttonRole: DialogButtonBox.AcceptRole }
            Button { text: "Discard"; DialogButtonBox.buttonRole: DialogButtonBox.DestructiveRole }
        }
        Label {
            width: parent.width
            wrapMode: Text.Wrap
            text: canvas.recoveryDescription
                  + "\n\nRestored artwork stays marked as unsaved until you save it. Discard deletes the recovery data."
            focus: true
            Keys.onReturnPressed: recoveryDialog.accept()
            Keys.onEnterPressed: recoveryDialog.accept()
        }
        onOpened: decided = false
        onAccepted: {
            decided = true
            canvas.restoreRecovery()
        }
        onDiscarded: {
            decided = true
            recoveryDialog.close()
            canvas.discardRecovery() // may offer the next one through recoveryChanged
        }
        // Escape: keep the recovery data for the next start without restoring it now.
        onClosed: {
            if (!decided)
                canvas.postponeRecovery()
            canvas.forceActiveFocus()
        }
    }

    Dialog {
        id: errorDialog
        property alias text: errorLabel.text
        anchors.centerIn: parent
        width: Math.min(window.width - 32, 480)
        modal: true
        focus: true
        title: "DrawApp"
        standardButtons: Dialog.Ok
        Label {
            id: errorLabel
            width: parent.width
            wrapMode: Text.Wrap
            focus: true
            Keys.onReturnPressed: errorDialog.accept()
            Keys.onEnterPressed: errorDialog.accept()
        }
        onClosed: canvas.forceActiveFocus()
    }

    Shortcut { sequences: [StandardKey.Undo]; onActivated: canvas.undo() }
    Shortcut { sequences: [StandardKey.Redo, "Ctrl+Shift+Z"]; onActivated: canvas.redo() }
    Shortcut { sequences: [StandardKey.New]; onActivated: window.newDocument() }
    Shortcut { sequences: [StandardKey.Open]; onActivated: window.openDocument() }
    Shortcut { sequences: [StandardKey.Save]; onActivated: window.saveDocument(null) }
    Shortcut { sequences: ["Ctrl+Shift+S"]; onActivated: window.saveAsDocument(null) }
    Shortcut { sequences: ["Ctrl+Shift+E"]; onActivated: exportDialog.open() }
    Shortcut { sequence: "B"; onActivated: canvas.tool = CanvasItem.Brush }
    Shortcut { sequence: "E"; onActivated: canvas.tool = CanvasItem.Eraser }
    Shortcut { sequence: "["; onActivated: canvas.brushSize = Math.max(1, Math.round(canvas.brushSize / 1.2)) }
    Shortcut { sequence: "]"; onActivated: canvas.brushSize = Math.min(256, Math.round(canvas.brushSize * 1.2) + 1) }
    Shortcut { sequences: [StandardKey.ZoomIn, "Ctrl+="]; onActivated: canvas.zoomIn() }
    Shortcut { sequences: [StandardKey.ZoomOut]; onActivated: canvas.zoomOut() }
    Shortcut { sequence: "Ctrl+0"; onActivated: canvas.fitToView() }
    Shortcut { sequence: "Ctrl+1"; onActivated: canvas.zoomToActualSize() }
}
