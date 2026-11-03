import QtQuick
import QtQuick.Controls as Controls
import QtQuick.Layouts
import org.kde.kirigami as Kirigami
import org.kde.plasma.workspace.dbus as DBus

ColumnLayout {
    id: root
    spacing: Kirigami.Units.smallSpacing
    property bool connected: false
    property bool busy: false
    property int level: 0
    property int effect: 255
    property string errorText: ""
    readonly property var effects: [
        {label: "Off", code: 255},
        {label: "Static", code: 3},
        {label: "Spectrum", code: 1},
        {label: "Staircase", code: 2},
        {label: "Breathing", code: 4},
        {label: "Hundred flowers", code: 5},
        {label: "Wave", code: 6},
        {label: "Up/down wave", code: 7},
        {label: "Fountain", code: 8},
        {label: "Galaxy", code: 9},
        {label: "Vortex", code: 10},
        {label: "Tide", code: 11},
        {label: "Sea wave", code: 12},
        {label: "Ripple", code: 13},
        {label: "Reactive ripple", code: 14},
        {label: "Single point", code: 15},
        {label: "Grid", code: 16},
        {label: "Piano", code: 17},
        {label: "Flowing light", code: 18},
        {label: "Falling rain", code: 19},
        {label: "Starry", code: 20},
        {label: "Fireworks", code: 21},
        {label: "Wave bar", code: 22},
        {label: "Custom (existing colors)", code: 0}
    ]

    function call(member, args, resolve, reject) {
        DBus.SystemBus.asyncCall({
            service: "local.NuPhy.Backlight",
            path: "/local/NuPhy/Backlight",
            iface: "local.NuPhy.Backlight",
            member: member,
            signature: args.length ? "(u)" : "()",
            arguments: args
        }, resolve, reject);
    }

    function refresh() {
        if (busy || slider.pressed || selector.popup.visible) return;
        busy = true;
        call("GetState", [], function(reply) {
            const state = reply.values;
            connected = unpack(state[0]);
            level = unpack(state[1]);
            effect = unpack(state[2]);
            errorText = connected ? "" : "Connect your Air75 HE by USB.";
            busy = false;
        }, function(reply) {
            connected = false;
            errorText = "Lighting service unavailable. Install or restart the updated service.";
            busy = false;
        });
    }

    function unpack(value) {
        return value !== null && typeof value === "object" && value.value !== undefined ? value.value : value;
    }

    function change(member, value) {
        if (busy) return;
        busy = true;
        call(member, [value], function(reply) {
            busy = false;
            refresh();
        }, function(reply) {
            errorText = reply.error.message;
            busy = false;
        });
    }

    Component.onCompleted: refresh()
    Timer {
        interval: 2000
        running: root.visible
        repeat: true
        onTriggered: root.refresh()
    }

    Kirigami.Heading {
        text: "NuPhy Air75 HE"
        level: 3
        Layout.margins: Kirigami.Units.smallSpacing
    }
    RowLayout {
        Layout.fillWidth: true
        Layout.margins: Kirigami.Units.smallSpacing
        Controls.Label { text: "Lighting effect" }
        Controls.ComboBox {
            id: selector
            Layout.fillWidth: true
            model: root.effects
            textRole: "label"
            valueRole: "code"
            currentIndex: root.effects.findIndex(item => item.code === root.effect)
            enabled: root.connected && !root.busy
            onActivated: root.change("SetEffect", currentValue)
        }
    }
    RowLayout {
        Layout.fillWidth: true
        Layout.margins: Kirigami.Units.smallSpacing
        Controls.Label { text: "Brightness" }
        Item { Layout.fillWidth: true }
        Controls.Label { text: Math.round(slider.value) + "%" }
    }
    Controls.Slider {
        id: slider
        Layout.fillWidth: true
        Layout.margins: Kirigami.Units.smallSpacing
        from: 0
        to: 100
        stepSize: 1
        value: root.level
        enabled: root.connected && !root.busy
        onPressedChanged: if (!pressed) root.change("SetBrightness", Math.round(value))
        onMoved: if (!pressed) root.change("SetBrightness", Math.round(value))
    }
    Controls.Label {
        Layout.fillWidth: true
        Layout.margins: Kirigami.Units.smallSpacing
        text: root.errorText || "0% switches the keys off. Raising brightness restores the previous effect."
        wrapMode: Text.WordWrap
        opacity: 0.7
    }
}
