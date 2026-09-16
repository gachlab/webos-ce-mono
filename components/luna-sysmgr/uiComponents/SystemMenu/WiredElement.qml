import QtQuick 2.0

// The cable's row in the system menu. Ours: webOS had no wired networking to
// show, so there was no element for it.
//
// Modelled on MuteElement rather than WiFiElement -- a row, not a drawer. There
// is no list to open: a socket has one network and either the cable is in or it
// is not. Tapping it connects or disconnects, which MenuListEntry turns into
// action() and SystemMenu.qml forwards as wiredToggleTriggered.
//
// delayUpdate exists for the same reason as MuteElement's: the state shown is
// what NetworkManager reports, and between the tap and NM's answer there is a
// moment where echoing the old value would make the row flicker back.
MenuListEntry {
    id: wiredElement
    property int    ident:              0
    property bool   connected:          false
    property string statusText:         ""
    property bool   delayUpdate:        false
    property string newText:            ""
    property bool   newConnectedStatus: false

    property int iconSpacing:  4
    property int rightMarging: 8

    content:
        Item {
            width: wiredElement.width
            height: 24

            Text {
                id: wiredLabel
                x: ident;
                anchors.verticalCenter: parent.verticalCenter
                text: runtime.getLocalizedString("Wired")
                color: "#FFF";
                font.bold: false;
                font.pixelSize: 18
                font.family: "Prelude"
            }

            // The address when there is one, so the row says something the icon
            // cannot. Dimmer than the label, like the battery row's value.
            Text {
                id: wiredStatus
                x: wiredLabel.x + wiredLabel.width + 8
                anchors.verticalCenter: parent.verticalCenter
                text: statusText
                color: "#AAA";
                font.bold: false;
                font.pixelSize: 18
                font.family: "Prelude"
            }

            Image {
                id: wiredIndicatorOn
                visible: connected
                x: parent.width - width - iconSpacing - rightMarging
                anchors.verticalCenter: parent.verticalCenter
                source: "/usr/palm/sysmgr/images/statusBar/icon-wired.png"
            }

            Image {
                id: wiredIndicatorOff
                visible: !connected
                x: parent.width - width - iconSpacing - rightMarging
                anchors.verticalCenter: parent.verticalCenter
                source: "/usr/palm/sysmgr/images/statusBar/icon-wired-off.png"
            }
        }
}
