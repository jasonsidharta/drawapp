import QtQuick
import QtQuick.Controls

// A response curve editor: drag a point to move it, tap empty space to add one, double-tap (or
// long-press, for touch) a point to remove it. The curve is drawn from samples the engine itself
// computes (studio.sampleCurve), so what is shown is exactly what the brush uses.
Item {
    id: editor

    required property var studio
    property var points: [[0, 0], [1, 1]] // [[x, y], ...], x ascending
    property string xLabel: "Sensor"
    property string yLabel: "Value"
    // Emitted with the new points after every change.
    signal edited(var points)

    implicitWidth: 200
    implicitHeight: 160

    readonly property real pad: 14
    readonly property real plotW: width - 2 * pad
    readonly property real plotH: height - 2 * pad
    property int dragIndex: -1

    function toItem(p) { return Qt.point(pad + p[0] * plotW, pad + (1 - p[1]) * plotH) }
    function toCurve(x, y) {
        return [Math.max(0, Math.min(1, (x - pad) / plotW)), Math.max(0, Math.min(1, 1 - (y - pad) / plotH))]
    }
    function hit(x, y) {
        const r = 14 // touch-friendly radius
        for (let i = 0; i < points.length; ++i) {
            const q = toItem(points[i])
            if (Math.abs(q.x - x) <= r && Math.abs(q.y - y) <= r)
                return i
        }
        return -1
    }
    function sorted(list) { return list.slice().sort((a, b) => a[0] - b[0]) }
    function commit(list) {
        points = sorted(list)
        canvas.requestPaint()
        edited(points)
    }

    onPointsChanged: canvas.requestPaint()

    Rectangle {
        anchors.fill: parent
        color: "#fbfbfb"
        border.color: "#c8c8c8"
        radius: 4
    }

    Canvas {
        id: canvas
        anchors.fill: parent
        onPaint: {
            const ctx = getContext("2d")
            ctx.reset()
            ctx.strokeStyle = "#e4e4e4"
            ctx.lineWidth = 1
            for (let i = 0; i <= 4; ++i) {
                const gx = editor.pad + editor.plotW * i / 4, gy = editor.pad + editor.plotH * i / 4
                ctx.beginPath(); ctx.moveTo(gx, editor.pad); ctx.lineTo(gx, editor.pad + editor.plotH); ctx.stroke()
                ctx.beginPath(); ctx.moveTo(editor.pad, gy); ctx.lineTo(editor.pad + editor.plotW, gy); ctx.stroke()
            }
            const samples = editor.studio ? editor.studio.sampleCurve(editor.points, 96) : editor.points
            ctx.strokeStyle = "#2f6fdf"
            ctx.lineWidth = 2
            ctx.beginPath()
            for (let i = 0; i < samples.length; ++i) {
                const q = editor.toItem(samples[i])
                if (i === 0) ctx.moveTo(q.x, q.y); else ctx.lineTo(q.x, q.y)
            }
            ctx.stroke()
            for (let i = 0; i < editor.points.length; ++i) {
                const q = editor.toItem(editor.points[i])
                ctx.fillStyle = i === editor.dragIndex ? "#2f6fdf" : "#ffffff"
                ctx.strokeStyle = "#2f6fdf"
                ctx.beginPath(); ctx.arc(q.x, q.y, 5, 0, 2 * Math.PI); ctx.fill(); ctx.stroke()
            }
        }
    }

    Label {
        text: editor.xLabel + " →"
        font.pixelSize: 10
        opacity: 0.6
        anchors.right: parent.right
        anchors.bottom: parent.bottom
        anchors.margins: 1
    }
    Label {
        text: "↑ " + editor.yLabel
        font.pixelSize: 10
        opacity: 0.6
        anchors.left: parent.left
        anchors.top: parent.top
        anchors.margins: 1
    }

    MouseArea {
        id: area
        anchors.fill: parent
        preventStealing: true // dragging a point must not scroll the studio
        pressAndHoldInterval: 600
        onPressed: mouse => {
            editor.dragIndex = editor.hit(mouse.x, mouse.y)
            if (editor.dragIndex < 0 && editor.points.length < 16) {
                const list = editor.points.slice()
                list.push(editor.toCurve(mouse.x, mouse.y))
                editor.points = editor.sorted(list)
                editor.dragIndex = editor.hit(mouse.x, mouse.y)
            }
            canvas.requestPaint()
        }
        onPositionChanged: mouse => {
            if (editor.dragIndex < 0)
                return
            const list = editor.points.slice()
            const p = editor.toCurve(mouse.x, mouse.y)
            // A point stays between its neighbours, so the order (and the index) never changes.
            const lo = editor.dragIndex > 0 ? list[editor.dragIndex - 1][0] + 0.01 : 0
            const hi = editor.dragIndex < list.length - 1 ? list[editor.dragIndex + 1][0] - 0.01 : 1
            list[editor.dragIndex] = [Math.max(lo, Math.min(hi, p[0])), p[1]]
            editor.points = list
            canvas.requestPaint()
        }
        onReleased: {
            if (editor.dragIndex >= 0)
                editor.commit(editor.points)
            editor.dragIndex = -1
            canvas.requestPaint()
        }
        function removeAt(x, y) {
            const i = editor.hit(x, y)
            if (i >= 0 && editor.points.length > 2) {
                const list = editor.points.slice()
                list.splice(i, 1)
                editor.dragIndex = -1
                editor.commit(list)
            }
        }
        onDoubleClicked: mouse => removeAt(mouse.x, mouse.y)
        onPressAndHold: mouse => removeAt(mouse.x, mouse.y)
    }
}
