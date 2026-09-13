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

#ifndef WEBOS_HOVER_H
#define WEBOS_HOVER_H

#include <SysMgrEvent.h>

//
// A pointer moving with no button held -- a hover -- carried across HP's IPC
// without changing it.
//
// webOS had fingers and no pointer, so there is nothing in its catalogue for
// this. Event::PenMove exists but only ever describes a finger already down:
// the shell builds it from touch, and Qt only synthesises touch while a button
// is held, so a bare move produces no touch, no pen event, and nothing that
// crosses the IPC at all. MEASURED in the running browser with counters armed
// on the page: 0 mousemove and 0 pointermove against 148 mouseover, those last
// ones coming from content sliding under a pointer that never moved.
//
// What it costs: any web content that reveals itself on hover stays hidden.
// YouTube's player controls are the visible case -- they appear on mousemove.
//
// Same reasoning as webos_wheel.h for why this is safe: kType is outside every
// mask HP tests (PenMask, KeyMask, GestureMask), so isPenEvent() and its
// siblings answer no, every switch falls through to its default, and the
// struct does not grow.
//
namespace WebosHover {

// Event::User + 4. User + 1 and + 2 are spent in WebAppManager.cpp as Qt event
// ids, and + 3 is the scroll in webos_wheel.h.
const SysMgrEvent::Type kType = (SysMgrEvent::Type) (SysMgrEvent::User + 4);

// Where the pointer is, in card coordinates. Nothing else: a hover has no
// button, no delta and no count.
struct Hover {
    int x = 0;
    int y = 0;
};

inline void pack(SysMgrEvent& e, const Hover& h, unsigned int timeMs)
{
    e.type = kType;
    e.x = h.x;
    e.y = h.y;
    e.time = timeMs;
}

inline bool isHover(const SysMgrEvent& e)
{
    return e.type == kType;
}

inline Hover unpack(const SysMgrEvent& e)
{
    Hover h;
    h.x = e.x;
    h.y = e.y;
    return h;
}

} // namespace WebosHover

#endif /* WEBOS_HOVER_H */
