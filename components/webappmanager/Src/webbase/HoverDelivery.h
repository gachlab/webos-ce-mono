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

#ifndef HOVERDELIVERY_H
#define HOVERDELIVERY_H

struct SysMgrEvent;
class SysMgrWebBridge;

//
// The receiving end of the hover the shell packs in components/input-compat.
//
namespace HoverDelivery {

// Hands the page a buttonless QMouseEvent(MouseMove), which is what a hover is
// in web terms. QWebPage::event already forwards MouseMove to the engine, and
// deliverToEmbedded routes it by position -- and correctly ignores its dragging
// branch, which only applies while a button is down -- so the browser's content
// gets the hover rather than its chrome.
void deliver(SysMgrWebBridge* bridge, const SysMgrEvent& event);

} // namespace HoverDelivery

#endif /* HOVERDELIVERY_H */
