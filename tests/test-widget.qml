import QtQuick
import QtQuick.Controls
import "../widget/contents/ui" as NuPhy

ApplicationWindow {
    width: 420
    height: 270
    visible: true
    NuPhy.LightingControls {
        id: controls
        anchors.fill: parent
        property int stage: 0
    }
    Timer {
        interval: 100
        running: true
        repeat: true
        onTriggered: {
            if (!controls.connected || controls.busy) return;
            if (controls.stage === 0) {
                if (controls.level !== 42 || controls.effect !== 3) Qt.exit(1);
                controls.stage = 1;
                controls.change("SetEffect", 6);
            } else if (controls.stage === 1) {
                if (controls.effect !== 6 || controls.level !== 42) Qt.exit(2);
                controls.stage = 2;
                controls.change("SetBrightness", 0);
            } else {
                if (controls.effect !== 255 || controls.level !== 0) Qt.exit(3);
                console.log("Widget D-Bus state, effect selection and off control passed.");
                Qt.quit();
            }
        }
    }
    Timer {
        interval: 10000
        running: true
        onTriggered: {
            console.error("Widget test timed out:", controls.errorText);
            Qt.exit(4);
        }
    }
}
