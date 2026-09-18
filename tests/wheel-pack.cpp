// The scroll wheel crossing HP's IPC without changing HP's IPC.
//
// adapters/input-compat carries a wheel in the range Event::Type reserves for
// events HP did not define (User = 0xFF000000), reusing fields of SysMgrEvent's
// union that a scroll never fills. Two things have to hold for that to be safe,
// and neither is obvious by reading:
//
//   * it round-trips -- what the shell packs is what WebAppMgr unpacks, since
//     the field map lives in one header and nothing else may touch those fields;
//   * it is invisible to HP's code -- isPenEvent, isKeyEvent and isGestureEvent
//     are bit tests, and if the chosen value tripped any of them the event would
//     be picked up by a switch meant for something else and mishandled in a way
//     no compiler would catch.
//
// Runs headless, and needs no Qt at all:  ./wheel-pack
#include <SysMgrEvent.h>
#include <webos_wheel.h>

#include <cstdio>
#include <cstring>

int main()
{
    bool ok = true;

    // What the shell measures from a QWheelEvent: a notch down, on a trackpad
    // that also reports pixels, over a point inside the card.
    WebosWheel::Scroll sent;
    sent.x = 317;
    sent.y = 64;
    sent.angleX = 0;
    sent.angleY = -120;
    sent.pixelX = 0;
    sent.pixelY = -53;

    SysMgrEvent e;
    std::memset(&e, 0, sizeof(e));
    WebosWheel::pack(e, sent, 4242);

    const WebosWheel::Scroll got = WebosWheel::unpack(e);

    const bool roundTrip = got.x == sent.x && got.y == sent.y
                        && got.angleX == sent.angleX && got.angleY == sent.angleY
                        && got.pixelX == sent.pixelX && got.pixelY == sent.pixelY;
    printf("round trip   -> x:%d y:%d angle:(%d,%d) pixel:(%d,%d)  time:%u\n",
           got.x, got.y, got.angleX, got.angleY, got.pixelX, got.pixelY, e.time);
    if (!roundTrip) {
        printf("FAIL: the field map does not survive pack/unpack\n");
        ok = false;
    }
    if (e.time != 4242u) {
        printf("FAIL: the timestamp was not carried\n");
        ok = false;
    }

    if (!WebosWheel::isScroll(e)) {
        printf("FAIL: a packed scroll is not recognised as one\n");
        ok = false;
    }

    // The part that keeps HP's code from ever seeing this as input it knows.
    const unsigned int type = (unsigned int) e.type;
    const bool pen     = (type & SysMgrEvent::PenMask) != 0;
    const bool key     = (type & SysMgrEvent::KeyMask) != 0;
    const bool gesture = (type & SysMgrEvent::GestureMask) != 0;
    const bool other   = (type & SysMgrEvent::OtherMask) != 0;
    printf("masks        -> type:0x%08X pen:%d key:%d gesture:%d other:%d\n",
           type, pen, key, gesture, other);
    if (pen || key || gesture || other) {
        printf("FAIL: HP's own event tests would claim this event\n");
        ok = false;
    }

    // An ordinary mouse reports no pixelDelta at all; that has to survive as
    // zero rather than as whatever was in the union before.
    WebosWheel::Scroll plain;
    plain.x = 10;
    plain.y = 20;
    plain.angleY = 120;
    SysMgrEvent m;
    std::memset(&m, 0, sizeof(m));
    WebosWheel::pack(m, plain, 1);
    const WebosWheel::Scroll back = WebosWheel::unpack(m);
    if (back.pixelX != 0 || back.pixelY != 0 || back.angleY != 120) {
        printf("FAIL: a plain mouse wheel does not survive the trip\n");
        ok = false;
    }

    printf("%s\n", ok ? "OK: the scroll crosses HP's IPC intact and unseen"
                      : "FAIL: see above");
    return ok ? 0 : 1;
}
