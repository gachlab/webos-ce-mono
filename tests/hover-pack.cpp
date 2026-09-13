// The hover crossing HP's IPC without changing HP's IPC.
//
// Same contract as tests/wheel-pack, for the other event
// components/input-compat carries: it has to round-trip, and it has to be
// invisible to every test HP's own code makes on an event's type.
//
// Runs headless, and needs no Qt:  ./hover-pack
#include <SysMgrEvent.h>
#include <webos_hover.h>
#include <webos_wheel.h>

#include <cstdio>
#include <cstring>

int main()
{
    bool ok = true;

    WebosHover::Hover sent;
    sent.x = 389;
    sent.y = 300;

    SysMgrEvent e;
    std::memset(&e, 0, sizeof(e));
    WebosHover::pack(e, sent, 77);

    const WebosHover::Hover got = WebosHover::unpack(e);
    printf("round trip -> x:%d y:%d  time:%u\n", got.x, got.y, e.time);
    if (got.x != sent.x || got.y != sent.y || e.time != 77u) {
        printf("FAIL: the hover does not survive pack/unpack\n");
        ok = false;
    }

    if (!WebosHover::isHover(e)) {
        printf("FAIL: a packed hover is not recognised as one\n");
        ok = false;
    }

    // The two events this component carries must never be mistaken for each
    // other: both ride in the User range and both are read off the same union.
    if (WebosWheel::isScroll(e)) {
        printf("FAIL: a hover reads as a scroll\n");
        ok = false;
    }
    SysMgrEvent s;
    std::memset(&s, 0, sizeof(s));
    WebosWheel::Scroll scroll;
    scroll.angleY = 120;
    WebosWheel::pack(s, scroll, 1);
    if (WebosHover::isHover(s)) {
        printf("FAIL: a scroll reads as a hover\n");
        ok = false;
    }

    const unsigned int type = (unsigned int) e.type;
    const bool pen     = (type & SysMgrEvent::PenMask) != 0;
    const bool key     = (type & SysMgrEvent::KeyMask) != 0;
    const bool gesture = (type & SysMgrEvent::GestureMask) != 0;
    const bool other   = (type & SysMgrEvent::OtherMask) != 0;
    printf("masks      -> type:0x%08X pen:%d key:%d gesture:%d other:%d\n",
           type, pen, key, gesture, other);
    if (pen || key || gesture || other) {
        printf("FAIL: HP's own event tests would claim this event\n");
        ok = false;
    }

    printf("%s\n", ok ? "OK: the hover crosses HP's IPC intact and unseen"
                      : "FAIL: see above");
    return ok ? 0 : 1;
}
