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
    title: "DrawApp (prototype)"

    readonly property var swatches: ["#141414", "#ffffff", "#e53935", "#fb8c00", "#fdd835",
                                     "#43a047", "#1e88e5", "#8e24aa"]

    header: ToolBar {
        Flow {
            id: toolbarFlow
            width: parent.width
            spacing: 6
            padding: 4

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

            ToolButton { text: "Clear"; onClicked: clearDialog.open() }
        }
    }

    CanvasItem {
        id: canvas
        anchors.fill: parent
        focus: true
        enabled: !clearDialog.visible
    }

    footer: ToolBar {
        Label {
            anchors.fill: parent
            anchors.leftMargin: 8
            verticalAlignment: Text.AlignVCenter
            elide: Text.ElideRight
            text: canvas.inputStatus + "   ·   Canvas " + canvas.documentSize.width + " × "
                  + canvas.documentSize.height + " px   ·   Wheel: zoom   ·   Middle-drag or Space+drag: pan"
        }
    }

    ColorDialog {
        id: colorDialog
        onAccepted: {
            canvas.brushColor = selectedColor
            canvas.tool = CanvasItem.Brush
        }
    }

    Dialog {
        id: clearDialog
        anchors.centerIn: parent
        modal: true
        title: "Clear the canvas?"
        standardButtons: Dialog.Ok | Dialog.Cancel
        Label { text: "This removes all artwork. There is no undo yet." }
        onAccepted: canvas.clearCanvas()
        onClosed: canvas.forceActiveFocus()
    }

    Shortcut { sequence: "B"; onActivated: canvas.tool = CanvasItem.Brush }
    Shortcut { sequence: "E"; onActivated: canvas.tool = CanvasItem.Eraser }
    Shortcut { sequence: "["; onActivated: canvas.brushSize = Math.max(1, Math.round(canvas.brushSize / 1.2)) }
    Shortcut { sequence: "]"; onActivated: canvas.brushSize = Math.min(256, Math.round(canvas.brushSize * 1.2) + 1) }
    Shortcut { sequences: [StandardKey.ZoomIn, "Ctrl+="]; onActivated: canvas.zoomIn() }
    Shortcut { sequences: [StandardKey.ZoomOut]; onActivated: canvas.zoomOut() }
    Shortcut { sequence: "Ctrl+0"; onActivated: canvas.fitToView() }
    Shortcut { sequence: "Ctrl+1"; onActivated: canvas.zoomToActualSize() }
}
