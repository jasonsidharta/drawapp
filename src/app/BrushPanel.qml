import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import DrawApp

// Brush presets and settings: the preset list with thumbnails drawn by the real brush engine,
// a scratchpad on the same engine, the basic settings and, expandable, the advanced ones.
// Edits change the working brush for this session; Save writes them into the preset.
Item {
    id: panel

    required property CanvasItem canvas
    readonly property BrushLibrary lib: canvas.brushes
    readonly property var cur: lib.current
    readonly property bool wide: width >= 620
    property bool favoritesOnly: false
    property bool advanced: false
    // The full editor (Brush Studio) was asked for.
    signal studioRequested()

    implicitHeight: layout.implicitHeight

    // A setting of the current brush by its key ("size", "pressureSize.minimum", ...).
    function val(key) {
        const dot = key.indexOf(".")
        return dot < 0 ? cur[key] : cur[key.slice(0, dot)][key.slice(dot + 1)]
    }

    component SectionLabel: Label {
        Layout.fillWidth: true
        Layout.topMargin: 8
        font.bold: true
    }

    component Hint: Label {
        Layout.fillWidth: true
        wrapMode: Text.Wrap
        font.pixelSize: 11
        opacity: 0.8
    }

    // Slider value = stored value × factor.
    component SettingSlider: RowLayout {
        id: row
        property string key
        property string label
        property real from: 0
        property real to: 100
        property real stepSize: 1
        property real factor: 100
        property string suffix: "%"
        property string tip
        property alias slider: control
        Layout.fillWidth: true
        spacing: 6
        Label {
            text: row.label
            Layout.preferredWidth: 104
            elide: Text.ElideRight
        }
        Slider {
            id: control
            Layout.fillWidth: true
            Layout.minimumHeight: 32 // a comfortable touch target
            from: row.from
            to: row.to
            stepSize: row.stepSize
            value: panel.val(row.key) * row.factor
            onMoved: panel.lib.setValue(row.key, value / row.factor)
            // Delayed, so the tip does not cover the next slider while this one is used.
            ToolTip.delay: 800
            ToolTip.visible: hovered && !pressed && row.tip !== ""
            ToolTip.text: row.tip
        }
        Label {
            text: Math.round(control.value) + row.suffix
            Layout.preferredWidth: 52
            horizontalAlignment: Text.AlignRight
        }
    }

    component SettingSwitch: Switch {
        property string key
        Layout.fillWidth: true
        checked: panel.val(key) === true
        onToggled: panel.lib.setValue(key, checked)
    }

    component PresetEntry: Rectangle {
        id: entry
        required property var modelData
        Layout.fillWidth: true
        implicitHeight: 54
        radius: 6
        color: modelData.current ? "#dbe8ff" : (hover.hovered ? "#f1f1f1" : "#ffffff")
        border.width: modelData.current ? 2 : 1
        border.color: modelData.current ? "#2f6fdf" : "#c9c9c9"
        objectName: "preset_" + modelData.id

        HoverHandler { id: hover }
        TapHandler { onTapped: panel.lib.select(entry.modelData.id) }

        RowLayout {
            anchors.fill: parent
            anchors.leftMargin: 6
            anchors.rightMargin: 2
            spacing: 8
            Image {
                source: entry.modelData.thumbnail
                sourceSize: Qt.size(168, 52)
                Layout.preferredWidth: 112
                Layout.preferredHeight: 35
                fillMode: Image.PreserveAspectFit
                asynchronous: true
            }
            ColumnLayout {
                Layout.fillWidth: true
                spacing: 0
                Label {
                    Layout.fillWidth: true
                    text: entry.modelData.name
                    font.bold: entry.modelData.current
                    elide: Text.ElideRight
                }
                Label {
                    Layout.fillWidth: true
                    font.pixelSize: 10
                    opacity: 0.75
                    elide: Text.ElideRight
                    text: [entry.modelData.blend === "erase" ? "eraser" : (entry.modelData.blend === "smudge" ? "smudge" : ""),
                           entry.modelData.builtIn ? "" : "custom",
                           entry.modelData.savedChanges ? "changed" : "",
                           entry.modelData.modified ? "unsaved edits" : ""].filter(s => s !== "").join(" · ")
                }
            }
            ToolButton {
                objectName: "favorite_" + entry.modelData.id
                text: entry.modelData.favorite ? "★" : "☆"
                font.pixelSize: 18
                implicitWidth: 40
                implicitHeight: 40
                onClicked: panel.lib.toggleFavorite(entry.modelData.id)
                ToolTip.delay: 600
                ToolTip.visible: hovered
                ToolTip.text: entry.modelData.favorite ? "Remove from favourites" : "Add to favourites"
            }
        }
    }

    GridLayout {
        id: layout
        width: panel.width
        columns: panel.wide ? 2 : 1
        columnSpacing: 14
        rowSpacing: 8

        // --- presets ---
        ColumnLayout {
            Layout.alignment: Qt.AlignTop
            Layout.preferredWidth: panel.wide ? 300 : panel.width
            Layout.fillWidth: !panel.wide
            spacing: 4

            RowLayout {
                Layout.fillWidth: true
                Label {
                    text: "Brushes"
                    font.bold: true
                    Layout.fillWidth: true
                }
                Button {
                    text: "All"
                    flat: true
                    checkable: true
                    checked: !panel.favoritesOnly
                    onClicked: panel.favoritesOnly = false
                }
                Button {
                    objectName: "favoritesFilter"
                    text: "★ Favourites"
                    flat: true
                    checkable: true
                    checked: panel.favoritesOnly
                    onClicked: panel.favoritesOnly = true
                }
            }
            Repeater {
                model: panel.lib.presets.filter(p => !panel.favoritesOnly || p.favorite)
                delegate: PresetEntry {}
            }
            Hint {
                visible: panel.favoritesOnly && !panel.lib.presets.some(p => p.favorite)
                text: "No favourites yet: tap ☆ next to a brush."
            }
        }

        // --- the current brush ---
        ColumnLayout {
            Layout.alignment: Qt.AlignTop
            Layout.fillWidth: true
            spacing: 2

            RowLayout {
                Layout.fillWidth: true
                Label {
                    objectName: "currentBrushName"
                    text: panel.lib.currentName
                    font.bold: true
                    font.pixelSize: 16
                    elide: Text.ElideRight
                    Layout.fillWidth: true
                }
                Label {
                    visible: panel.lib.currentModified
                    text: "unsaved edits"
                    color: "#b35a00"
                }
            }
            BrushScratchpad {
                id: scratchpad
                objectName: "brushScratchpad"
                canvas: panel.canvas
                Layout.fillWidth: true
                Layout.preferredHeight: 120
            }
            RowLayout {
                Layout.fillWidth: true
                Hint {
                    text: "Try the brush here: same renderer and pen handling as the canvas, not part of the drawing."
                }
                Button {
                    text: "Clear"
                    onClicked: scratchpad.clear()
                }
            }

            SettingSlider {
                key: "size"
                label: "Size"
                from: 1
                to: 256
                factor: 1
                suffix: " px"
                slider.objectName: "panelSizeSlider"
            }
            SettingSlider {
                key: "opacity"
                label: "Opacity"
                from: 1
                slider.objectName: "brushOpacitySlider"
                tip: "The most one stroke can cover. Overlaps within a stroke never go past it; separate strokes build up."
            }
            SettingSlider {
                visible: panel.cur.accumulation === "buildUp" || panel.cur.airbrush
                key: "flow"
                label: "Flow"
                from: 1
                slider.objectName: "flowSlider"
                tip: "How much one pass adds, towards the opacity. Slow or repeated passes within the stroke build up."
            }
            SettingSlider {
                visible: panel.cur.tip === ""
                key: "hardness"
                label: "Hardness"
                tip: "100% = crisp anti-aliased edge; lower = softer falloff"
            }
            SettingSlider {
                visible: panel.cur.blend === "smudge"
                key: "smudgeStrength"
                label: "Strength"
                tip: "How strongly the carried colour is laid down"
            }
            SettingSlider {
                key: "smoothing"
                label: "Smoothing"
                factor: 1
                suffix: ""
                slider.objectName: "smoothingSlider"
                tip: "The brush trails the pen by up to " + Math.round(panel.cur.smoothing * 0.4)
                     + " screen pixels and catches up when you lift it"
            }
            SettingSlider {
                visible: panel.cur.pressureSize.enabled
                key: "pressureSize.minimum"
                label: "Lightest size"
                slider.objectName: "pressureMinSlider"
                tip: "Size at the lightest pen pressure, as a share of the brush size"
            }
            Hint {
                visible: panel.lib.currentSizeChanged
                text: "The size is temporary: it is not saved into the preset unless you press Save."
            }

            Button {
                objectName: "advancedButton"
                Layout.topMargin: 4
                flat: true
                // Plain text: Android's default font has no triangle arrows.
                text: panel.advanced ? "Fewer settings" : "More settings…"
                onClicked: panel.advanced = !panel.advanced
            }
            Button {
                objectName: "openStudioButton"
                flat: true
                text: "Brush Studio…"
                onClicked: panel.studioRequested()
                ToolTip.delay: 600
                ToolTip.visible: hovered
                ToolTip.text: "Every setting of this brush, with sensor curves and a preview"
            }

            ColumnLayout {
                visible: panel.advanced
                Layout.fillWidth: true
                spacing: 2

                SectionLabel { text: "Stroke" }
                SettingSlider {
                    key: "spacing"
                    label: "Spacing"
                    from: 2
                    to: 100
                    tip: "Distance between dabs as a share of the size. Flow is adjusted to it, so only smoothness changes."
                }
                Switch {
                    objectName: "buildUpSwitch"
                    Layout.fillWidth: true
                    text: "Build up within a stroke"
                    checked: panel.cur.accumulation === "buildUp"
                    onToggled: panel.lib.setValue("accumulation", checked ? "buildUp" : "uniform")
                }
                Hint {
                    text: panel.cur.accumulation === "buildUp"
                          ? "Slow and repeated passes darken gradually, up to the opacity."
                          : "Uniform: the stroke reaches its opacity at once; overlaps never darken."
                }
                SettingSwitch {
                    visible: panel.cur.blend !== "smudge"
                    key: "airbrush"
                    text: "Airbrush: keeps adding while held still"
                }
                SettingSlider {
                    visible: panel.cur.airbrush && panel.cur.blend !== "smudge"
                    key: "airbrushRate"
                    label: "Rate"
                    from: 1
                    to: 200
                    factor: 1
                    suffix: "/s"
                    tip: "Dabs per second while the pen is held; depends on time, not on how many events the pen sends"
                }

                SectionLabel { text: "Pen pressure" }
                Hint { text: "Only pens that report pressure use these. Mouse and touch always draw at full pressure." }
                SettingSwitch {
                    key: "pressureSize.enabled"
                    text: "Pressure changes size"
                }
                RowLayout {
                    visible: panel.cur.pressureSize.enabled
                    Layout.fillWidth: true
                    spacing: 6
                    Label {
                        text: "Response"
                        Layout.preferredWidth: 104
                    }
                    Slider {
                        objectName: "pressureFirmnessSlider"
                        Layout.fillWidth: true
                        Layout.minimumHeight: 32
                        from: -100
                        to: 100
                        stepSize: 5
                        value: Math.round(panel.canvas.pressureFirmness * 100)
                        onMoved: panel.canvas.pressureFirmness = value / 100
                        ToolTip.delay: 800
                        ToolTip.visible: hovered && !pressed
                        ToolTip.text: "Soft: light pressure already draws large. Firm: needs more pressure."
                    }
                    Label {
                        Layout.preferredWidth: 52
                        horizontalAlignment: Text.AlignRight
                        text: panel.canvas.pressureFirmness < -0.02 ? "Soft" : (panel.canvas.pressureFirmness > 0.02 ? "Firm" : "Linear")
                    }
                }
                SettingSwitch {
                    key: "pressureOpacity.enabled"
                    text: panel.cur.blend === "smudge" ? "Pressure changes strength" : "Pressure changes opacity"
                }
                SettingSlider {
                    visible: panel.cur.pressureOpacity.enabled
                    key: "pressureOpacity.minimum"
                    label: panel.cur.blend === "smudge" ? "Lightest strength" : "Lightest opacity"
                }
                SettingSwitch {
                    visible: panel.cur.accumulation === "buildUp" || panel.cur.airbrush
                    key: "pressureFlow.enabled"
                    text: "Pressure changes flow"
                }
                SettingSlider {
                    visible: panel.cur.pressureFlow.enabled && (panel.cur.accumulation === "buildUp" || panel.cur.airbrush)
                    key: "pressureFlow.minimum"
                    label: "Lightest flow"
                }

                SectionLabel { text: "Tip" }
                SettingSlider {
                    key: "roundness"
                    label: "Roundness"
                    from: 5
                }
                SettingSlider {
                    key: "angle"
                    label: "Angle"
                    from: -180
                    to: 180
                    factor: 1
                    suffix: "°"
                }
                SettingSwitch {
                    key: "followDirection"
                    text: "Turn with the stroke (or with pen tilt, if reported)"
                }
                SettingSlider {
                    key: "angleJitter"
                    label: "Angle jitter"
                    from: 0
                    to: 180
                    factor: 1
                    suffix: "°"
                }

                SectionLabel { text: "Texture" }
                RowLayout {
                    Layout.fillWidth: true
                    Label {
                        text: "Grain"
                        Layout.preferredWidth: 104
                    }
                    ComboBox {
                        id: textureBox
                        Layout.fillWidth: true
                        readonly property var ids: ["", "paper", "canvas"]
                        model: ["None", "Paper", "Canvas"]
                        currentIndex: Math.max(0, ids.indexOf(panel.cur.texture))
                        onActivated: index => {
                            panel.lib.setValue("texture", ids[index])
                            if (index > 0 && panel.cur.textureStrength === 0)
                                panel.lib.setValue("textureStrength", 0.4)
                        }
                    }
                }
                SettingSlider {
                    visible: panel.cur.texture !== ""
                    key: "textureStrength"
                    label: "Strength"
                }
                SettingSlider {
                    visible: panel.cur.texture !== ""
                    key: "textureScale"
                    label: "Scale"
                    from: 10
                    to: 800
                    stepSize: 5
                }
                RowLayout {
                    visible: panel.cur.texture !== ""
                    Layout.fillWidth: true
                    Label {
                        text: "Mode"
                        Layout.preferredWidth: 104
                    }
                    ComboBox {
                        Layout.fillWidth: true
                        model: ["Multiply", "Tooth (pressure fills it)"]
                        currentIndex: panel.cur.textureMode === "subtract" ? 1 : 0
                        onActivated: index => panel.lib.setValue("textureMode", index === 1 ? "subtract" : "multiply")
                    }
                }

                SectionLabel { text: "Pen tilt" }
                Hint { text: "Used only when the pen reports tilt; otherwise the brush draws as if upright." }
                SettingSlider {
                    key: "tiltSize"
                    label: "Size tilted"
                    from: 25
                    to: 400
                    stepSize: 5
                }
                SettingSlider {
                    key: "tiltOpacity"
                    label: panel.cur.blend === "smudge" ? "Strength tilted" : "Opacity tilted"
                }

                SectionLabel {
                    visible: panel.cur.blend === "smudge"
                    text: "Smudge"
                }
                SettingSlider {
                    visible: panel.cur.blend === "smudge"
                    key: "smudgePickup"
                    label: "Pickup"
                    tip: "How quickly the carried colour takes on the colours it passes"
                }
                Hint {
                    visible: panel.cur.blend === "smudge"
                    text: "Pushes and blends the colours on the active layer (not a paint simulation). "
                          + "Smudging into transparency spreads paint; smudging transparency into paint thins it."
                }
            }

            Flow {
                Layout.fillWidth: true
                Layout.topMargin: 8
                spacing: 6
                Button {
                    objectName: "saveBrushButton"
                    text: "Save"
                    enabled: panel.lib.currentModified || panel.lib.currentSizeChanged
                    onClicked: panel.lib.saveCurrent()
                    ToolTip.delay: 600
                    ToolTip.visible: hovered
                    ToolTip.text: "Save these settings into “" + panel.lib.currentName + "”"
                }
                Button {
                    objectName: "saveAsNewButton"
                    text: "Save as new…"
                    onClicked: nameDialog.ask(false)
                }
                Button {
                    objectName: "resetBrushButton"
                    text: panel.lib.currentBuiltIn ? "Reset to default" : "Revert"
                    enabled: panel.lib.currentModified || panel.lib.currentSizeChanged || panel.lib.currentHasSavedChanges
                    onClicked: panel.lib.resetCurrent()
                    ToolTip.delay: 600
                    ToolTip.visible: hovered
                    ToolTip.text: panel.lib.currentBuiltIn ? "Back to the original built-in settings"
                                                           : "Back to the last saved settings"
                }
                Button {
                    visible: !panel.lib.currentBuiltIn
                    text: "Rename…"
                    onClicked: nameDialog.ask(true)
                }
                Button {
                    visible: !panel.lib.currentBuiltIn
                    text: "Delete…"
                    onClicked: deleteDialog.open()
                }
            }
            Label {
                visible: panel.lib.storeStatus !== ""
                Layout.fillWidth: true
                wrapMode: Text.Wrap
                color: "#b00020"
                text: panel.lib.storeStatus
            }
        }
    }

    Dialog {
        id: nameDialog
        property bool rename: false
        function ask(isRename) {
            rename = isRename
            nameField.text = isRename ? panel.lib.currentName : panel.lib.currentName + " copy"
            open()
        }
        parent: Overlay.overlay
        anchors.centerIn: parent
        modal: true
        title: rename ? "Rename brush" : "Save as a new brush"
        standardButtons: Dialog.Ok | Dialog.Cancel
        onOpened: {
            nameField.forceActiveFocus()
            nameField.selectAll()
        }
        onAccepted: {
            if (rename)
                panel.lib.renameCurrent(nameField.text)
            else
                panel.lib.saveAsNew(nameField.text)
        }
        TextField {
            id: nameField
            objectName: "brushNameField"
            width: 260
            maximumLength: 64
            onAccepted: nameDialog.accept()
        }
    }

    Dialog {
        id: deleteDialog
        parent: Overlay.overlay
        anchors.centerIn: parent
        modal: true
        title: "Delete “" + panel.lib.currentName + "”?"
        standardButtons: Dialog.Ok | Dialog.Cancel
        onAccepted: panel.lib.deleteCurrent()
        Label { text: "The custom brush is removed. This cannot be undone." }
    }
}
