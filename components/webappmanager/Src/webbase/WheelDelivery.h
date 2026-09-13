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

#ifndef WHEELDELIVERY_H
#define WHEELDELIVERY_H

struct SysMgrEvent;
class SysMgrWebBridge;

//
// The receiving end of the scroll the shell packs in components/input-compat.
//
// Kept out of WindowedWebApp so that HP's dispatch needs one line rather than a
// branch: everything about what a scroll means on this side lives here.
//
namespace WheelDelivery {

// Hands the page a real QWheelEvent. Not a synthesized DOM event, because
// QWebPage::event already forwards QEvent::Wheel to the engine view: Chromium
// scrolls the page natively and enyo still receives the "mousewheel" its
// Dispatcher registers and ScrollStrategy.mousewheel reads wheelDeltaY from.
// One event, both halves, and no JavaScript of HP's to keep in step.
//
// It also reaches embedded pages correctly for free: the compatibility layer's
// deliverToEmbedded already routes QEvent::Wheel by position, so a wheel over
// the browser's content scrolls the content rather than its chrome.
void deliver(SysMgrWebBridge* bridge, const SysMgrEvent& event);

} // namespace WheelDelivery

#endif /* WHEELDELIVERY_H */
