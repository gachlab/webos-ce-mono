import QtQuick 2.0

Image {
    property int widthOffset: 7
    width: parent.width - widthOffset
    anchors.horizontalCenter: parent.horizontalCenter
    source: "/usr/palm/sysmgr/images/menu-divider.png"
}
