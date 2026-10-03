import QtQuick 2.0
import SystemMenu 1.0

// Where webOS's audio goes -- a drawer that lists the outputs by their real
// names and sends webOS's sound to the one tapped. New UI HP never had, modelled
// on WiFiElement: a Drawer with a header carrying the current output's name and
// a body whose ListView is filled from C++ (SystemMenu.cpp) off com.palm.audio.
//
// Simpler than Wi-Fi: no radio to toggle, no security, no spinner states. One
// list, one entry marked current, and selecting another asks the service to
// move the stream. The move itself is the half still pending the
// PipeWire-vs-WirePlumber measurement (see services/audiod-pipewire); until it
// lands, the service answers the selection with "not yet" and the current
// output does not change, which this drawer reflects by simply not re-marking.

Drawer {
    id: audioOutputMenu
    property int ident:         0
    property int internalIdent: 0

    // ------------------------------------------------------------
    // External interface to the Audio Output Element is defined here:

    signal menuCloseRequest(int delayMs)
    signal menuOpened()
    signal menuClosed()
    signal itemSelected(int index, string outputId, string name)

    // Pushed from C++: the human label of the output webOS is currently using,
    // shown on the right of the header like Wi-Fi's state text.
    function setCurrentOutput(name) {
        outputTitleState.text = name;
    }

    function addAudioOutputEntry(outputId, name, isCurrent) {
        outputList.append({"outputId": outputId,
                           "outputName": name,
                           "isCurrent": isCurrent,
                           "listIndex": outputList.count
                          });
        outputListView.height = separator.height * outputList.count
                              + outputListView.rowHeight * outputList.count;
    }

    function clearAudioOutputList() {
        outputList.clear();
        outputListView.height = 1;
    }

    // ------------------------------------------------------------

    width: parent.width

    onDrawerOpened: menuOpened()
    onDrawerClosed: menuClosed()

    onDrawerFinishedClosingAnimation: {
        clearAudioOutputList();
    }

    drawerHeader:
    MenuListEntry {
        selectable: audioOutputMenu.active
        content: Item {
                    width: parent.width;

                    Text {
                        id: outputTitle
                        x: ident;
                        anchors.verticalCenter: parent.verticalCenter
                        text: runtime.getLocalizedString("Sound Output");
                        color: audioOutputMenu.active ? "#FFF" : "#AAA";
                        font.bold: false;
                        font.pixelSize: 18
                        font.family: "Prelude"
                    }

                    Text {
                        id: outputTitleState
                        x: audioOutputMenu.width - width - 14;
                        anchors.verticalCenter: parent.verticalCenter
                        text: "";
                        width: audioOutputMenu.width - outputTitle.width - 60
                        horizontalAlignment: Text.AlignRight
                        elide: Text.ElideRight;
                        color: "#AAA";
                        font.pixelSize: 13
                        font.family: "Prelude"
                        font.capitalization: Font.AllUppercase
                    }
                }
    }

    drawerBody:
    Column {
        spacing: 0
        width: parent.width

        MenuDivider { id: separator }

        ListView {
            id: outputListView
            property int rowHeight: 42
            width: parent.width
            interactive: false
            spacing: 0
            height: 1
            model: outputList
            delegate: outputListDelegate
        }
    }

    Component {
        id: outputListDelegate
        Column {
            spacing: 0
            width: parent.width
            property int index: listIndex

            MenuListEntry {
                id: entry
                selectable: true
                forceSelected: isCurrent

                content: Item {
                    width: parent.width

                    Text {
                        id: outputNameText
                        x: ident + internalIdent;
                        anchors.verticalCenter: parent.verticalCenter
                        width: audioOutputMenu.width - x - 28
                        elide: Text.ElideRight;
                        text: outputName;
                        color: "#FFF";
                        font.bold: isCurrent;
                        font.pixelSize: 18
                        font.family: "Prelude"
                    }

                    // A check on the output webOS is on now, like the menu's
                    // own selected-item checkmark.
                    Image {
                        visible: isCurrent
                        x: parent.width - width - 14
                        anchors.verticalCenter: parent.verticalCenter
                        source: "/usr/palm/sysmgr/images/statusBar/system-menu-popup-item-checkmark.png"
                    }
                }

                onAction: {
                    itemSelected(index, outputId, outputName);
                    menuCloseRequest(300);
                }
            }

            MenuDivider {}
        }
    }

    ListModel {
        id: outputList
    }
}
