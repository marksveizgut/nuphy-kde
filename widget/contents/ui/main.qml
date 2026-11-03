pragma ComponentBehavior: Bound
import QtQuick
import QtQuick.Layouts
import org.kde.plasma.plasmoid
import org.kde.kirigami as Kirigami

PlasmoidItem {
    id: applet
    toolTipMainText: "NuPhy Lighting"
    toolTipSubText: "Keyboard effects and brightness"
    preferredRepresentation: compactRepresentation

    compactRepresentation: Kirigami.Icon {
        source: "input-keyboard"
        active: mouse.containsMouse
        Layout.minimumWidth: Kirigami.Units.iconSizes.small
        Layout.minimumHeight: Kirigami.Units.iconSizes.small
        MouseArea {
            id: mouse
            anchors.fill: parent
            hoverEnabled: true
            onClicked: applet.expanded = !applet.expanded
        }
    }

    fullRepresentation: LightingControls {
        Layout.minimumWidth: Kirigami.Units.gridUnit * 19
        Layout.preferredWidth: Kirigami.Units.gridUnit * 21
        Layout.minimumHeight: implicitHeight
        Layout.preferredHeight: implicitHeight
    }
}
