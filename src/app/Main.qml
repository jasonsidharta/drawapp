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
    // Action waiting for the running save to finish successfully (New, Open or Close after Save).
    property var afterSave: null
    property bool closeConfirmed: false
    // The window was closed during a save or open; close again once it has finished.
    property bool closeWhenIdle: false
    // "Save a Copy" was chosen instead of an overwrite: the cancelled save is not an outcome.
    property bool savingCopy: false

    function showError(message) {
        errorDialog.text = message
        errorDialog.open()
    }

    function showNotice(message) {
        noticeLabel.text = message
        noticeBanner.visible = true
        noticeTimer.restart()
    }

    // Runs action now, or after the user saves or discards unsaved changes. Cancel drops it.
    function whenChangesHandled(action) {
        if (transformBlocks())
            return
        if (canvas.fileBusy) {
            showNotice(canvas.fileBusyText + " Try again when it has finished.")
            return
        }
        if (!canvas.modified) {
            action()
            return
        }
        pendingAction = action
        unsavedDialog.open()
    }

    // Starts a save. then() runs only after it succeeds (see onSaveFinished).
    function startSave(call, then) {
        const error = call()
        if (error) {
            showError(error)
            return
        }
        if (then)
            afterSave = then
    }

    // Saves to the current file, or asks for one.
    // A transform preview is not part of the document: file actions wait for Apply or Cancel.
    function transformBlocks() {
        if (!canvas.transforming)
            return false
        showNotice("Apply or cancel the transform first (Enter applies, Esc cancels).")
        return true
    }

    // Runs a clipboard or selection action and shows why it did nothing, if it did nothing.
    function runEdit(result) {
        if (result)
            showNotice(result)
    }

    function saveDocument(then) {
        if (transformBlocks())
            return
        if (canvas.hasFile)
            startSave(() => canvas.saveFile(), then)
        else
            saveAsDocument(then)
    }

    function saveAsDocument(then) {
        if (transformBlocks())
            return
        if (canvas.fileBusy) {
            showNotice(canvas.fileBusyText + " Try again when it has finished.")
            return
        }
        afterSaveAs = then || null
        saveDialog.open()
    }

    function newDocument() { whenChangesHandled(() => canvas.newDocument()) }
    function openDocument() { whenChangesHandled(() => openDialog.open()) }

    Connections {
        target: canvas
        function onSaveFinished(ok, error) {
            if (!ok && !error && window.savingCopy) {
                window.savingCopy = false // the copy is saved through the Save As dialog instead
                return
            }
            const then = window.afterSave
            window.afterSave = null
            if (!ok) {
                if (error)
                    window.showError(error) // the document stays unsaved; nothing else happens
            } else if (then) {
                // Edits made while saving are not in the file: ask again rather than drop them.
                if (canvas.modified)
                    window.whenChangesHandled(then)
                else
                    then()
            }
            window.closeIfWaiting()
        }
        function onOpenFinished(ok, error) {
            if (!ok && error)
                window.showError(error)
            window.closeIfWaiting()
        }
        function onOpenConflict() { openConflictDialog.open() }
        // The pending action (afterSave) stays until the user decides.
        function onOverwriteConfirmationNeeded() { overwriteDialog.open() }
        function onExportFinished(ok, error) {
            if (!ok && error)
                window.showError(error)
            else if (ok)
                window.showNotice("PNG exported.")
            window.closeIfWaiting()
        }
    }

    function closeIfWaiting() {
        if (closeWhenIdle && !canvas.fileBusy) {
            closeWhenIdle = false
            window.close()
        }
    }

    onClosing: (close) => {
        if (canvas.fileBusy) {
            // Never exit in the middle of a save or open; close afterwards (with the usual prompt).
            close.accepted = false
            closeWhenIdle = true
            showNotice(canvas.fileBusyText + " The window closes when it has finished.")
            return
        }
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
        // Android draws apps edge to edge (target API 35+): keep controls out of the status bar and
        // display cutouts. The margins are 0 on desktop.
        topPadding: SafeArea.margins.top
        leftPadding: SafeArea.margins.left
        rightPadding: SafeArea.margins.right
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
                    MenuItem { text: "New   (Ctrl+N)"; enabled: !canvas.fileBusy; onTriggered: window.newDocument() }
                    MenuItem { text: "Open…   (Ctrl+O)"; enabled: !canvas.fileBusy; onTriggered: window.openDocument() }
                    MenuItem { text: "Save   (Ctrl+S)"; enabled: !canvas.opening; onTriggered: window.saveDocument(null) }
                    MenuItem { text: "Save As…   (Ctrl+Shift+S)"; enabled: !canvas.fileBusy; onTriggered: window.saveAsDocument(null) }
                    MenuSeparator {}
                    MenuItem { text: "Export PNG…   (Ctrl+Shift+E)"; enabled: !canvas.fileBusy; onTriggered: { if (!window.transformBlocks()) exportDialog.open() } }
                }
            }
            ToolButton {
                objectName: "editMenuButton"
                text: "Edit"
                onClicked: editMenu.open()
                Menu {
                    id: editMenu
                    y: parent.height
                    MenuItem { text: "Cut   (Ctrl+X)"; enabled: !canvas.opening; onTriggered: window.runEdit(canvas.cutSelection()) }
                    MenuItem { text: "Copy   (Ctrl+C)"; enabled: !canvas.opening; onTriggered: window.runEdit(canvas.copySelection()) }
                    MenuItem { text: "Paste as new layer   (Ctrl+V)"; enabled: canvas.canPaste && !canvas.opening; onTriggered: window.runEdit(canvas.paste()) }
                    MenuItem { text: "Clear selected pixels   (Delete)"; enabled: !canvas.opening; onTriggered: canvas.clearSelected() }
                    MenuSeparator {}
                    MenuItem { text: "Select all   (Ctrl+A)"; onTriggered: canvas.selectAll() }
                    MenuItem { text: "Deselect   (Ctrl+Shift+A)"; enabled: canvas.hasSelection; onTriggered: canvas.deselect() }
                    MenuItem { text: "Reselect   (Ctrl+Shift+D)"; enabled: canvas.canReselect; onTriggered: canvas.reselect() }
                    MenuItem { text: "Invert selection   (Ctrl+Shift+I)"; enabled: canvas.hasSelection; onTriggered: canvas.invertSelection() }
                    MenuSeparator {}
                    MenuItem { text: "Transform   (Ctrl+T)"; enabled: !canvas.transforming && !canvas.opening; onTriggered: canvas.tool = CanvasItem.Transform }
                }
            }
            ToolButton {
                objectName: "undoButton"
                text: "Undo"
                enabled: canvas.canUndo
                onClicked: canvas.undo()
                ToolTip.visible: hovered
                ToolTip.text: (canvas.undoText ? "Undo " + canvas.undoText.toLowerCase() : "Undo") + " (Ctrl+Z)"
            }
            ToolButton {
                objectName: "redoButton"
                text: "Redo"
                enabled: canvas.canRedo
                onClicked: canvas.redo()
                ToolTip.visible: hovered
                ToolTip.text: (canvas.redoText ? "Redo " + canvas.redoText.toLowerCase() : "Redo") + " (Ctrl+Y)"
            }

            ToolSeparator {}

            ButtonGroup { id: toolGroup }

            ToolButton {
                objectName: "brushToolButton"
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
            ToolButton {
                objectName: "selectToolButton"
                text: "Select"
                checkable: true
                checked: canvas.tool === CanvasItem.Select
                ButtonGroup.group: toolGroup
                onClicked: canvas.tool = CanvasItem.Select
                ToolTip.visible: hovered
                ToolTip.text: "Select: rectangle, ellipse or lasso (M). Shift adds, Alt subtracts"
            }
            ToolButton {
                objectName: "transformToolButton"
                text: "Transform"
                checkable: true
                checked: canvas.tool === CanvasItem.Transform
                ButtonGroup.group: toolGroup
                onClicked: canvas.tool = CanvasItem.Transform
                ToolTip.visible: hovered
                ToolTip.text: "Move, scale, rotate or flip the selection or the layer (Ctrl+T)"
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
            Label { text: "Opacity"; height: sizeSlider.height; verticalAlignment: Text.AlignVCenter }
            Slider {
                objectName: "toolbarOpacitySlider"
                width: 100
                from: 1
                to: 100
                stepSize: 1
                value: Math.round(canvas.brushOpacity * 100)
                onMoved: canvas.brushOpacity = value / 100
            }
            Label {
                text: Math.round(canvas.brushOpacity * 100) + "%"
                width: 40
                height: sizeSlider.height
                verticalAlignment: Text.AlignVCenter
            }
            // The active brush, always visible: its real stroke and name. Opens the brush panel.
            ToolButton {
                id: brushSettingsButton
                objectName: "brushSettingsButton"
                checkable: true
                checked: brushPopup.visible
                onClicked: brushPopup.visible ? brushPopup.close() : brushPopup.open()
                ToolTip.visible: hovered && !brushPopup.visible
                ToolTip.text: "Brushes: presets, settings and scratchpad"
                contentItem: Row {
                    spacing: 6
                    Image {
                        anchors.verticalCenter: parent.verticalCenter
                        source: canvas.brushes.currentThumbnail
                        sourceSize: Qt.size(168, 52)
                        width: 78
                        height: 24
                        fillMode: Image.PreserveAspectFit
                        asynchronous: true
                    }
                    Label {
                        objectName: "activeBrushLabel"
                        anchors.verticalCenter: parent.verticalCenter
                        text: canvas.brushes.currentName + (canvas.brushes.currentModified ? " *" : "")
                    }
                }

                Popup {
                    id: brushPopup
                    objectName: "brushPopup"
                    y: parent.height
                    // Inside the window on small screens; scrolls when taller than the window.
                    margins: 8
                    width: Math.min(window.width - 16, 780)
                    height: Math.min(brushPanel.implicitHeight + topPadding + bottomPadding,
                                     window.height - 24 - brushSettingsButton.height)
                    padding: 10
                    // Modal (without dimming): a tap outside closes it without painting.
                    modal: true
                    dim: false
                    onClosed: canvas.forceActiveFocus()

                    ScrollView {
                        id: brushScroll
                        anchors.fill: parent
                        contentWidth: availableWidth
                        clip: true

                        BrushPanel {
                            id: brushPanel
                            objectName: "brushPanel"
                            canvas: canvas
                            width: brushScroll.availableWidth
                        }
                    }
                }
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
                objectName: "clearLayerButton"
                text: "Clear Layer"
                onClicked: canvas.clearActiveLayer()
                ToolTip.visible: hovered
                ToolTip.text: "Clear the active layer (can be undone)"
            }
            ToolButton {
                text: "Layers"
                checkable: true
                checked: layerPanel.visible
                onToggled: layerPanel.visible = checked
                ToolTip.visible: hovered
                ToolTip.text: "Show or hide the layer panel"
            }
        }
    }

    // The active layer's entry in canvas.layers, or null.
    function activeLayer() {
        const list = canvas.layers
        for (let i = 0; i < list.length; ++i) {
            if (list[i].active)
                return list[i]
        }
        return null
    }

    RowLayout {
        anchors.fill: parent
        spacing: 0

        Item {
            Layout.fillWidth: true
            Layout.fillHeight: true

            CanvasItem {
                id: canvas
                objectName: "canvas"
                anchors.fill: parent
                focus: true
                // Painting is blocked while a file is opening, so no edit can be made to a drawing
                // that is about to be replaced (C++ still detects it, see openConflict).
                enabled: !unsavedDialog.visible && !errorDialog.visible && !recoveryDialog.visible
                         && !openConflictDialog.visible && !canvas.opening
                         && !renameDialog.visible && !overwriteDialog.visible
                onNotice: (message) => {
                    noticeLabel.text = message
                    noticeBanner.visible = true
                    noticeTimer.restart()
                }
                onLayersChanged: window.syncOpacitySlider()
            }

            Pane {
                id: contextBar
                objectName: "contextBar"
                visible: canvas.tool === CanvasItem.Select || canvas.transforming
                anchors.bottom: parent.bottom
                anchors.horizontalCenter: parent.horizontalCenter
                anchors.bottomMargin: 10
                // As wide as the visible groups need, wrapping onto more lines on narrow screens.
                readonly property real naturalWidth: {
                    let w = 0
                    for (const g of [shapeRow, modeRow, selectionRow, clipboardRow, numbersRow, adjustRow, applyRow])
                        if (g.visible)
                            w += g.implicitWidth + contextFlow.spacing
                    return w
                }
                width: Math.min(parent.width - 16, naturalWidth + leftPadding + rightPadding)
                padding: 6
                background: Rectangle {
                    radius: 8
                    color: "#f2f4f4f4"
                    border.color: "#a0a0a0"
                }

                // Touch-sized (40 px tall) but only as wide as the text.
                component BarButton: Button {
                    implicitHeight: 40
                    implicitWidth: Math.max(44, contentItem.implicitWidth + 20)
                    leftPadding: 10
                    rightPadding: 10
                    focusPolicy: Qt.NoFocus // keeps Enter and Escape with the canvas
                }

                // A number field that applies on Enter or when it loses focus.
                component NumberField: TextField {
                    property string key
                    property real value
                    property int decimals: 0
                    implicitWidth: 64
                    implicitHeight: 40
                    horizontalAlignment: TextInput.AlignRight
                    inputMethodHints: Qt.ImhFormattedNumbersOnly
                    validator: DoubleValidator { notation: DoubleValidator.StandardNotation }
                    text: activeFocus ? text : value.toFixed(decimals)
                    onEditingFinished: {
                        const v = Number(text.replace(",", "."))
                        if (!isNaN(v))
                            canvas.setTransformValue(key, v)
                        text = Qt.binding(() => activeFocus ? text : value.toFixed(decimals))
                        canvas.forceActiveFocus()
                    }
                }

                Flow {
                    id: contextFlow
                    width: contextBar.availableWidth
                    spacing: 8

                    ButtonGroup { id: shapeGroup }
                    ButtonGroup { id: modeGroup }

                    Row {
                        id: shapeRow
                        visible: !canvas.transforming
                        spacing: 2
                        BarButton { objectName: "shapeRect"; text: "Rectangle"; checkable: true; ButtonGroup.group: shapeGroup
                            checked: canvas.selectShape === CanvasItem.RectangleShape; onClicked: canvas.selectShape = CanvasItem.RectangleShape }
                        BarButton { objectName: "shapeEllipse"; text: "Ellipse"; checkable: true; ButtonGroup.group: shapeGroup
                            checked: canvas.selectShape === CanvasItem.EllipseShape; onClicked: canvas.selectShape = CanvasItem.EllipseShape }
                        BarButton { objectName: "shapeLasso"; text: "Lasso"; checkable: true; ButtonGroup.group: shapeGroup
                            checked: canvas.selectShape === CanvasItem.LassoShape; onClicked: canvas.selectShape = CanvasItem.LassoShape }
                    }
                    Row {
                        id: modeRow
                        visible: !canvas.transforming
                        spacing: 2
                        BarButton { objectName: "modeReplace"; text: "New"; checkable: true; ButtonGroup.group: modeGroup
                            checked: canvas.selectMode === CanvasItem.Replace; onClicked: canvas.selectMode = CanvasItem.Replace
                            ToolTip.visible: hovered; ToolTip.text: "Replace the selection" }
                        BarButton { objectName: "modeAdd"; text: "Add"; checkable: true; ButtonGroup.group: modeGroup
                            checked: canvas.selectMode === CanvasItem.Add; onClicked: canvas.selectMode = CanvasItem.Add
                            ToolTip.visible: hovered; ToolTip.text: "Add to the selection (or hold Shift)" }
                        BarButton { objectName: "modeSubtract"; text: "Subtract"; checkable: true; ButtonGroup.group: modeGroup
                            checked: canvas.selectMode === CanvasItem.Subtract; onClicked: canvas.selectMode = CanvasItem.Subtract
                            ToolTip.visible: hovered; ToolTip.text: "Subtract from the selection (or hold Alt)" }
                        BarButton { objectName: "modeIntersect"; text: "Intersect"; checkable: true; ButtonGroup.group: modeGroup
                            checked: canvas.selectMode === CanvasItem.Intersect; onClicked: canvas.selectMode = CanvasItem.Intersect
                            ToolTip.visible: hovered; ToolTip.text: "Keep only the overlap (or hold Shift+Alt)" }
                    }
                    Row {
                        id: selectionRow
                        visible: !canvas.transforming
                        spacing: 2
                        BarButton { objectName: "selectAllButton"; text: "All"; onClicked: canvas.selectAll() }
                        BarButton { objectName: "deselectButton"; text: "None"; enabled: canvas.hasSelection; onClicked: canvas.deselect() }
                        BarButton { objectName: "invertButton"; text: "Invert"; enabled: canvas.hasSelection; onClicked: canvas.invertSelection() }
                        BarButton { objectName: "reselectButton"; text: "Reselect"; enabled: canvas.canReselect; onClicked: canvas.reselect() }
                    }
                    Row {
                        id: clipboardRow
                        visible: !canvas.transforming
                        spacing: 2
                        BarButton { objectName: "copyButton"; text: "Copy"; onClicked: window.runEdit(canvas.copySelection()) }
                        BarButton { objectName: "cutButton"; text: "Cut"; onClicked: window.runEdit(canvas.cutSelection()) }
                        BarButton { objectName: "pasteButton"; text: "Paste"; enabled: canvas.canPaste; onClicked: window.runEdit(canvas.paste()) }
                        BarButton { objectName: "selectTransformButton"; text: "Transform"; highlighted: true
                            onClicked: canvas.tool = CanvasItem.Transform }
                    }

                    Row {
                        id: numbersRow
                        visible: canvas.transforming
                        spacing: 3
                        Label { text: "X"; anchors.verticalCenter: parent.verticalCenter }
                        NumberField { objectName: "transformXField"; key: "x"; value: canvas.transformX }
                        Label { text: "Y"; anchors.verticalCenter: parent.verticalCenter }
                        NumberField { objectName: "transformYField"; key: "y"; value: canvas.transformY }
                        Label { text: "W"; anchors.verticalCenter: parent.verticalCenter }
                        NumberField { objectName: "transformWidthField"; key: "width"; value: canvas.transformWidth }
                        Label { text: "H"; anchors.verticalCenter: parent.verticalCenter }
                        NumberField { objectName: "transformHeightField"; key: "height"; value: canvas.transformHeight }
                        Label { text: "°"; anchors.verticalCenter: parent.verticalCenter }
                        NumberField { objectName: "transformRotationField"; key: "rotation"; value: canvas.transformRotation; decimals: 1 }
                    }
                    Row {
                        id: adjustRow
                        visible: canvas.transforming
                        spacing: 2
                        BarButton { objectName: "keepAspectButton"; text: "Keep ratio"; checkable: true; checked: canvas.keepAspect
                            onToggled: canvas.keepAspect = checked
                            ToolTip.visible: hovered; ToolTip.text: "Handles keep the proportions (Shift inverts while dragging)" }
                        BarButton { objectName: "flipHButton"; text: "Flip H"; onClicked: canvas.flipTransformHorizontal() }
                        BarButton { objectName: "flipVButton"; text: "Flip V"; onClicked: canvas.flipTransformVertical() }
                        BarButton { objectName: "resetTransformButton"; text: "Reset"; onClicked: canvas.resetTransform() }
                    }
                    Row {
                        id: applyRow
                        visible: canvas.transforming
                        spacing: 2
                        BarButton { objectName: "applyTransformButton"; text: "Apply"; highlighted: true; onClicked: canvas.applyTransform()
                            ToolTip.visible: hovered; ToolTip.text: "Apply (Enter). One undo step. Pixels moved off the canvas are cut off." }
                        BarButton { objectName: "cancelTransformButton"; text: "Cancel"; onClicked: canvas.cancelTransform()
                            ToolTip.visible: hovered; ToolTip.text: "Cancel (Esc): the drawing stays exactly as it was" }
                    }
                }
            }

            Rectangle {
                id: noticeBanner
                objectName: "noticeBanner"
                visible: false
                anchors.top: parent.top
                anchors.horizontalCenter: parent.horizontalCenter
                anchors.topMargin: 12
                width: Math.min(parent.width - 24, noticeLabel.implicitWidth + 32)
                height: noticeLabel.implicitHeight + 16
                radius: 6
                color: "#e6303030"
                border.color: "#f0a030"
                Label {
                    id: noticeLabel
                    objectName: "noticeLabel"
                    anchors.centerIn: parent
                    width: parent.width - 32
                    wrapMode: Text.Wrap
                    horizontalAlignment: Text.AlignHCenter
                    color: "#ffffff"
                }
                Timer { id: noticeTimer; interval: 3500; onTriggered: noticeBanner.visible = false }
            }
        }

        Pane {
            id: layerPanel
            enabled: !canvas.opening
            Layout.preferredWidth: 270
            Layout.fillHeight: true
            visible: window.width >= 720
            padding: 8

            ColumnLayout {
                anchors.fill: parent
                spacing: 6

                Label { text: "Layers (" + canvas.layerCount + "/" + canvas.maxLayers + ")"; font.bold: true }

                ListView {
                    id: layerList
                    Layout.fillWidth: true
                    Layout.fillHeight: true
                    clip: true
                    model: canvas.layers
                    spacing: 2
                    delegate: Rectangle {
                        required property var modelData
                        objectName: "layerRow_" + modelData.name
                        width: layerList.width
                        height: 40
                        radius: 4
                        // Theme colors, so rows stay readable in light and dark Windows themes.
                        color: modelData.active ? window.palette.highlight : window.palette.base
                        border.color: modelData.active ? window.palette.highlight : window.palette.mid

                        MouseArea {
                            anchors.fill: parent
                            onClicked: canvas.setActiveLayer(parent.modelData.id)
                            onDoubleClicked: window.openRename(parent.modelData)
                        }
                        RowLayout {
                            anchors.fill: parent
                            anchors.leftMargin: 4
                            anchors.rightMargin: 4
                            spacing: 2
                            CheckBox {
                                objectName: "visible_" + modelData.name
                                checked: modelData.visible
                                onToggled: canvas.setLayerVisible(modelData.id, checked)
                                ToolTip.visible: hovered
                                ToolTip.text: modelData.visible ? "Visible (click to hide)" : "Hidden (click to show)"
                            }
                            Label {
                                Layout.fillWidth: true
                                text: modelData.name
                                elide: Text.ElideRight
                                color: modelData.active ? window.palette.highlightedText
                                       : (modelData.visible ? window.palette.text : window.palette.placeholderText)
                                font.italic: !modelData.visible
                            }
                            Label {
                                text: Math.round(modelData.opacity * 100) + "%"
                                color: modelData.active ? window.palette.highlightedText : window.palette.text
                            }
                            ToolButton {
                                objectName: "lock_" + modelData.name
                                text: modelData.locked ? "Locked" : "Lock"
                                checkable: true
                                checked: modelData.locked
                                onToggled: canvas.setLayerLocked(modelData.id, checked)
                                ToolTip.visible: hovered
                                ToolTip.text: modelData.locked ? "Locked: painting is blocked (click to unlock)"
                                                               : "Lock painting on this layer"
                            }
                        }
                    }
                }

                GridLayout {
                    Layout.fillWidth: true
                    columns: 3
                    rowSpacing: 2
                    columnSpacing: 2
                    Button {
                        Layout.fillWidth: true
                        objectName: "addLayerButton"
                        text: "Add"
                        enabled: canvas.layerCount < canvas.maxLayers
                        onClicked: canvas.addLayer()
                        ToolTip.visible: hovered
                        ToolTip.text: "New transparent layer above the active one (Ctrl+Shift+N)"
                    }
                    Button {
                        Layout.fillWidth: true
                        objectName: "duplicateLayerButton"
                        text: "Duplicate"
                        enabled: canvas.layerCount < canvas.maxLayers
                        onClicked: canvas.duplicateLayer(canvas.activeLayerId)
                    }
                    Button {
                        Layout.fillWidth: true
                        objectName: "deleteLayerButton"
                        text: "Delete"
                        enabled: canvas.layerCount > 1
                        onClicked: canvas.deleteLayer(canvas.activeLayerId)
                    }
                    Button {
                        Layout.fillWidth: true
                        objectName: "layerUpButton"
                        text: "Up"
                        enabled: canvas.layerCount > 1 && canvas.layers.length > 0 && !canvas.layers[0].active
                        onClicked: canvas.moveLayerUp(canvas.activeLayerId)
                    }
                    Button {
                        Layout.fillWidth: true
                        objectName: "layerDownButton"
                        text: "Down"
                        enabled: canvas.layerCount > 1 && canvas.layers.length > 0
                                 && !canvas.layers[canvas.layers.length - 1].active
                        onClicked: canvas.moveLayerDown(canvas.activeLayerId)
                    }
                    Button {
                        Layout.fillWidth: true
                        objectName: "renameLayerButton"
                        text: "Rename"
                        onClicked: window.openRename(window.activeLayer())
                    }
                }

                RowLayout {
                    Layout.fillWidth: true
                    Label { text: "Opacity" }
                    Slider {
                        id: opacitySlider
                        objectName: "opacitySlider"
                        Layout.fillWidth: true
                        from: 0
                        to: 100
                        stepSize: 1
                        // Dragging previews live; releasing records one undo step for the drag.
                        onMoved: {
                            if (pressed)
                                canvas.previewLayerOpacity(canvas.activeLayerId, value / 100)
                            else
                                canvas.commitLayerOpacity(canvas.activeLayerId, value / 100) // keyboard
                        }
                        onPressedChanged: {
                            if (!pressed)
                                canvas.commitLayerOpacity(canvas.activeLayerId, value / 100)
                        }
                    }
                    Label { text: Math.round(opacitySlider.value) + "%"; Layout.preferredWidth: 36 }
                }
            }
        }
    }

    function syncOpacitySlider() {
        const layer = activeLayer()
        if (layer && !opacitySlider.pressed)
            opacitySlider.value = Math.round(layer.opacity * 100)
    }

    function openRename(layer) {
        if (!layer)
            return
        renameDialog.layerId = layer.id
        renameField.text = layer.name
        renameDialog.open()
        renameField.selectAll()
        renameField.forceActiveFocus()
    }

    Component.onCompleted: syncOpacitySlider()

    footer: ToolBar {
        // Above the Android navigation bar (0 on desktop).
        bottomPadding: SafeArea.margins.bottom
        leftPadding: SafeArea.margins.left
        rightPadding: SafeArea.margins.right
        // The children are anchored, so the content height has to be given explicitly.
        contentHeight: statusLabel.implicitHeight + 6
        BusyIndicator {
            id: fileBusyIndicator
            objectName: "fileBusyIndicator"
            anchors.left: parent.left
            anchors.verticalCenter: parent.verticalCenter
            height: parent.height - 4
            width: visible ? height : 0
            running: canvas.fileBusy
            visible: canvas.fileBusy
        }
        Label {
            id: statusLabel
            anchors.fill: parent
            anchors.leftMargin: 8 + fileBusyIndicator.width
            verticalAlignment: Text.AlignVCenter
            elide: Text.ElideRight
            text: (canvas.fileBusy ? canvas.fileBusyText + "   ·   " : "")
                  + canvas.inputStatus + "   ·   Canvas " + canvas.documentSize.width + " × "
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
            window.startSave(() => canvas.saveFileAs(selectedFile), then)
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
            const error = canvas.exportPng(selectedFile) // the result arrives through onExportFinished
            if (error)
                window.showError(error)
        }
    }

    Dialog {
        id: overwriteDialog
        objectName: "overwriteDialog"
        anchors.centerIn: parent
        width: Math.min(window.width - 32, 500)
        modal: true
        closePolicy: Popup.CloseOnEscape
        title: "Overwrite \"" + canvas.overwriteTarget + "\"?"
        footer: DialogButtonBox {
            Button { text: "Save a Copy…"; DialogButtonBox.buttonRole: DialogButtonBox.ActionRole; onClicked: overwriteDialog.saveCopy() }
            Button { text: "Overwrite"; DialogButtonBox.buttonRole: DialogButtonBox.DestructiveRole }
            Button { text: "Cancel"; DialogButtonBox.buttonRole: DialogButtonBox.RejectRole }
        }
        ColumnLayout {
            width: parent.width
            Label {
                Layout.fillWidth: true
                wrapMode: Text.Wrap
                text: "This file is kept by a storage app that does not let DrawApp replace files safely. "
                      + "Overwriting writes over the existing file directly. If that is interrupted (for example "
                      + "the storage fills up or the app is closed), the file there can be left damaged.\n\n"
                      + "Your drawing stays open in DrawApp either way, and a copy is kept on this device until "
                      + "the file has been written and checked.\n\n"
                      + "Save a Copy creates a new file instead and leaves this one unchanged; you then continue "
                      + "in the new file."
            }
            Label {
                Layout.fillWidth: true
                wrapMode: Text.Wrap
                font.pixelSize: 11
                color: window.palette.placeholderText
                visible: canvas.overwriteDetails !== ""
                text: "Storage details: " + canvas.overwriteDetails
            }
        }
        function saveCopy() {
            window.afterSaveAs = window.afterSave
            window.afterSave = null
            window.savingCopy = true
            overwriteDialog.close()
            canvas.cancelOverwrite()
            saveDialog.open()
        }
        onDiscarded: {
            overwriteDialog.close()
            const error = canvas.confirmOverwrite()
            if (error) {
                window.afterSave = null
                window.showError(error)
            }
        }
        onRejected: {
            window.closeWhenIdle = false
            if (canvas.overwriteConfirmationPending)
                canvas.cancelOverwrite()
        }
        onClosed: canvas.forceActiveFocus()
    }

    Dialog {
        id: unsavedDialog
        anchors.centerIn: parent
        width: Math.min(window.width - 32, 420)
        modal: true
        title: "Save changes?"
        focus: true
        standardButtons: Dialog.Save | Dialog.Discard | Dialog.Cancel
        Label {
            width: parent.width
            wrapMode: Text.Wrap
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
        // A stray touch outside must not dismiss the decision (Escape still postpones).
        closePolicy: Popup.CloseOnEscape
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
        id: openConflictDialog
        anchors.centerIn: parent
        width: Math.min(window.width - 32, 460)
        modal: true
        closePolicy: Popup.CloseOnEscape
        title: "Replace the drawing?"
        footer: DialogButtonBox {
            Button { text: "Open the file"; DialogButtonBox.buttonRole: DialogButtonBox.DestructiveRole }
            Button { text: "Keep my drawing"; DialogButtonBox.buttonRole: DialogButtonBox.RejectRole }
        }
        Label {
            width: parent.width
            wrapMode: Text.Wrap
            text: "The drawing changed while the file was opening. Opening the file now discards those changes."
        }
        onDiscarded: {
            openConflictDialog.close()
            canvas.resolveOpenConflict(true)
        }
        onRejected: canvas.resolveOpenConflict(false)
    }

    Dialog {
        id: renameDialog
        property int layerId: 0
        anchors.centerIn: parent
        width: Math.min(window.width - 32, 360)
        modal: true
        title: "Rename layer"
        standardButtons: Dialog.Ok | Dialog.Cancel
        TextField {
            id: renameField
            objectName: "renameField"
            width: parent.width
            maximumLength: 256
            // On Android the keyboard's Enter otherwise moves focus instead of confirming.
            EnterKey.type: Qt.EnterKeyDone
            onAccepted: renameDialog.accept()
        }
        onAccepted: canvas.renameLayer(layerId, renameField.text)
        onClosed: canvas.forceActiveFocus()
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

    Shortcut { sequences: [StandardKey.Undo]; enabled: !canvas.opening; onActivated: canvas.undo() }
    Shortcut { sequences: [StandardKey.Redo, "Ctrl+Shift+Z"]; enabled: !canvas.opening; onActivated: canvas.redo() }
    Shortcut { sequences: [StandardKey.New]; onActivated: window.newDocument() }
    Shortcut { sequences: [StandardKey.Open]; onActivated: window.openDocument() }
    Shortcut { sequences: [StandardKey.Save]; onActivated: window.saveDocument(null) }
    Shortcut { sequences: ["Ctrl+Shift+S"]; onActivated: window.saveAsDocument(null) }
    Shortcut { sequences: ["Ctrl+Shift+E"]; enabled: !canvas.fileBusy; onActivated: exportDialog.open() }
    Shortcut { sequences: ["Ctrl+Shift+N"]; enabled: !canvas.opening; onActivated: canvas.addLayer() }
    Shortcut { sequence: "B"; enabled: !canvas.opening; onActivated: canvas.tool = CanvasItem.Brush }
    Shortcut { sequence: "E"; enabled: !canvas.opening; onActivated: canvas.tool = CanvasItem.Eraser }
    Shortcut { sequence: "M"; enabled: !canvas.opening; onActivated: canvas.tool = CanvasItem.Select }
    Shortcut { sequence: "Ctrl+T"; enabled: !canvas.opening && !canvas.transforming; onActivated: canvas.tool = CanvasItem.Transform }
    // Not while a number field is being edited: there Enter confirms the number.
    Shortcut { sequences: ["Return", "Enter"]; enabled: canvas.transforming && !(window.activeFocusItem instanceof TextInput)
        onActivated: canvas.applyTransform() }
    Shortcut { sequence: "Escape"; enabled: canvas.transforming && !(window.activeFocusItem instanceof TextInput)
        onActivated: canvas.cancelTransform() }
    Shortcut { sequences: [StandardKey.SelectAll]; enabled: !canvas.opening; onActivated: canvas.selectAll() }
    Shortcut { sequences: ["Ctrl+Shift+A", "Ctrl+D"]; enabled: !canvas.opening; onActivated: canvas.deselect() }
    Shortcut { sequence: "Ctrl+Shift+D"; enabled: !canvas.opening; onActivated: canvas.reselect() }
    Shortcut { sequence: "Ctrl+Shift+I"; enabled: !canvas.opening; onActivated: canvas.invertSelection() }
    Shortcut { sequences: [StandardKey.Copy]; enabled: !canvas.opening; onActivated: window.runEdit(canvas.copySelection()) }
    Shortcut { sequences: [StandardKey.Cut]; enabled: !canvas.opening; onActivated: window.runEdit(canvas.cutSelection()) }
    Shortcut { sequences: [StandardKey.Paste]; enabled: !canvas.opening; onActivated: window.runEdit(canvas.paste()) }
    Shortcut { sequences: [StandardKey.Delete, "Backspace"]; enabled: !canvas.opening && !canvas.transforming; onActivated: canvas.clearSelected() }
    Shortcut { sequence: "["; enabled: !canvas.opening; onActivated: canvas.brushSize = Math.max(1, Math.round(canvas.brushSize / 1.2)) }
    Shortcut { sequence: "]"; enabled: !canvas.opening; onActivated: canvas.brushSize = Math.min(256, Math.round(canvas.brushSize * 1.2) + 1) }
    Shortcut { sequences: [StandardKey.ZoomIn, "Ctrl+="]; onActivated: canvas.zoomIn() }
    Shortcut { sequences: [StandardKey.ZoomOut]; onActivated: canvas.zoomOut() }
    Shortcut { sequence: "Ctrl+0"; onActivated: canvas.fitToView() }
    Shortcut { sequence: "Ctrl+1"; onActivated: canvas.zoomToActualSize() }
}
