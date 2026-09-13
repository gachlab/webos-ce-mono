# input-compat

Input that modern hardware produces and webOS's event catalogue never had,
carried across HP's own IPC without changing it.

Same idea as `components/qt6-compat`: not a component of its own, just a header
the components that need it pull in.

    add_subdirectory(${CMAKE_CURRENT_SOURCE_DIR}/../input-compat
                     ${CMAKE_CURRENT_BINARY_DIR}/input-compat)
    target_link_libraries(<target> PRIVATE input-compat)

## What is here

- `include/webos_wheel.h` — a scroll wheel packed into HP's `Event`.

## Why a wheel needs an adapter at all

webOS scrolled by gesture. `Event::Type` is `Key*`, `Pen*`, `Gesture*` and the
sensors, with no scroll member anywhere, and `QEvent::Wheel`, `QWheelEvent` and
`wheelEvent` appear zero times in luna-sysmgr, luna-sysmgr-common and
webappmanager. So a wheel is dropped in the shell before any of HP's code sees
it, and the receiver on the other side has been waiting the whole time: enyo's
`Dispatcher.js` registers `"mousewheel"` and `ScrollStrategy.mousewheel` reads
`wheelDeltaY` out of it.

The obvious fix is to add a member to `Event::Type`, a pair of fields to
`SysMgrEvent`, a branch to `CardWebApp`'s orientation mapping and another to
`WindowedWebApp`'s dispatch. That was written, measured to be safe, and thrown
away: it is 92 lines inside four of HP's files for something that does not need
a single one.

`Event::Type` already reserves `User = 0xFF000000` for events HP did not
define — the tree uses it twice already, in `WebAppManager.cpp`. Taking a value
there costs nothing on the wire: `ParamTraits` sends the struct as raw bytes and
the struct does not change, and because the value carries none of the
`PenMask`/`KeyMask`/`GestureMask` bits, every `isPenEvent()` test in HP's code
answers no and every switch falls through to its default. The event travels the
whole way and is invisible until our own code asks for it.

## The field map, in one place

`SysMgrEvent`'s union has room that a scroll event never fills, and naming the
reuse once here is what keeps it from becoming folklore:

| Field | Carries |
|---|---|
| `x`, `y` | where the pointer was, in card coordinates |
| `flickXVel`, `flickYVel` | `angleDelta`: eighths of a degree, 120 to a notch |
| `z`, `clickCount` | `pixelDelta`: device pixels, both 0 when the source has none |

Reading or writing those fields by hand anywhere else is how the two sides drift
apart, so both ends call `webos_wheel.h` instead.
