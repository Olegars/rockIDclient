pragma Singleton
import QtQuick

QtObject {
    property color window: "#0e1621"
    property color sidebar: "#17212b"
    property color chat: "#0e1621"
    property color header: "#17212b"
    property color bubbleOut: "#2b5278"
    property color bubbleIn: "#182533"
    property color accent: "#2AABEE"
    property color accentDark: "#1f8ec4"
    property color text: "#f5f5f5"
    property color secondary: "#7f91a4"
    property color hover: "#202b36"
    property color selected: "#2b5278"
    property color line: "#0d141c"
    property color input: "#242f3d"
    property color danger: "#e55b5b"
    property color online: "#0ac630"
    property color chip: "#182533"
    readonly property string fontFamily: "Segoe UI"
    readonly property string iconFont: "Segoe MDL2 Assets"

    function initials(name) {
        var clean = String(name || "").trim()
        if (!clean)
            return "?"
        var parts = clean.split(/\s+/)
        if (parts.length < 2)
            return parts[0].substring(0, 1).toUpperCase()
        return (parts[0].substring(0, 1) + parts[1].substring(0, 1)).toUpperCase()
    }

    function avatarColor(seed) {
        var colors = ["#e17076", "#7bc862", "#e5ca77", "#65aadd", "#a695e7", "#ee7aae", "#6ec9cb", "#faa774"]
        var n = Math.abs(Number(seed) || 0) % colors.length
        return colors[n]
    }
}
