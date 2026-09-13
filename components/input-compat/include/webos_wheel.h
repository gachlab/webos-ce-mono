/* @@@LICENSE
 *
 * Copyright (c) 2026 webOS CE modern build
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *     http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 *
 * LICENSE@@@ */

#ifndef WEBOS_WHEEL_H
#define WEBOS_WHEEL_H

#include <SysMgrEvent.h>

//
// A scroll wheel carried across HP's IPC without changing HP's IPC.
//
// webOS had no wheel: it scrolled by gesture, so SysMgrEvent::Type is Key*,
// Pen*, Gesture* and the sensors and has no scroll member. Rather than add one
// -- which means editing the catalogue, the struct, WindowedWebApp's dispatch
// and CardWebApp's orientation mapping, all of them HP's -- this takes a value
// out of the range HP reserved for events he did not define.
//
// Why that is safe, and it is not a matter of taste:
//
//   * The wire does not change. ParamTraits sends SysMgrEvent as raw bytes
//     (writeBytes(&p.type, sizeof(SysMgrEvent))) and nothing here grows the
//     struct, so both sides keep agreeing about its 64 bytes.
//   * HP's code cannot mistake it for anything. Event::isPenEvent, isKeyEvent
//     and isGestureEvent are bit tests against PenMask (1<<17), KeyMask (1<<16)
//     and GestureMask (1<<18); kType is 0xFF000003, which has none of those
//     bits, so every one of those tests answers no and every switch over
//     Event::Type falls through to its default. The event crosses the whole
//     path untouched and is invisible until our own code asks for it.
//   * EventThrottler::shouldDropEvent only inspects PenDown, PenUp, PenMove and
//     GestureChange, so it passes this through.
//
namespace WebosWheel {

// Event::User + 3. The tree already spends User + 1 and User + 2 in
// WebAppManager.cpp -- as Qt event ids rather than SysMgrEvent types, so they
// could not actually collide, but stepping over them costs nothing and saves
// the next person the same five minutes of checking.
const SysMgrEvent::Type kType = (SysMgrEvent::Type) (SysMgrEvent::User + 3);

// One scroll, in the two units a wheel reports.
//
// angleDelta is the one that matters: 120 eighths of a degree to a notch, which
// is also the unit enyo's ScrollStrategy.mousewheel reads out of wheelDeltaY.
// pixelDelta is what a trackpad adds on top, and it is 0 for an ordinary mouse.
// Both are carried because a touchpad that reports only pixels would otherwise
// arrive as no scroll at all -- MEASURED in tests/wheel-in, where a
// pixelDelta-only event coming through QGraphicsSceneWheelEvent arrives as
// delta 0, which is one of the reasons the wheel is picked up before the scene
// gets it.
struct Scroll {
    int x = 0;              // card coordinates
    int y = 0;
    int angleX = 0;
    int angleY = 0;
    int pixelX = 0;
    int pixelY = 0;
};

// Fills an Event that has already been zeroed (Event's constructor does that).
// The field map lives here and nowhere else; see the table in the README.
inline void pack(SysMgrEvent& e, const Scroll& s, unsigned int timeMs)
{
    e.type = kType;
    e.x = s.x;
    e.y = s.y;
    e.flickXVel = s.angleX;
    e.flickYVel = s.angleY;
    e.z = s.pixelX;
    e.clickCount = s.pixelY;
    e.time = timeMs;
}

inline bool isScroll(const SysMgrEvent& e)
{
    return e.type == kType;
}

inline Scroll unpack(const SysMgrEvent& e)
{
    Scroll s;
    s.x = e.x;
    s.y = e.y;
    s.angleX = e.flickXVel;
    s.angleY = e.flickYVel;
    s.pixelX = e.z;
    s.pixelY = e.clickCount;
    return s;
}

} // namespace WebosWheel

#endif /* WEBOS_WHEEL_H */
