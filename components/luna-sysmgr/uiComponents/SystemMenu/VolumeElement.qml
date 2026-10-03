import QtQuick 2.0

// webOS's own volume, as a slider in the system menu -- new UI that HP never
// had: neither the TouchPad's shell nor Open webOS's shipped an audio element
// here. Modelled on BrightnessElement, down to reusing Slider.qml, so a reader
// of one reads the other; the only differences are the icons (a quiet/loud pair
// instead of brightness-less/more) and the signal name.
//
// The slider's value is normalised 0.0..1.0, like brightness; SystemMenu.cpp
// maps it to and from com.palm.audio's 0..100 the same way it does for display
// brightness.

MenuListEntry {
    id: volumeElement

    property alias volumeValue: volumeSlider.setValue
    property bool active: true

    signal volumeChanged(real value, bool save)

    selectable: false

    property int margin: 0
    property int spacing: 5

    content:
        Item {
            id: volumeContent
            x: 4
            width: volumeElement.width - 8
            height: volumeElement.height

            Image {
                id: imgQuiet
                source: "/usr/palm/sysmgr/images/statusBar/volume-quiet.png"
                x: margin
                y: volumeElement.height/2 - height/2
            }

            Image {
                id: imgLoud
                source: "/usr/palm/sysmgr/images/statusBar/volume-loud.png"
                x: volumeContent.width - width - margin
                y: volumeElement.height/2 - height/2
            }

            Slider {
                id: volumeSlider
                width: volumeContent.width - (imgQuiet.width + imgLoud.width + 2 * margin + 2 * spacing)
                x: volumeContent.width/2 - width/2
                y: volumeContent.height/2 - height/2
                active: volumeElement.active

                onValueChanged: {
                    volumeChanged(value, done);
                }

                onSetFlickOverride: {
                    flickOverride(override)
                }
            }
        }
}
