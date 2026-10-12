import QtQuick
import QtQuick.Controls
import QtQuick.Dialogs
import QtQuick.Layouts
import DrawApp

// The Brush Studio: every setting of one brush, by category or by search, edited on a copy with a
// live preview and a pad to try it. Done saves into the preset; Cancel leaves the preset as it
// was. Only settings the brush's engine uses are shown by default; the others (and the planned
// ones) can be listed with an explanation instead of silently doing nothing.
Item {
    id: page

    required property CanvasItem canvas
    readonly property BrushStudio studio: canvas.brushes.studio
    readonly property bool wide: width >= 760
    property bool showUnused: false
    property bool showPlanned: false
    signal closeRequested()

    function finish(accept) {
        if (accept)
            studio.done()
        else
            studio.cancel()
        page.closeRequested()
    }

    // Scrolls the settings list so that item (inside it) is in view.
    function ensureVisible(item) {
        const flick = scroll.contentItem
        const y = item.mapToItem(flick.contentItem, 0, 0).y
        const maxY = Math.max(0, flick.contentHeight - flick.height)
        if (y < flick.contentY || y + item.height > flick.contentY + flick.height)
            flick.contentY = Math.max(0, Math.min(maxY, y - 12))
    }

    // Shown value with its unit: stored value × scale.
    function shown(row, v) {
        const x = v * row.scale
        const digits = Math.abs(x) >= 100 || row.type === "integer" ? 0 : (Math.abs(x) >= 10 ? 1 : 2)
        return x.toFixed(digits)
    }

    component RowHeader: RowLayout {
        id: header
        required property var row
        Layout.fillWidth: true
        spacing: 4
        Label {
            text: header.row.name
            font.bold: true
            Layout.fillWidth: true
            elide: Text.ElideRight
            opacity: header.row.implemented && header.row.applies ? 1 : 0.55
        }
        ToolButton {
            visible: header.row.implemented && header.row.modified === true
            objectName: "reset_" + header.row.id
            text: "Reset" // plain text: Android's default font has no "↺"
            ToolTip.visible: hovered
            ToolTip.text: "Reset to the brush's saved value"
            onClicked: page.studio.resetValue(header.row.id)
        }
    }

    component Explanation: Label {
        Layout.fillWidth: true
        wrapMode: Text.Wrap
        font.pixelSize: 11
        opacity: 0.75
    }

    component NumberEditor: RowLayout {
        id: num
        required property var row
        Layout.fillWidth: true
        spacing: 6
        Slider {
            id: slider
            objectName: "slider_" + num.row.id
            Layout.fillWidth: true
            Layout.minimumHeight: 32
            from: num.row.min * num.row.scale
            to: num.row.max * num.row.scale
            stepSize: num.row.type === "integer" ? 1 : 0
            value: num.row.value * num.row.scale
            onMoved: page.studio.setValue(num.row.id, value / num.row.scale)
        }
        TextField {
            id: field
            objectName: "field_" + num.row.id
            Layout.preferredWidth: 72
            horizontalAlignment: TextInput.AlignRight
            inputMethodHints: Qt.ImhFormattedNumbersOnly
            text: page.shown(num.row, num.row.value)
            // Not DoubleValidator: its locale fix-up turns "40.0" into "400" where "." groups digits.
            validator: RegularExpressionValidator { regularExpression: /-?[0-9]*([.,][0-9]*)?/ }
            onEditingFinished: {
                const v = parseFloat(text.replace(",", "."))
                if (!isNaN(v))
                    page.studio.setValue(num.row.id, v / num.row.scale)
                text = Qt.binding(() => page.shown(num.row, num.row.value))
            }
        }
        Label {
            text: num.row.unit
            Layout.preferredWidth: 46
            elide: Text.ElideRight
        }
    }

    component PressureEditor: ColumnLayout {
        id: pr
        required property var row
        Layout.fillWidth: true
        Switch {
            objectName: "switch_" + pr.row.id
            text: "Use pen pressure"
            checked: pr.row.value.enabled === true
            onToggled: page.studio.setValue(pr.row.id + ".enabled", checked)
        }
        RowLayout {
            visible: pr.row.value.enabled === true
            Layout.fillWidth: true
            Label { text: "At lightest"; Layout.preferredWidth: 80 }
            Slider {
                Layout.fillWidth: true
                from: 0; to: 100
                value: pr.row.value.minimum * 100
                onMoved: page.studio.setValue(pr.row.id + ".minimum", value / 100)
            }
            Label { text: Math.round(pr.row.value.minimum * 100) + " %"; Layout.preferredWidth: 46 }
        }
        RowLayout {
            visible: pr.row.value.enabled === true
            Layout.fillWidth: true
            Label { text: "Firmness"; Layout.preferredWidth: 80 }
            Slider {
                Layout.fillWidth: true
                from: -100; to: 100
                value: Math.log(pr.row.value.gamma) / Math.log(3) * 100
                onMoved: page.studio.setValue(pr.row.id + ".gamma", Math.pow(3, value / 100))
            }
            Label {
                Layout.preferredWidth: 46
                text: pr.row.value.gamma < 0.97 ? "Soft" : (pr.row.value.gamma > 1.03 ? "Firm" : "Linear")
            }
        }
    }

    component DynamicsEditor: ColumnLayout {
        id: dyn
        required property var row
        readonly property var d: row.value
        Layout.fillWidth: true
        spacing: 6
        Switch {
            objectName: "switch_" + dyn.row.id
            text: "Follow sensors"
            checked: dyn.d.enabled === true
            onToggled: page.studio.setDynamicsField(dyn.row.id, "enabled", checked)
        }
        RowLayout {
            visible: dyn.d.enabled === true
            Layout.fillWidth: true
            Label { text: "Minimum"; Layout.preferredWidth: 80 }
            Slider {
                objectName: "minimum_" + dyn.row.id
                Layout.fillWidth: true
                from: 0; to: 100
                value: dyn.d.minimum * 100
                onMoved: page.studio.setDynamicsField(dyn.row.id, "minimum", value / 100)
            }
            Label { text: Math.round(dyn.d.minimum * 100) + " %"; Layout.preferredWidth: 46 }
        }
        RowLayout {
            visible: dyn.d.enabled === true && dyn.d.sensors.length > 1
            Layout.fillWidth: true
            Label { text: "Combine"; Layout.preferredWidth: 80 }
            ComboBox {
                Layout.fillWidth: true
                readonly property var ids: ["multiply", "add", "max", "min", "difference"]
                model: ["Multiply", "Add", "Highest", "Lowest", "Difference"]
                currentIndex: ids.indexOf(dyn.d.combine)
                onActivated: index => page.studio.setDynamicsField(dyn.row.id, "combine", ids[index])
            }
        }
        Repeater {
            model: dyn.d.enabled === true ? dyn.d.sensors : []
            delegate: Frame {
                id: mapping
                required property var modelData
                required property int index
                Layout.fillWidth: true
                padding: 6
                ColumnLayout {
                    anchors.fill: parent
                    spacing: 4
                    RowLayout {
                        Layout.fillWidth: true
                        ComboBox {
                            objectName: "sensor_" + dyn.row.id + "_" + mapping.index
                            Layout.fillWidth: true
                            model: page.studio.sensorLabels
                            currentIndex: page.studio.sensorIds.indexOf(mapping.modelData.sensor)
                            onActivated: index => page.studio.setSensor(dyn.row.id, mapping.index, page.studio.sensorIds[index])
                        }
                        ToolButton {
                            text: "Remove" // plain text: Android's default font has no "✕"
                            ToolTip.visible: hovered
                            ToolTip.text: "Remove this sensor"
                            onClicked: page.studio.removeSensor(dyn.row.id, mapping.index)
                        }
                    }
                    Explanation { text: page.studio.sensorDescription(mapping.modelData.sensor) }
                    RowLayout {
                        visible: page.studio.sensorUsesRange(mapping.modelData.sensor)
                        Layout.fillWidth: true
                        Label { text: "Range" }
                        TextField {
                            Layout.preferredWidth: 80
                            horizontalAlignment: TextInput.AlignRight
                            inputMethodHints: Qt.ImhFormattedNumbersOnly
                            text: mapping.modelData.range !== undefined ? String(mapping.modelData.range) : ""
                            validator: RegularExpressionValidator { regularExpression: /[0-9]*([.,][0-9]*)?/ }
                            onEditingFinished: {
                                const v = parseFloat(text.replace(",", "."))
                                if (!isNaN(v))
                                    page.studio.setSensorRange(dyn.row.id, mapping.index, v)
                            }
                        }
                        Label { text: page.studio.sensorRangeUnit(mapping.modelData.sensor) }
                    }
                    CurveEditor {
                        objectName: "curve_" + dyn.row.id + "_" + mapping.index
                        studio: page.studio
                        Layout.fillWidth: true
                        Layout.preferredHeight: 150
                        points: mapping.modelData.curve
                        xLabel: page.studio.sensorLabels[page.studio.sensorIds.indexOf(mapping.modelData.sensor)]
                        yLabel: dyn.row.name.replace(" dynamics", "")
                        onEdited: list => page.studio.setSensorCurve(dyn.row.id, mapping.index, list)
                    }
                    Flow {
                        Layout.fillWidth: true
                        spacing: 4
                        Repeater {
                            model: [["linear", "Linear"], ["inverted", "Inverted"], ["soft", "Soft"], ["firm", "Firm"],
                                    ["threshold", "Threshold"], ["clamped", "Clamped"]]
                            delegate: Button {
                                required property var modelData
                                text: modelData[1]
                                flat: true
                                onClicked: page.studio.setSensorCurve(dyn.row.id, mapping.index,
                                                                      page.studio.curvePreset(modelData[0]))
                            }
                        }
                    }
                }
            }
        }
        Button {
            visible: dyn.d.enabled === true && dyn.d.sensors.length < 4
            objectName: "addSensor_" + dyn.row.id
            text: "Add sensor"
            onClicked: page.studio.addSensor(dyn.row.id, "random")
        }
    }

    // One property, by type.
    component PropertyRow: ColumnLayout {
        id: prop
        required property string modelData
        readonly property var row: { page.studio.revision; return page.studio.propertyRow(modelData) }
        readonly property bool usable: row.implemented === true && row.applies === true
        visible: usable || (!row.implemented ? page.showPlanned : page.showUnused) || page.studio.search !== ""
        Layout.fillWidth: true
        spacing: 2
        objectName: "property_" + modelData

        RowHeader { row: prop.row }
        Explanation {
            text: prop.row.description
            visible: prop.usable
        }
        Explanation {
            visible: prop.row.implemented === true && prop.row.applies !== true
            text: "Not used by this brush's engine (" + page.studio.engine + "). Used by: " + prop.row.engines + "."
            color: "#9a5b00"
        }
        Explanation {
            visible: prop.usable && prop.row.requires !== ""
            text: "Has an effect with: " + prop.row.requires
        }
        Loader {
            Layout.fillWidth: true
            active: prop.usable
            sourceComponent: {
                switch (prop.row.type) {
                case "number": case "integer": return numberC
                case "bool": return boolC
                case "choice": return choiceC
                case "pressure": return pressureC
                case "dynamics": return dynamicsC
                case "image": return imageC
                }
                return null
            }
            Component { id: numberC; NumberEditor { row: prop.row } }
            Component { id: pressureC; PressureEditor { row: prop.row } }
            Component { id: dynamicsC; DynamicsEditor { row: prop.row } }
            Component {
                id: boolC
                Switch {
                    objectName: "switch_" + prop.row.id
                    text: checked ? "On" : "Off"
                    checked: prop.row.value === true
                    onToggled: page.studio.setValue(prop.row.id, checked)
                }
            }
            Component {
                id: choiceC
                ComboBox {
                    objectName: "choice_" + prop.row.id
                    model: prop.row.choiceLabels
                    currentIndex: prop.row.choices.indexOf(prop.row.value)
                    onActivated: index => page.studio.setValue(prop.row.id, prop.row.choices[index])
                }
            }
            Component {
                id: imageC
                RowLayout {
                    Label { text: prop.row.value === true ? "Imported image" : "None"; Layout.fillWidth: true }
                    Button { text: "Import image…"; onClicked: tipDialog.open() }
                    Button { text: "Remove"; enabled: prop.row.value === true; onClicked: page.studio.clearTip() }
                }
            }
        }
        Label {
            visible: prop.row.implemented !== true
            text: "Planned: not available yet."
            font.italic: true
            opacity: 0.6
        }
        MenuSeparator { Layout.fillWidth: true }
    }

    ColumnLayout {
        anchors.fill: parent
        spacing: 6

        // Header: name, engine, actions.
        Flow {
            Layout.fillWidth: true
            spacing: 6
            TextField {
                id: nameField
                objectName: "studioName"
                width: 200
                text: page.studio.name
                readOnly: page.studio.builtIn
                ToolTip.visible: hovered && page.studio.builtIn
                ToolTip.text: "Built-in brushes keep their names; Duplicate makes your own copy."
                onEditingFinished: if (!readOnly) page.studio.rename(text)
            }
            ComboBox {
                objectName: "studioEngine"
                width: 150
                enabled: !page.studio.engineLocked
                readonly property var ids: page.studio.engineLocked ? ["erase"] : ["paint", "smudge", "wet"]
                model: page.studio.engineLocked ? ["Eraser"] : ["Paint", "Smudge", "Wet paint"]
                currentIndex: Math.max(0, ids.indexOf(page.studio.engine))
                onActivated: index => page.studio.setValue("blend", ids[index])
            }
            Button { objectName: "studioResetAll"; text: "Reset all"; enabled: page.studio.modified; onClicked: page.studio.resetAll() }
            Button { objectName: "studioDuplicate"; text: "Duplicate"; onClicked: page.studio.duplicate("") }
            Button { text: "Import…"; onClicked: importDialog.open() }
            Button { text: "Export…"; onClicked: exportDialog.open() }
            Button { objectName: "studioCancel"; text: "Cancel"; onClicked: page.finish(false) }
            Button { objectName: "studioDone"; text: "Done"; highlighted: true; onClicked: page.finish(true) }
        }
        Label {
            visible: page.studio.status !== ""
            text: page.studio.status
            Layout.fillWidth: true
            wrapMode: Text.Wrap
            color: "#2f6fdf"
        }

        RowLayout {
            Layout.fillWidth: true
            Layout.fillHeight: true
            spacing: 8

            // Categories (wide) / category menu (narrow).
            ListView {
                id: categories
                objectName: "studioCategories"
                visible: page.wide
                Layout.preferredWidth: 170
                Layout.fillHeight: true
                clip: true
                model: page.studio.groups
                delegate: ItemDelegate {
                    required property string modelData
                    objectName: "category_" + modelData
                    width: ListView.view.width
                    text: modelData
                    highlighted: modelData === page.studio.group && page.studio.search === ""
                    onClicked: {
                        page.studio.search = ""
                        page.studio.group = modelData
                    }
                }
            }

            ColumnLayout {
                Layout.fillWidth: true
                Layout.fillHeight: true
                spacing: 4
                RowLayout {
                    Layout.fillWidth: true
                    ComboBox {
                        visible: !page.wide
                        Layout.preferredWidth: 170
                        model: page.studio.groups
                        currentIndex: page.studio.groups.indexOf(page.studio.group)
                        onActivated: index => {
                            page.studio.search = ""
                            page.studio.group = page.studio.groups[index]
                        }
                    }
                    TextField {
                        objectName: "studioSearch"
                        Layout.fillWidth: true
                        placeholderText: "Search settings"
                        text: page.studio.search
                        onTextEdited: page.studio.search = text
                    }
                }
                Flow {
                    Layout.fillWidth: true
                    spacing: 8
                    CheckBox { text: "Show settings this engine does not use"; checked: page.showUnused; onToggled: page.showUnused = checked }
                    CheckBox { text: "Show planned settings"; checked: page.showPlanned; onToggled: page.showPlanned = checked }
                }
                ScrollView {
                    id: scroll
                    objectName: "studioScroll"
                    Layout.fillWidth: true
                    Layout.fillHeight: true
                    contentWidth: availableWidth
                    clip: true
                    ColumnLayout {
                        width: scroll.availableWidth
                        spacing: 6
                        Repeater {
                            model: page.studio.propertyIds
                            delegate: PropertyRow {}
                        }
                        Label {
                            Layout.fillWidth: true
                            wrapMode: Text.Wrap
                            font.pixelSize: 11
                            opacity: 0.7
                            text: page.studio.implementedCount + " of " + page.studio.designCount
                                  + " planned brush settings are available; the rest are listed as planned."
                        }
                    }
                }
            }

            // Preview and try-out pad (below the list on narrow screens).
            ColumnLayout {
                visible: page.wide
                Layout.preferredWidth: 300
                Layout.fillHeight: true
                spacing: 4
                Label { text: "Preview"; font.bold: true }
                Image {
                    objectName: "studioPreview"
                    Layout.fillWidth: true
                    Layout.preferredHeight: 110
                    source: page.studio.active ? page.studio.preview : ""
                    sourceSize: Qt.size(600, 220)
                    fillMode: Image.PreserveAspectFit
                    cache: false
                    asynchronous: true
                }
                Explanation { text: "Simulated pressure, light → firm → light. Real pens may differ." }
                Label { text: "Try it"; font.bold: true }
                BrushScratchpad {
                    id: pad
                    objectName: "studioPad"
                    Layout.fillWidth: true
                    Layout.fillHeight: true
                    Layout.minimumHeight: 140
                    canvas: page.canvas
                    studio: page.studio
                }
                RowLayout {
                    Explanation { text: "Strokes here are not part of your drawing." }
                    Button { text: "Clear"; onClicked: pad.clear() }
                }
            }
        }

        // Narrow screens: preview and pad at the bottom, compact.
        RowLayout {
            visible: !page.wide
            Layout.fillWidth: true
            Layout.preferredHeight: 110
            Image {
                Layout.fillWidth: true
                Layout.fillHeight: true
                source: page.studio.active ? page.studio.preview : ""
                sourceSize: Qt.size(400, 160)
                fillMode: Image.PreserveAspectFit
                cache: false
                asynchronous: true
            }
            BrushScratchpad {
                id: smallPad
                Layout.fillWidth: true
                Layout.fillHeight: true
                canvas: page.canvas
                studio: page.studio
            }
        }
    }

    FileDialog {
        id: tipDialog
        title: "Import a tip image"
        fileMode: FileDialog.OpenFile
        nameFilters: ["Images (*.png *.jpg *.jpeg *.bmp *.webp)", "All files (*)"]
        onAccepted: page.studio.importTip(selectedFile)
    }
    FileDialog {
        id: importDialog
        title: "Import a brush"
        fileMode: FileDialog.OpenFile
        nameFilters: ["DrawApp brush (*.drawbrush)", "All files (*)"]
        onAccepted: page.studio.importFrom(selectedFile)
    }
    FileDialog {
        id: exportDialog
        title: "Export brush"
        fileMode: FileDialog.SaveFile
        defaultSuffix: "drawbrush"
        nameFilters: ["DrawApp brush (*.drawbrush)"]
        onAccepted: page.studio.exportTo(selectedFile)
    }
}
