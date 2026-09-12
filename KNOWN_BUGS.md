# Known bugs

What is broken, what it depends on, and what is already ruled out. Each entry
says how it was diagnosed so nobody has to redo the work.

Verified against the reference where possible: an Ubuntu 12.04 VM running HP's
own `build-webos-desktop.sh`. **If it fails there too, it is not our port.**

---

## The Qt 6 port

The build is Qt 6.10 with QtWebEngine, in `build/`: `tools/build.sh`, then
`tools/run-lunasysmgr.sh`. There is no Qt 5 build any more and nothing compiles
QtWebKit 5.212. The shell draws, WebAppMgr starts its QtWebEngine processes, and
the apps run.

How it was done: build against Qt 6, inventory every error with `make -k`, and
fix each where it can be fixed without touching HP's code. The Qt 5 build stayed
alongside until the apps ran on Qt 6, and was then dropped.

- **WebAppMgr's QtWebKit** (`components/qtwebkit-compat`): the QtWebKit classes it
  uses, over QtWebEngine, without changing WebAppMgr's web code. Pages render
  offscreen and are grabbed; input goes to the view's focus proxy; objects added
  with `addToJavaScriptWindowObject` become JavaScript proxies whose properties
  and methods answer synchronously through a synchronous XHR to a
  `webos-bridge:///` scheme answered in-process, and whose signals arrive
  through `runJavaScript` in every frame of the page.
  `tests/webengine-capabilities` checks each QtWebEngine
  capability this relies on, and `tests/qtwebkit-compat` drives the layer the way
  SysMgrWebBridge does.
- **Adapters** (`components/qt6-compat`, only compiled for Qt 6): the `QGL*`
  classes over `QOpenGL*`, and a forced include that brings back `qrand`,
  `qSort`, `qFind`, `qVariantFromValue` and `qRegisterMetaTypeStreamOperators`
  and includes `<QObject>`, which Qt 5's headers pulled in for HP's.
- **Module moves**, in CMake: `QStateMachine` (Debian: `qt6-scxml-dev`),
  `QOpenGLWidget`, and `GuiPrivate` for `QMutableEventPoint`.
- **Members Qt removed from its own classes** cannot be adapted from outside, so
  about 85 call sites changed, each to a form that compiles on Qt 5.15 too:
  `horizontalAdvance`, `sizeInBytes`, `QMultiMap::insert`, `QChar(int(key))`,
  `drawRoundedRect(..., Qt::RelativeSize)`, and similar.

Traps found on the way, each confirmed before being fixed:

- **`QQuickWindow::graphicsApi()` reports OpenGL after `setGraphicsApi(Software)`.**
  A probe printed 3 from the static call while the window's
  `rendererInterface()` said 1 and rendered the right pixel. Deciding on the
  static value made `QmlSceneItem` call `QQuickRenderControl::initialize()`,
  which with the software adaptation creates an unusable RHI ("QRhi is only
  compatible with default adaptation") and every later `sync()`/`render()`
  refuses. Qt 6 documents that `initialize()` must not be called there.
- **The software renderer repaints only dirty regions.** Clearing the target
  image before each render left it empty on any frame with nothing dirty. Both
  mistakes were put back one at a time; `tests/qml-scene-item` fails on each.
- **A touch sent with `sendEvent()` never reaches QML under Qt 6.** QQuickWindow
  only accepts points its device tracks as active, and a device only tracks
  points that came in through the platform: "point is not in activePoints".
  Real touches in the shell do come through the platform; the test now injects
  through `QTest::touchEvent` on Qt 6.
- **`Window`'s copy constructor was declared and never defined**, with
  `HostWindow` a friend. Qt 6's moc instantiates the copy constructor of every
  QObject subclass that looks copyable, so the link failed; it is `= delete` now.
- **Qt 6's moc needs the complete type behind a pointer property**
  (`Q_PROPERTY(LayoutItem * ...)` in icon.h). CMake passes `-b layoutitem.h` to
  moc instead of editing the header.

- **WebAppMgr segfaulted in `__dynamic_cast` on its first `loadProgress`.** It is
  built with `-fno-rtti` (HP's flag), so `SysMgrWebPage` has no type_info, and in
  a Debug build Qt 6 dynamic_casts the receiver of a signal connected to a
  member function. The layer connects through lambdas. `tests/qtwebkit-compat`
  now subclasses QWebPage and builds with `-fno-rtti` and in Debug, and with the
  member-function connections back it segfaults the same way; the tests had been
  built without a build type, where Qt's checks do not exist, and passed.
- **LunaSysMgrCommon calls sqlite3 without linking it.** QtWebKit used to bring
  libsqlite3 into every executable, so nobody noticed until WebAppMgr stopped
  linking QtWebKit. It is linked by the library now.
- **A custom scheme is reachable from `file://` pages only when flagged
  `LocalScheme` and `SecureScheme` and addressed without a host
  (`webos-bridge:///...`).** Without either the request never reaches the handler.
- **A `QWebEngineScript` runs in the main frame only, unless it is told
  `setRunsOnSubFrames(true)`.** QtWebKit had no such switch: it cleared and
  repopulated every frame's global object, so HP's code assumes a frame is a
  frame. Measured on the mail card: the main frame answered `function` for
  `PalmServiceBridge` while all three of its iframes answered `undefined` for
  that, for `PalmSystem`, and for `__webosBridge` itself, the bridge's own core.
  The email app loads `../accounts/` (`mail/depends.js:43`), so the account
  wizard runs in one of those frames, and adding an account threw
  `PalmServiceBridge is not defined` at `AccountWizard._getTemplateList`. All six
  injected scripts set it now; `tests/subframe-bridge` fails without it.
- **`QWebEnginePage::runJavaScript` reaches the main frame alone**, which left
  the bridge's return path broken in exactly the same shape. The core keeps its
  proxies, and the handlers connected to them, in a map private to each frame,
  so a reply to an object a child frame proxied landed in the main frame's map,
  which had never heard of that id, and `__webosBridge.emit` returned without
  calling anything. With a recorder wrapped around the main frame's `emit`, one
  service call fired from the wizard's iframe logged `[{id: 98, name:
  "response"}, {id: 87, name: "response"}]` in the *main* frame while the
  iframe's own callback never ran and its probe stayed `pending`; the same call
  made from the main frame came back with the template list, so the service and
  the outbound half were both fine. `AccountWizard.protValidators` is only ever
  assigned from that callback, so pressing the button to add an account threw
  `Cannot read properties of undefined (reading 'GOOGLE')` at
  `AccountWizard.js:1047`. `SignalRelay::deliver()` walks the frame tree with
  `QWebEngineFrame` now; ids come from one counter for the whole page, so the
  owner is reached and every other frame's `emit` finds nothing and stops.
  `tests/subframe-signal` fails without it.
- **Nothing scrolls a page by wheel, anywhere, because webOS had no wheel.**
  `Event::Type` (`luna-sysmgr-ipc-messages/.../SysMgrEvent.h`, reached through
  `luna-sysmgr-common/include/Event.h`) is `Key*`, `Pen*`, `Gesture*` and the
  sensors -- there is no scroll or wheel member, and `QEvent::Wheel`,
  `QWheelEvent` and `wheelEvent` appear nowhere in luna-sysmgr or in
  webappmanager. A trackpad's two-finger swipe is therefore dropped before any
  of our code sees it, and the browser's embedded page cannot be scrolled by
  one. Adding it means a new event type carried the whole way: shell, IPC, app.
- **Faking that scroll from the drag does not work, and the numbers are worth
  keeping so nobody pays for them twice.** A drag already reaches an embedded
  page as mouse events, so it was turned into wheel events there instead.
  Delivering to that offscreen widget, QtWebEngine acts on `angleDelta` and
  ignores `pixelDelta`, and the phased form it does not listen for at all:
  `pixelDelta` + `angleDelta` with `NoScrollPhase` moved 80 px of drag by 4 px;
  `pixelDelta` alone with `ScrollUpdate`, 0; a full `ScrollBegin`, updates,
  `ScrollEnd` sequence, 0. Scaling the angle by the 20:1 those first numbers
  implied then overshot to 720 px, so the relationship is not even linear. It
  was all reverted: an unexplained constant that does not extrapolate is not
  worth shipping, and a drag that scrolls also costs text selection, which
  works today.
- **The touch path is plumbed the whole way and amputated at the last metre.**
  The shell synthesises touch from the mouse and sends it
  (`CardWindow::touchEvent` -> `View_TouchEvent`), and WebAppMgr registers the
  handler (`WindowedWebApp.cpp:197`) -- but the body of `onTouchEvent` is
  commented out, lines 691 to 762, because it calls
  `m_page->webkitView()->touchEvent(...)` and `webkitView()` is declared only in
  `luna-sysmgr-ce`, HP's QtWebKit `Palm::WebView`, which this tree does not
  build. So touches arrive over IPC and land in an empty function. Writing that
  last metre against Qt would give Chromium real touch, and with it the native
  kinetic scrolling the wheel cannot provide. It is the principled fix and it is
  not small.
- **Native touch was tried and reverted, and the order of the work matters.**
  The engine side works: a synthetic QTouchEvent handed to QWebPage::event
  reaches the page -- touchstart and touchend fire at the exact coordinates sent
  -- and Chromium's gesture recognizer scrolls from it. MEASURED twice,
  identically: 145px of scroll out of a 160px drag, with the touch points
  carrying real press and last positions. Points whose three positions are all
  the current one scroll nothing, the same trap MouseToTouch documents.

  Three things were measured that make it more than a missing call:

  * The engine does not advertise touch. `navigator.maxTouchPoints` stays 0 and
    `'ontouchstart' in window` is false, so enyo apps, which feature-detect,
    keep taking their mouse path however many touches arrive. Passing Chromium
    `--touch-events=enabled` did not fix that and made things worse -- one run
    reported zero touches delivered, two later runs stopped producing output
    after the first two lines. Not pursued: the no-flag path works.
  * Chromium synthesizes a click from each touch sequence. The shell also sends
    the same tap as pen events, so with both alive every tap counts twice. That
    is what HP's filter in `CardWebApp::onInputEvent` is for.
  * **Switching that filter on kills the browser's drag-to-scroll**, which is
    what ended the attempt. That scroll is built on pen events reaching
    `deliverToEmbedded`'s `m_dragging` path in the compat layer, and the filter
    drops exactly those. Worse, `deliverToEmbedded` handles Mouse\* and Wheel
    and nothing else, so a touch never reaches an embedded page at all: in the
    browser it lands on the host page -- the chrome -- and not on the content.

  So the order is: teach `deliverToEmbedded` to route touch by position the way
  it already routes the mouse, and only then is suppressing the pen events safe.
  Doing it the other way round leaves the browser with no working scroll.

  One more thing found on the way: `needTouchEvents` had zero callers. QtWebKit
  called it when a page registered touch listeners; QtWebEngine gives no such
  signal, so the shell's sender and the `View_TouchEvent` message have been
  alive the whole time waiting for a request nobody ever made.
- **The keyboard button allows the on-screen keyboard, it does not summon it.**
  `KEYS::Key_Keyboard` reaches `SystemUiController` (line 616), which calls
  `IMEController::setIMEActive`. That sets `m_imeAllowed` and then re-evaluates
  input focus; `notifyInputFocusChange` returns at `if (!client || client !=
  m_client)` when nothing holds input focus, so `showIMEInternal()` is never
  reached. On webOS the keyboard appears when a field takes focus, and the key
  only decides whether it is permitted to. A button that summons one regardless
  would need to drive `showIMEInternal()` or give something input focus first.
  That is only half of it, and the other half was the real cause: **there was no
  keyboard at all.** `VirtualKeyboardEnabled` defaults to false
  (`Settings.cpp:189`) and was false in both shipped configs, and
  `WindowServerLuna` builds an `InputWindowManager` only when it is set
  (line 142). That object owns the `IMEManager` by value, and `IMEManager`
  searches `/usr/lib/luna` for keyboard plugins only when asked to create one
  (`InputWindowManager.cpp:69` is the single caller) -- so nothing ever asked,
  the search never ran, and its unconditional qDebug never appeared in any log.
  `libkeyboard-efigs-phone.so` and `libkeyboard-efigs-tablet.so` were installed
  in that directory the whole time and never loaded. Worth remembering as a
  shape: a missing log line meant "this code never ran", not "this code ran and
  found nothing".
- **The on-screen keyboard draws but cannot be typed on, so it is off again.**
  Setting `VirtualKeyboardEnabled=true` loads the efigs plugins and paints a
  real tablet keyboard -- and every tap on it dismisses it, which is worse than
  having none. MEASURED with `WEBOS_TRACE_IME=1 WEBOS_TRACE_TOUCH=1` (both
  traces are in the tree): over one session, 10 `VIEWPORT TouchBegin` and
  exactly 0 `IMEVIEW TouchBegin`. The keyboard is not refusing the touches, it
  is never offered one. They are consumed upstream:

      FILTER overlay touch: type 194, points 1, uSearchState 2
      FILTER overlay handleTouchBegin -> 1
      VIEWPORT touch consumed by the filter chain: type 194

  `WindowServerLuna::sysmgrEventFilters` (1203-1223) is desktop-port code: while
  Just Type is visible it hands every touch to `OverlayWindowManager` and
  returns what that returns, without ever looking at where the touch landed. A
  `true` there makes `WindowServer::viewportEvent` return before reaching
  `QGraphicsView::viewportEvent` (line 947), so `QGraphicsScene` never runs its
  own delivery and no item sees a `TouchBegin`. The launcher reads the tap as a
  click outside its search field, drops input focus, and
  `IMEController::hideIMEInternal` hides the keyboard -- which is exactly why it
  vanishes when touched.

  Ruled out along the way, each by evidence and not by reading: the touch does
  reach the process (`VIEWPORT TouchBegin` fires); `acceptPoint`'s geometry is
  correct (`bounds h 920, keyboard h 340` against taps at y=666, so its cut at
  580 would accept them); the IME is genuinely open (`signalShowIME` is emitted
  only inside `if (!m_imeOpened)`, so the log line proves the flag); mouse
  handling is irrelevant, because `MouseEventEater` means no mouse event ever
  reaches the scene, making `IMEView::mousePressEvent` dead code here; and
  z-order is not it (`m_inputWindowMgr` is parented last and nothing calls
  `setZValue` on it).

  The fix is small and has a precedent to copy: `doReticle` is already the
  "does this point belong to the IME?" question, and `WindowServer.cpp:1408`
  asks it before showing the reticle. That block needs the same guard, latched
  for the whole touch sequence so the overlay never receives an End without its
  Begin.
- **Two settings files, and the second one wins, and it is not called what it is
  called.** `Settings::load` reads `/etc/palm/luna.conf` and then
  `/etc/palm/luna-platform.conf` (Settings.cpp:248-249), so every key the second
  defines overrides the first. There is no `luna-platform.conf` in the source
  tree: `assemble-rootfs.sh:21` copies `luna-sysmgr/conf/luna-desktop.conf` to
  that name. So the screen size lives in `luna-desktop.conf`, and editing
  `DisplayWidth` in `luna.conf` changes nothing at all. What makes it worth a
  note is how convincing the failure looks: the file is right, the `[General]`
  section is right -- `KEY_INTEGER("General","DisplayWidth",...)`,
  Settings.cpp:406 -- and reading the running shell's own copy through
  `/proc/<pid>/root/etc/palm/luna.conf` shows the new value, because `/etc/palm`
  really is bind-mounted from the rootfs. Everything checks out except the
  result.

- **A first-ever start needs `init` AFTER `services`, and the tools now enforce
  it.** configurator registers every db8 kind by calling com.palm.db, so db8 has
  to be answering first. Run before the services stage, it rejected all 35 kinds
  with "com.palm.db is not running" and left the database empty -- and nothing
  said so out loud: the shell drew, the apps started, and they were simply empty.
  Memos opened with nothing and refused to add; calendar retried getCalendars
  every ten seconds with "kind not registered"; email logged the same for its own
  kinds. The old build trees hid it, their database already holding the kinds
  from earlier sessions. `tools/run-lunasysmgr.sh init` now starts mojodb-luna if
  it is not running and waits for com.palm.db before configuring: 41 kinds and 66
  permissions, 0 failed, and the apps fill up.

- **Nobody was creating the profile account, so the calendar had nowhere to put
  a calendar.** HP's accounts service ships a `createLocalAccount` method
  (`handlers/create-local-account.js`, registered public in `services.json`) that
  makes a `com.palm.palmprofile` account named "Open webOS" carrying the
  CONTACTS, CALENDAR, TASKS, MEMOS, MESSAGING, PHONE and LOCAL.FILESTORAGE
  capability providers. On a device an upstart job called it
  (`files/etc/event.d/createLocalAccount`, `start on started LunaSysMgr`); there
  is no upstart here, so it was never called and `listAccounts` answered
  `{"results":[]}`. That is the whole reason the calendar looked broken: it
  creates even its own *local* calendar only for an account whose templateId is
  `com.palm.palmprofile` (`CalendarsManager.js`, `gotCalendars`), so with no
  account `added` stayed false and `com.palm.calendar:1` stayed empty. Calling
  the method by hand created the account (`_id ++OQFGSME00cBuzk`) and the app
  then made its calendar ("HP webOS Account", `syncSource: "Local"`) on its own.
  `tools/run-lunasysmgr.sh init` calls it now; the handler asks `listAccounts`
  for an existing profile account first, so repeating it answers
  `{"returnValue":true,"accountCreated":false}` and changes nothing.
  Note this is not why the accounts *app* looks empty: it hides that account
  deliberately, asking for the list with
  `excl=["com.palm.palmprofile","com.palm.sim"]`.
- **`path.existsSync` is gone from node**, and `create-local-account.js` touches
  `/var/luna/preferences/first-use-profile-created` through it. The account was
  created and the handler then threw `TypeError: pathLib.existsSync is not a
  function` on the next line, so the flag was never written. Nothing was
  corrupted -- the handler's own `listAccounts` check is what keeps it from
  creating a second account -- but the exception is noise and configurator's
  upstart job used to `start on first-use-profile-created`. It is restored in
  `components/node-v8-shim/js/webos-node-compat.js`, which every JS service
  already loads through `NODE_OPTIONS=--require`, rather than by editing HP's
  handler. A sweep for the rest of that family (`require('sys')`, `util.print`,
  `new Buffer(`, `path.exists`) turns up nothing else outside the tests.

- **What a clean start leaves in the logs, so none of it gets investigated
  twice.** With the kinds loaded first, email walks its whole startup: carrier
  defaults, account list, `com.palm.app.email.prefs:1` created and loaded, the
  folder and email change processors watching, the card opening, and MailApp
  reaching its first-launch view with the one account template we install.
  Calendar reaches first launch too. What is left is noise with a known cause:
  * `enyo.xhr.request() exception ... tellurium_config.json` -- enyo catches this
    itself (`try { send() } catch` in enyo-build.js) and returns undefined.
    Tellurium is left out on purpose, and a device without it logs the same line.
  * `_CallAcquire failed` -- a `g_debug()` in luna-service2's own client
    (callmap.c:1090) when a reply arrives for a call already off the map.
  * `Service does not exist: com.palm.power` / `com.palm.audio` / `com.palm.vpn`
    -- components this tree does not build.
  * `calendar.AppIcon.updateIconFailed` -- the app-icon update service is absent.
  The single real failure left is `this.$.body.setRedirects is not a function`
  (MessageDisplay.js:906, HtmlView.js:99), which is enyo's WebView control
  forwarding to the browser adapter. Email needs the browser to render a message
  body and for nothing else: the rest of the app runs without one.

- **`-webkit-border-image` used to imply a border box, and the apps are built on
  that.** In the WebKit webOS shipped, an element with a border image took its
  `border-width` even though no `border-style` was ever declared. Chromium
  computes that border to 0. Measured in isolation: the same box is 100x100 with
  no `border-style` and 140x140 with one. 75 of the 94 stylesheets in this tree
  put `border-width` next to `-webkit-border-image` and never a `border-style`.
  The image is still painted -- what is lost is the space it used to occupy.

  The calculator shows the cost, and it cascades. Its keys ask for a 15px border
  image, so a key measures 121x94 instead of 91x64; the app sizes its own font
  from the key it measures (`Calculator.js`: `floor(min(h, w) * 0.9)`), so it
  picks 84px where it used to pick 57px, and every two-character label -- MC, M+,
  M-, MR -- spills out of its key. That is the "huge buttons" the shell showed.
  Nothing was wrong with the fonts (Prelude resolves correctly through
  `$ROOTFS/etc/fonts.conf`), with the legacy `-webkit-box` flexbox (still
  distributes space correctly here), or with the card compositing (a texture
  brush does scale with the item transform) -- each of those was measured and
  ruled out before the border turned up.

  `components/qtwebkit-compat` now injects a script at document creation that
  walks the stylesheets and gives `border-style: solid; border-color: transparent`
  to every rule carrying a border image and no style of its own. A rule that
  declares its own `border-style` is left alone. `tests/border-image-box`
  covers it, and fails (exit 1) when the injection is removed.

  **And for a long time it reached only half the rules.** The injection walked
  `document.styleSheets[i].cssRules` flat. A `CSSMediaRule` has no `.style`, so
  the loop's `if (!style) continue` skipped it -- and its children are not in
  that list, so every rule inside a media block went unpatched. The enyo
  stylesheet the package ships has **114** border-image rules inside media
  blocks: the whole radio, tab and button theme sits inside
  `@media (-webkit-max-device-pixel-ratio: ...)`.

  The clock's toolbar is what showed it -- two bare icons touching each other
  with no button box at all. `.enyo-radiobutton` carries
  `border-width: 0px 16px` while the border image sits on
  `.enyo-radiobutton.enyo-first` inside the media block, so the element never
  got a border-style and its 16px sides computed to zero. Patching either rule
  is enough: border-style applies to the *element*, not to the rule that set it.

  Measured in the running shell with the same probe before and after:

  | page | border-image elements | still `style:none` |
  | --- | --- | --- |
  | clock (main.html) | 11 | **2 -> 0** |
  | luna-applauncher | 22 | 0 |
  | calculator | 52 | 0 |

  and the two buttons went from `w=20 bw=0px st=none` to
  `w=52 bw=16px st=solid`. The calculator and the launcher were never affected
  because their rules are top-level, which is exactly why this hid behind a test
  whose three cases were all top-level too.

  Two things worth not repeating. The first attempt recursed with
  `if (rules[r].cssRules) { ...; continue; }` and stopped the script doing
  anything at all: an empty `CSSRuleList` is still an object, so the guard fired
  on ordinary rules that had a perfectly good `.style`. The test caught it at
  once -- the plain case fell from 130 to 100 and no resize was dispatched,
  which is the signature of the script not running rather than running wrong.
  Recurse *in addition to* patching, never instead of it. And measuring this
  needs both `-webkit-border-image` and `border-image-source` read off the
  computed style: filtering on the standard property alone reported 0
  border-image elements in the calculator, contradicting the measurement earlier
  in this entry -- the instrument was wrong, not the fact.

  Restoring the border is only half of it, and the half on its own looks worse
  than the bug. Every box shrinks by the border it just got back, while an app
  that already measured itself keeps the number it computed without one: the
  calculator held its 84px font for keys that were now 71px wide, and the labels
  spilled further than before. So the adapter dispatches a `resize` once it has
  changed something, which is what enyo's controls and the apps listen to in
  order to measure again (`Calculator.js: resizeHandler`). It also watches for
  stylesheets that arrive later, because enyo adds its own from script
  (`dom.js: makeElement("link")`), after DOMContentLoaded and after load.
  Measured on the calculator's own page: 84px and a 13px overflow before, 46px
  and the labels in their keys after.

- **Closing a card killed WebAppMgr, and LunaSysMgr followed it out.** Caught
  under gdb (`WEBOS_WAM_WRAPPER` runs WebAppMgr through a debugger):

      #0  QObject::parent (this=0x555555d41c80)
      #1  WebAppBase::destroyActivity      WebAppBase.cpp:257
      #2  WebAppBase::cleanResources       WebAppBase.cpp:62
      #3  WindowedWebApp::~WindowedWebApp  WindowedWebApp.cpp:106
      #4  CardWebApp::~CardWebApp          CardWebApp.cpp:303
      #6  WebAppManager::closeAppInternal  WebAppManager.cpp:1143
      #7  WindowedWebApp::onClose          WindowedWebApp.cpp:290

  `destroyActivity()` asks `m_page->parent()`, and `m_page` was freed memory:
  not null, so it walked straight through the `if (!m_page)` guard above it. A
  `SysMgrWebBridge` can be the Qt child of another bridge -- `openWindow()` and
  the shell page both call `setParent()` -- and Qt deletes a child along with
  its parent, without telling the `WebAppBase` that points at it. Making that
  member a `QPointer<SysMgrWebBridge>` (the component already uses QPointer for
  `m_jsObj`) nulls it when Qt destroys the object, so the existing guards do
  what they were written to do.

  Not the border-image adapter: the same crash, with the same signature, is in
  the logs of a session that ran the binary built before any of that work.

  Left alone, but worth knowing: the pointer the other way, `SysMgrWebBridge::
  m_client`, is a raw `WebAppBase*` cleared only in `detach()`, and two places
  assign it directly (`SysMgrWebBridge.cpp:383` and `:443`). Nothing observed
  has fired it.

- **Two taps to open an app, and the calendar opening three cards at once.**
  Both came from `SysMgrWebBridge::slotSetupPage()`, which is what turns a window
  a page opened into a card, and both were measured through the bus rather than
  by tapping: `luna-send -n 1 palm://com.palm.applicationManager/launch
  '{"id":"com.palm.app.calendar"}'`, counting the windows the shell attached.

  The first tap was eaten on purpose. That function ends in a card branch that
  throws away the first card window of an app flagged as launched at boot:

      } else { // card
          if (parent->m_launchedAtBoot) {
              parent->m_launchedAtBoot = false;
              this->deleteLater();

  The rule is meant to drop the window a headless app opens *while* it is being
  started at boot. The three apps started that way here -- calendar, clock,
  email -- open none: they check `params.launchedAtBoot` and return. So the flag
  survived boot and ate the user's first real launch instead. `relaunch()` now
  clears it as it delivers: a relaunch is a launch request, so what it opens was
  asked for. Measured: first launch 0 windows before, 1 after.

  The three cards were one window adopted three times. `slotSetupPage()` runs on
  `urlChanged`, and QtWebEngine emits that several times for one page -- the
  blank document it starts on, then the real one -- where QtWebKit emitted it
  once, so `launchWithPageInternal()` ran again on each firing. The probe showed
  it plainly: one `createWindow` from the calendar, three adoptions. HP's earlier
  tree opens the same function with `if (m_client) return;`, which this one had
  lost; with it back, one launch is one card. Measured across calendar, email,
  notes and calculator: +1 window each, no crash.

  Also restored while in there, and honestly not the cause of either: a relaunch
  that arrives while the page is still loading is parked in `relaunch()`, and
  nothing ever took it out again -- the three members were written and read
  nowhere. `slotLoadProgress()` delivers it at 100 now, as HP's earlier tree did.
  That path was never taken in any of these measurements.

- **The prefixed transition events enyo still waits for.** Chromium fires
  `transitionend` and nothing else; measured here, a listener registered as
  `webkitTransitionEnd` is called zero times and one on the modern name once.
  enyo registers only the prefixed name -- nineteen places in the framework, six
  of them `addEventListener` -- and `enyo.Pane` treats a transition as still in
  flight until that handler runs. `Pane.flow()` applies `display: none` only to a
  view that is neither the current one nor transitioning, so a pane that switches
  views never hides the one it left. `components/qtwebkit-compat` now registers a
  prefixed listener under the modern name as well, covering transition and
  animation events; `tests/prefixed-transition-event` checks it and fails when
  the injection is removed.

  It did not clear the overlap in the mail card, which is what sent me looking:
  email still paints its first-launch screen under the three-pane view. The
  likelier cause there is its own `_unhideMainApp()`, which selects the mail view
  and then throws on `$$.body.setRedirects` -- a method of the browser's WebView
  control, which this tree has no implementation for -- so the half that hides
  the first-launch view never runs. That one waits on the browser.

Not done yet:

- **Checked by hand on Qt 6:** the shell and the apps run, and the line QtWebKit
  5.212 drew across Enyo text fields is gone. Not yet gone through app by app.
- **`/usr/palm/frameworks/tellurium` is not in the rootfs,** so every enyo app logs
  a failed load of `tellurium_config.json`. Not Qt 6's doing: the file exists in
  `components/enyo-1.0` and assemble-rootfs.sh never installs that directory.
- `QGLWidget` over `QOpenGLWidget` differs in one way that can matter at
  runtime: Qt 5 created the GL context in the constructor, Qt 6 only when the
  widget is first shown, so `makeCurrent()` right after construction does nothing.

---

### ~~The mail card opens half transparent and stops responding~~ (fixed)

Two views painted on top of each other, the top one frozen part-way through its
fade, and from then on the card ignored every navigation.

The cause is one line of HP's, in `enyo-1.0/framework/source/dom/util.js`:

```js
var builtin = window.webkitRequestAnimationFrame;
enyo.requestAnimationFrame = builtin ? enyo.bind(window, builtin) : ...
var builtin = window.webkitCancelRequestAnimationFrame || window.clearTimeout;
enyo.cancelRequestAnimationFrame = enyo.bind(window, builtin);
```

Chromium still has `webkitRequestAnimationFrame` but dropped
`webkitCancelRequestAnimationFrame`, so that `||` settles on `clearTimeout`.
enyo then takes handles from the frame scheduler and hands them to the timer
one. They number their handles independently, so a cancelled frame clears
whichever timeout holds the same number.

What that costs: `enyo.transitions.Fade` drives its animation from a
`setTimeout` chain kept in a single handle, and the scroller cancels a frame
thousands of times as it starts and stops. When the numbers collide the chain
stops, and since only the Fade's `done()` clears `Pane._transitioning`, the pane
stays transitioning forever -- `flow()` only hides a view that is neither
current nor transitioning, so the outgoing view keeps painting, and
`transitionView()` queues every later change instead of running it.

Measured in the running shell, on the mail card:

- `enyo.cancelRequestAnimationFrame(55)` killed a plain `setTimeout` whose id
  was 55. `window.webkitCancelRequestAnimationFrame` is `undefined`;
  `window.webkitRequestAnimationFrame` is still a function.
- 12245 frame cancels in 14 seconds, all of them `clearTimeout`.
- The pane froze at `_transitioning: true`, `handle: 53`, with `firstLaunch` at
  `opacity: 0.310755` over `slidingPane` -- the same handle every run.
- Re-running the same transition by hand afterwards completed in 153 ticks, so
  the Fade itself is healthy; only the startup, where the scroller churns, kills
  it.

Ruled out along the way, each by measurement, not argument: an exception
escaping the timer callback (zero `Runtime.exceptionThrown` in 45 s across every
page), a blocking bridge call inside the callback (211 synchronous bridge calls,
1-6 ms each, none stalling), and two overlapping fades sharing `this.handle`.

The fix is in `components/qtwebkit-compat`: hand the prefixed canceller back, so
HP's `||` finds it and cancels frames instead of timers. enyo is untouched.
`tests/frame-cancel` covers it, and mutation-verified: with the shim removed,
"cancelling a frame leaves a plain timer alone" fails.

Confirmed in the shell afterwards, on the same card that used to freeze:
`webkitCancelRequestAnimationFrame` is a function again, a plain timer survives
`enyo.cancelRequestAnimationFrame`, and the pane ends at
`_transitioning: false`, `queueLength: 0`, with `slidingPane` at
`display: none` and `firstLaunch` at `opacity: 1` -- one view, fully opaque,
and navigation works again.

## Blocked on a missing dependency

### ~~The QML parts of the UI do not draw~~ (fixed)

**Symptom.** Notifications appeared as an empty band with no text. The log only
said `QQmlComponent: Component is not ready`, which explains nothing.

**Cause.** Three things, in order:

1. `qml-module-qtquick2` was not installed, so `QQmlComponent` could not resolve
   the import. Found by loading the file with a minimal standalone loader
   (`tools/qml-check.cpp`), which does print the real error.
2. All 34 QML files opened with `import Qt 4.7`, which is QML 1; Qt 5 removed
   QtDeclarative. Measured with that loader: 0/34 loaded, 30/34 after swapping
   the import for `QtQuick 2.0`. The other 4 use types registered from C++ and
   cannot be checked standalone.
3. Even loading, none of it drew. A QtQuick 2 root is a `QQuickItem`, which is
   not a `QGraphicsItem`, so the `qobject_cast<QGraphicsObject*>` at all seven
   call sites returned null -- silently, and every site guards on null.

**Fix.** `QmlSceneItem` (`Src/base/QmlSceneItem.h`) hosts a QtQuick 2 scene
inside the QGraphicsScene: an offscreen `QQuickWindow` driven by
`QQuickRenderControl`, grabbed to a `QImage` and blitted in `paint()`, with
scene events translated back into window events. Callers keep the QML root
through `rootItem()`.

**Worth knowing if this breaks again.**

- Input arrives as touch, not mouse: `WindowServer::deliverAsTouch` converts it
  and `MouseEventEater` swallows the rest. The host must
  `setAcceptTouchEvents(true)` and forward touch.
- `QQuickRenderControl::initialize()` warns and returns without initializing
  unless handed the context that is already current. The shell draws through a
  `QGLWidget`, so one usually is.

`SystemMenu.qml`'s `ReferenceError: inProgress is not defined` was a typo for
`airplaneModeInProgress`, fixed along with `MenuContainer.qml`'s, and
`tests/qml-functions-qt5` catches both. The 34 files in
`components/luna-sysmgr-ce/` still carry the QML 1 import; that tree is kept for
reference and not built.

### ~~Calendar and email open empty~~ (the JavaScript services run, on demand)

All six of HP's JavaScript services start when something calls them and answer,
with LunaSysMgr and WebAppMgr up and nothing started by hand:

    com.palm.location                    answered
    com.palm.connectionmanager           answered
    com.palm.service.accounts            answered
    com.palm.service.contacts            answered
    com.palm.service.contacts.linker     answered
    com.palm.service.calendar.reminders  answered

That is `tests/node-shim/service.sh`, which needs the bus and the static services
up and so is run by hand. Underneath: HP's JavaScript unchanged, on node 26,
through `components/node-v8-shim`.

What had to be true for that, each of which was broken:

- **The hub starts services through the namespace.** ls-hubd stays outside the
  bwrap namespace on purpose -- it identifies callers through /proc/<pid>/exe --
  so anything it launched straight from the rootfs saw the host's filesystem.
  Every .service file now goes through `run-lunasysmgr.sh js-service` (for
  JavaScript, via bash: run-js-service uses `==` in `[ ]`, which dash rejects) or
  `run-lunasysmgr.sh ns-exec` (for C++).
- **Every service's bus files are installed.** Four of the six were skipped
  outright: the install read an `id` from services.json, and the app-services do
  not have one.
- **db8 keeps its data.** mojodb-luna wrote into the tmpfs over /var, so every
  restart of the services emptied the database. /var/db is now bound from the
  rootfs; the kinds survive a restart with no init in between, checked by asking
  db8 for them.
- **init loads the kinds.** When configurator started by init had not registered
  in time, the hub launched a copy from outside the namespace, which found
  nothing in /etc/palm/db/kinds -- 2 configurations instead of 41. With ns-exec
  that copy sees the same paths.
- **luna-send has a role.** HP's templates in luna-service2 were never installed,
  so the private hub refused luna-send outright.
- **Two of HP's JSON files parse.** The accounts services.json and the palmprofile
  account template had trailing commas.
- **There are account templates.** The accounts service lists every template
  under /usr/palm/public/accounts, and HP's -- in mojomail and
  app-services/account-templates -- were never installed, so it said `Found 0
  account templates` and there was no kind of account to add. It now lists four:
  com.palm.othermail, com.palm.palmprofile (contacts, calendar, tasks, memos,
  phone, messaging, file storage), com.palm.imap and com.palm.pop.
- **Reassembling with the bus up leaves the hub consistent.** ls-hubd reloads
  whenever its .conf is written (inotify on the conf directory), and
  assemble-rootfs.sh used to write it early and fix its paths midway, so a
  running hub reloaded halfway through and kept a view with the JavaScript
  services missing ("Service not listed in service files", every file correct on
  disk). The .conf is now written last, only when it changes, and the running
  hubs get one SIGHUP at the end. Checked by reassembling twice with everything
  up: one reload per hub each time, all six services answering, no manual step.

**Not a problem, though it looks like one:** `com.palm.tempdb`'s .service runs
mojodb-luna on /var/db, the same directory as com.palm.db. HP's desktop config
does too, and it is fine: one mojodb-luna process opens both services
(MojDbLunaServiceApp.cpp opens MainDir and TempDir under the same directory), so
the hub never has a second one to start. com.palm.tempdb answers with the single
process that is already running.

**Worth knowing when checking any of this by hand:** luna-send's `-P` is the
PUBLIC bus and no flag is private. A com.palm service listens on the private bus,
and on the public one only if it has commands marked "public", so a non-public
method answering "is not running" on `-P` is by design. db8 answering -3963
"permission denied" to a find means the kind IS registered; -3970 means it is not.

**Still open.**

- run-js-service prints `Failure writing to tasks file "/no-group/not-present"`
  on every launch. That path is HP's own deliberate fallback for a device without
  its cgroup setup, commented as such in the script; it is harmless.
- Calendar and email have account types to add now, but no account has been
  added and synced end to end. That needs the mail transports (mojomail-imap,
  -pop, -smtp) running against a real server, a separate question from whether
  their services run.

---

## Open, cause not yet found

### ~~LunaSysMgr segfaults~~ (fixed: `fclose(NULL)` on the first card)

The shell dies on its own about two minutes after the last interaction and
takes WebAppMgr and the static services out behind it. Caught in the run log:

    13:46:50.040 d (362893) DisplayManager::notifySubscribers(int, sptr<Event>):
                            {"returnValue":true,"event":"displayInactive"}
    run-lunasysmgr.sh: line 300: 362893 Segmentation fault  .../LunaSysMgr

`DisplayManager::activity()` logs `diff=120000` on the line before. **That is a
coincidence, and an earlier version of this entry was titled as though it were
the cause.** The event fires every 120 seconds regardless, so anything that dies
after two minutes of idling appears to follow it.

Four runs settle it, and they fill all four cells:

| run | displayInactive | segfault |
| --- | --- | --- |
| dev, first    | 0 | 0 |
| dev, second   | 1 | 1 |
| package, first  | **0** | **1** |
| package, second | **1** | **0** |

There is a crash with no displayInactive and a displayInactive with no crash, so
the two are independent. Worth keeping as a shape: the entry was written from a
single run where the two lines happened to be adjacent, which is exactly how a
periodic event manufactures a false cause.

The same table separates the deaths. The first left no `Segmentation fault` line
at all -- that message comes from the script's own shell, so a crash would have
been recorded -- which fits an external signal, not a fault. The other two are
real SIGSEGVs in unrelated contexts: one while idle, one in the alert path, with
`DWMStateAlertOpen`, `Playing default alert sound` and
`DashboardWindowManager::raiseAlertWindow` on the three lines before it.

In every case WebAppMgr then exits by design (`Remote server disconnected.
Exiting...`) and the static services follow it out, so "everything died" is one
fault plus a designed cascade, not several failures.

Ruled out by evidence rather than by argument, because each was believed at
some point during the session that found it:

* **Not the OOM killer.** No `oom-kill`, `killed process` or `segfault` line in
  the kernel ring buffer, with 20 GiB available.
* **Not a reassembly overwriting a running binary.** The binaries under
  `usr/lib/luna` carry an mtime seven minutes *after* the crash; nothing was
  running by then, which is precisely why that copy succeeded -- overwriting a
  running executable fails with ETXTBSY.
* **Not how the process was started.** It had been detached into its own
  session with `setsid` and survived several tool invocations before dying.

**Not established, and worth not repeating:** an earlier death in the same
session left *no* `Segmentation fault` line in its log at all. That message is
printed by the script's own shell, so a crash there would have been recorded.
The two deaths are therefore not known to share a cause, and only this one is a
confirmed SIGSEGV. Do not merge them into one story without new evidence.

**The cause.** `tools/run-lunasysmgr.sh` now takes `WEBOS_SYSMGR_WRAPPER`, the
twin of the `WEBOS_WAM_WRAPPER` that caught WebAppMgr's crash, so LunaSysMgr can
run under gdb inside the bwrap namespace where the fault actually happens. Left
as a passive trap, it caught it:

    #0  fclose () from libc
    #1  CardWindowManager::markFirstCardDone ()   f = 0x0
    #2  CardWindowManager::firstCardAlert ()
    #3  MinimizeState::onEntry ()

`f = 0x0`. HP's code calls `fclose(f)` without checking the `fopen` above it:

```cpp
g_mkdir_with_parents(Settings::LunaSettings()->lunaPrefsPath.c_str(), 0755);
FILE* f = fopen(Settings::LunaSettings()->firstCardLaunch.c_str(), "w");
fclose(f);
```

and the two paths are not the same thing. `lunaPrefsPath` is configurable --
`KEY_STRING("General", "PreferencesPath", ...)`, Settings.cpp:379 -- so the
rootfs assembly repoints it. `firstCardLaunch` is the hardcoded literal
`/var/luna/preferences/used-first-card` (Settings.cpp:167) with no key of its
own, so the `g_mkdir_with_parents` on the line above creates a directory that
is not the one about to be written. Where that path is not writable, `fopen`
returns NULL and `fclose(NULL)` segfaults, which takes WebAppMgr's connection
with it and the services behind that.

**Why it looked intermittent.** The developer tree owns
`build/rootfs/var/luna/preferences` and already had `used-first-card` in it, so
the open succeeded and nothing faulted -- for months. The package installs that
directory root-owned at 0755: `tools/mkdeb.sh` made `var/db`, `var/luna` and
`var/palm` world-writable but not their children, and the child is what gets
written. So the same binary crashed when installed and not when run from the
build tree, which is what made it look like a packaging mystery rather than a
missing NULL check.

**Fixed in two places, because either alone is not enough.** The `fclose` is
guarded and logs a warning naming the path, so an unwritable location costs a
log line instead of the session -- worth having for any read-only install.
`mkdeb.sh` chmods every directory under `var/`, not the three at the top, so the
file can actually be written and first-use is not offered again on every start.

One measurement that is worth not repeating: before the trap caught it, the
shell had run 3m39s under gdb through a full inactivity window and eight app
launches without faulting, which had suggested the bug was gone. It was not --
the crash needs the first card to be *minimised*, which none of those launches
did. A non-reproduction had narrowed nothing.

Two traps to avoid when picking this up. gdb runs every `-ex` in order
regardless of why `run` returned, so a wrapper that prints "FAULTING THREAD"
unconditionally reports a captured crash for a clean exit -- the giveaway is
`No stack.` under the heading. The wrapper now tests `$_isvoid($_siginfo)` and
says which it was. And a long-running tool invocation that is killed takes
`ls-hubd` with it if the bus was started from it: one "crash" investigated here
was LunaSysMgr exiting with code 1 three seconds after the hub had gone,
which the log shows plainly as `Failed to connect. Is the hub running?`. Start
the whole stack under one `setsid`.

### ~~LunaUniversalSearchMgr dies inside the namespace~~ (never did)

It was reported dead in every status line while up to thirteen copies of it were
running at once -- one leaked per launch, all session.

Linux truncates a task's comm to 15 characters. `pgrep -x` and `pkill -x` match
against comm, so for `LunaUniversalSearchMgr`, which is 22, they match nothing.
The status line read that as dead, and the same truncation stopped the script's
`pkill -x` from ever reaping the previous copy. pgrep does say so, on stderr:

    pgrep: pattern that searches for process name longer than 15 characters
           will result in zero matches

run-lunasysmgr.sh now compares argv[0] out of /proc, which is exact and has no
length limit. All five of HP's static services report alive.

Guarded by tests/long-process-name.sh.

### Just Type's touch coordinates went through a fudge factor (fixed, unverified)

`OverlayWindowManager::mapCoordToWindow` converts a position from the manager's
coordinates into the window's. Under Qt 5 on the desktop it did not:

    y -= kTouchPointYOffset;          // 50. x was not converted at all.

That was downstream of the cause. Its touch handlers read `scenePos()` where the
mouse twin reads `pos()`, and the events reaching them come from
`WindowServerLuna::sysmgrEventFilters` -- the raw viewport event, before
QGraphicsView translated anything -- so nothing was in the space the conversion
needed and a constant was subtracted until it roughly lined up.

The `sceneEvent` branch that would have delivered them the other way is
unreachable: neither the class nor `WindowManagerBase` ever calls
`setAcceptTouchEvents`. That is what made the fix tractable -- there is only one
live path, so the position can be converted once, on the way in, and the real
conversion restored.

**Not verified by using it.** It builds, the shell runs, and
`tools/check-touch-vs-mouse.py` no longer reports the mismatch, but driving Just
Type with synthetic input did not work well enough to confirm taps land where
they should. Worth trying by hand: open Just Type, tap in the field, and see
whether the caret goes where you tapped.

### ~~A line crosses the Just Type search field~~ (gone on Qt 6)

Two thin horizontal lines are drawn inside the search input, one through the
text. It is an Enyo `RichText`; the CSS renders differently on QtWebKit 5.212
than on HP's WebKit. Cosmetic.

QtWebEngine does not draw them, so they are gone (checked by hand). They were
only ever a QtWebKit 5.212 rendering difference.

---

## Known and accepted

### The bridge is synchronous, on purpose, and why that is safe here

HP's apps talk to the system through an object QtWebKit published into the page
with `addToJavaScriptWindowObject()`. Reading a property or calling a method was
an ordinary, immediate JavaScript expression. QtWebEngine has no equivalent: the
page lives in another process, and everything it can be told is asynchronous. So
the old contract is kept by blocking.

**The mechanism**, in two halves of `components/qtwebkit-compat/src/qtwebkit_compat.cpp`:

The injected side (`kBridgeCore`, line 325) builds a proxy per published object
from its metadata and turns every access into one blocking request:

```js
xhr.open("GET", "webos-bridge:///" + id + "/" + op + "/" + name
         + "?a=" + encodeURIComponent(JSON.stringify(args || [])), false);
xhr.send();
```

That trailing `false` is the whole trick. `Object.defineProperty` maps each
property to `request(id, "get", name)` and `request(id, "set", name, [value])`,
and each method to `request(id, "call", name, args)`.

The native side is a `QWebEngineUrlSchemeHandler` (`BridgeHandler`, line 913) on
a scheme registered before the QApplication exists, which answers by
introspection: `indexOfProperty` then `read`/`write` for properties,
`invoke` for methods, and a JSON body back through `job->reply()`. Signals
travel the other way, injected per frame by `runInEveryFrame`.

Not synchronous script evaluation from C++, and not preloaded static values:
those were both considered and neither can serve an arbitrary `QObject` whose
properties change. And **not QWebChannel** -- it is used nowhere in this tree,
which matters for anyone planning to add an async path: there is no second
transport already in place to piggyback on.

**Why blocking is not the hazard it looks like.** A sync XHR freezes the calling
page's JavaScript until C++ answers. On a browser that would stall the UI; here
it cannot, because the UI is not in that process. LunaSysMgr is a separate
process that composites the cards itself: it takes each app's pixels through
shared memory (`CardWindow::acquireScreenPixmap`, `RemoteWindowData`) and moves
the cards with `QPropertyAnimation` in C++ (`CardWindowManager`). An app that
blocks its own JavaScript freezes its own content and nothing else -- not the
compositor, not the gestures, not the other cards. HP's IPC split between shell
and apps is what buys that, and it is worth knowing before anyone "fixes" the
blocking.

**What is genuinely at risk** is Chromium's tolerance for synchronous XHR, which
has been narrowing for years. Nothing breaks today, but a Qt update could end
it, and then the fix is a second transport rather than a patch: QWebChannel or a
hand-rolled async channel, with the injected proxy choosing between them.

A dual path is the obvious shape for that, and it is worth being clear about who
it would serve. Not HP's apps: measured across `enyo-1.0/framework`,
`core-apps` and the browser, `new Promise`, `async function` and `await` appear
in zero files, and there is not one arrow function in their JavaScript (the
`=>` matches in that tree are Ruby hashes in Rakefiles). Enyo 1.0 is ES5 from
2011 and would keep taking the blocking path forever. An async path is
insurance against a future Qt, and a door for new apps -- not a speed-up for
anything that exists.

On cost, to avoid overclaiming: the per-call round trip has **not** been
measured. What has been measured is that the system idles at 0% of one core
across 16 processes and that a full card repaint costs 2.96 ms, so nothing
observed so far points at the bridge as a bottleneck. That is an absence of
evidence for a problem, not evidence that each call is free.

### The browser's padlock was never clickable, and the SSL dialog is a real gap

Two separate things, and only one of them is missing.

**The padlock is not a regression.** `AddressInput.js:33` declares it as
`{name: "secureLock", kind: enyo.CustomButton, showing: false, className:
"secure-lock"}` -- with no `onclick`, while every sibling in that same list has
one (`refreshButton` has `onclick: "doRefresh"`, `stopButton` has
`onclick: "doStop"`). It is shown when the URL is https (line 85) and that is
all it ever did. HP's own comment three lines above `showLeftButton` says it:
`// need an IxD for secure lock icon (wireframe b5)`. The interaction design was
never done, so clicking it did nothing on a TouchPad either. Nothing to restore.

**The certificate-error dialog is missing, and it is a capability.**
`Browser.js:248 showSSLConfirmDialog(inSender, inHost, inErrorCode, inCertFile)`
is a complete dialog -- "Trust Always", "Trust Once", "Don't Trust", "View
Certificate" -- and nothing in this tree raises it. On a bad certificate the
page simply fails. What HP fed it is in
`components/BrowserServer/Src/SSLValidationInfo.h`: the certificate's file path,
a user message, the common name, the host name, the signing CA, a failure reason
code, and an accept decision travelling back.

Qt has the hook: `QWebEnginePage::certificateError` delivers a
`QWebEngineCertificateError` carrying `url()`, `type()`, `description()`,
`certificateChain()` and `isOverridable()`, with `defer()`,
`acceptCertificate()` and `rejectCertificate()` to answer once the user has. So
the shape of the work is: catch it in `BrowserViewAdapter`, `defer()`, write the
chain somewhere the app can name, raise `onSSLConfirmDialog`, and answer with
the response the app sends back.

Two things it will run into. HP's `inErrorCode` is a curl/OpenSSL validation
code and the app switches on exact ranges (0, 2-4, 5-9, 10-17, 18-23, 24-29,
30/31/50) to choose its wording; `QWebEngineCertificateError::Type` is a
Chromium enum and will need mapping, with a default for what does not map. And
"View Certificate" cannot work at all: `CertificateDetail.js:198` calls
`palm://com.palm.certificatemanager/getcertificatedetails`, and that service
exists nowhere in this tree -- only two references to its name do, in the
browser and in enyo's wifi module. The dialog is worth having without that
button.

And for a page that loads *correctly* there is no certificate to show even if
the padlock had a handler: Qt exposes a chain only through the error path.
`QWebEnginePage` has no accessor for the peer certificate of a good connection.

### Performance: where the time goes, and why the GPU is off

Measured 2026-09-12, because "can we put this on Vulkan and Wayland" deserves
numbers rather than an opinion.

What it costs today:

* **Idle: nothing.** 16 webOS processes, found by walking `/proc` and matching
  their `exe` against the rootfs, burn 0% of one core over 5 seconds.
* **One full card repaint: 2.96 ms median** at 1280x920 (best 2.58, worst 5.89 --
  `tests/grab-cost`, built but deliberately not a test). That is a ceiling of
  ~338 fps for the offscreen render and read-back the whole card model rests on,
  so the read-back is not the bottleneck it looks like.

What is already accelerated, contrary to the obvious guess:

* The shell's `QGraphicsView` viewport **is** a real `QOpenGLWidget`.
  `HAVE_OPENGL` is defined (CMakeLists), `ForceSoftwareRendering` is set in
  neither installed config so the code default of false stands, and
  `qt6-compat`'s `QGLWidget` is a `QOpenGLWidget` subclass rather than a stub.
* Proof from the live process, not inference: LunaSysMgr holds
  `/dev/dri/renderD128` open, with `libGLX_mesa`, `libEGL` and `libdrm_intel`
  mapped. Hardware GL on the Intel iGPU.

Why QtWebEngine runs on the CPU, and why that stays:

`qtwebkit_compat.cpp:59` puts `--disable-gpu` into `QTWEBENGINE_CHROMIUM_FLAGS`
unless the environment already carries flags, because WebAppMgr only ever draws
pages offscreen and reads them back, where Chromium's GPU process loses its
context. The engine's processes hold no render node, which matches.

Lifting it was tried: with `QTWEBENGINE_CHROMIUM_FLAGS` set to anything else,
**WebAppMgr dies with SIGSEGV during startup**, and LunaSysMgr then exits behind
it by design -- `WebAppMgrProxy was Disconnected!! Exiting Sysmgr...`. So the
flag is load-bearing, not an oversight. Whether other flag combinations survive
(forcing software GL inside the GPU process alone, say) was not tried.

Vulkan and Wayland, specifically:

* **Vulkan cannot reach the shell's UI.** Qt has no Vulkan backend for
  `QWidget`/`QGraphicsView`, which is what the whole shell is drawn with. Qt
  Quick's RHI does list `vulkan`, but `QmlSceneItem::setUpSoftwareBackend`
  pins QtQuick to the Software backend on purpose: `QQuickRenderControl` sets up
  an RHI unless the graphics API is Software, and `sync()` then refused with
  "can only sync when beginFrame() has been called".
* **Wayland works, and buys little.** There is no X11-specific code anywhere in
  luna-sysmgr, webappmanager or luna-sysmgr-common; a platform only has to be
  named at all because LunaSysMgr asks for HP's "palm" plugin. It is the default
  now: the shell comes up as a native Wayland client, keeps hardware GL
  (`/dev/dri/renderD128` still open), and was driven for a minute -- browser
  launched, card maximized, touches and text-field focus all fine. What it
  removes is XWayland's copy, against a shell that already idles at 0%, so this
  is hygiene rather than speed.

  It costs something real, though, and it is worth knowing before debugging
  under it: the window is no longer an X client, so **xdotool and ImageMagick's
  `import` stop working**, and GNOME refuses `org.gnome.Shell.Screenshot` over
  D-Bus ("Screenshot is not allowed"). Every way this tree has of looking at the
  running UI from outside goes away at once. `QT_QPA_PLATFORM=xcb` brings them
  all back for a session.

Not established, and worth saying so: there is no trustworthy under-load number
here. Two attempts at measuring a scroll produced "0%" -- once from integer
truncation in the measuring script, once from a drag that was never verified to
have moved the page at all. Any future claim about scrolling cost needs an
image-diff proof that the page moved, and raw tick counts rather than a rounded
percentage.

One trap that cost evidence: `run-lunasysmgr.sh` truncates `/tmp/webos/WebAppMgr.log`
on every launch, so a crash's log is gone the moment the shell is restarted.
Copy it aside before restarting, or the next run destroys what you were reading.

### Every page logs a tellurium error at startup

Once per page, in all five:

```
enyo.xhr.request() exception: NetworkError: Failed to execute 'send' on
'XMLHttpRequest': Failed to load
'file:///usr/palm/frameworks/tellurium/tellurium_config.json'.
```

It is noise, and the missing file is HP's own off-switch. Tellurium is the test
automation nub, a separate package for the device that we do not install.
`framework/source/palm/tellurium/startup.js` looks for its config and gives up
when it is not there:

```js
var xhr = window.enyo.xhr.request({url: Tellurium.nubPath + "tellurium_config.json", sync: true});
var resp = xhr && xhr.responseText;
if (!resp || !resp.length) {
    return;
}
```

That still decides correctly. What changed since 2010 is only how the failure
arrives: Chromium throws from `send()` on a file:// URL that does not exist,
where the engine webOS shipped returned an empty response. `enyo.xhr.request`
catches it (`dom/xhr.js`), logs that line and returns `undefined`, so `resp` is
undefined and the switch works exactly as HP intended. Nothing downstream is
affected -- left alone deliberately rather than papered over by making failed
synchronous file:// requests return empty, which would also hide real ones.

Do not "fix" it by installing the nub directory. The config HP ships carries
`{"enableUserEvents": true}`, so `Tellurium.setup()` would then wrap
`enyo.Pane.prototype.doSelectView` to call `com.palm.telluriumservice` on every
view change -- a service that does not exist here, and a wrapper around the very
path that the frame-canceller bug ran through.

Note there are two paths, and only one fails: `loader.js` resolves
`$palm-tellurium/tellurium_config.json` through enyo's path table, which points
inside the installed framework and answers 200. `startup.js` builds
`Tellurium.nubPath + ...` by hand, and that is the one that 404s.

### Files HP's depends.js lists and HP never released

Three scripts are named in an app's `depends.js` and exist nowhere in the drop:

| listed in | file |
| --- | --- |
| `com.palm.app.contacts/depends.js:37` | `app/Ringtones.js` |
| `com.palm.app.contacts/depends.js:50` | `app/dialogs/NameDetails.js` |
| `com.palm.app.accounts/depends.js:23` | `source/FirstLaunch.js` |

`find components -name` finds none of them, so this is a hole in what HP
published, not something the rootfs assembly drops -- it copies each app
directory whole.

Nothing aborts. enyo's loader writes each dependency as a script tag with an
`onerror` that only logs (`dependency-loader.js:7`), so loading continues and
the apps open. What is missing is whatever those kinds provided -- a ringtone
picker, a name-details dialog, the accounts first-use screen -- and a reference
to one of them would throw at the point of use, not at startup.

Each shows up once per launch as `Error loading script <path>`.

### What a sweep of every app turns up

Run with the shell up, closing each card first and launching the seven apps in
turn under `tools/devtools-watch.py`. Worth repeating after engine changes; the
frame-canceller bug proved that reading the source finds nothing, because the
code that fails reads as correct.

Everything below is either HP's or cosmetic. No app failed on an engine
difference, so there is no second `webkitCancelRequestAnimationFrame` hiding:

- Contacts, Accounts: the missing scripts above.
- Memos: `Uncaught TypeError: Cannot read properties of null (reading
  'displayText')`. `MemoView` declares `memo: null` as its default
  (`MemoView.js:32`) and `MemoRowView` builds list rows from it, so
  `memoChanged()` runs once before a row has its model. HP's own logic; the app
  works.
- Calendar: a 404 for `app/images/icon.png`. The icon is at `images/icon.png`
  and every reference in the app asks for exactly that; the miss comes from
  `app/calendar.html` resolving it relative to its own directory. A favicon,
  with no visible effect.
- Clock: a 404 for a header icon under `applications/images/`, same shape.

Two things to know about reading that output. `Log.enable` replays what a page
logged before the watcher attached, so entries marked `[old]` may be from an
earlier run -- the watcher marks anything arriving within half a second of an
attach. And attributing a line to an app by launch time is approximate: apps
take different times to come up, and in this run two lines landed under the
wrong heading.

### db8 builds without leveldb

HP pinned leveldb 1.9; Debian does not package it. db8 configures, builds and
answers queries without it. Whether `mojodb-luna` needs that backend for
anything beyond what we exercise is still unverified.

### The bus logs errors that come from HP

Both appear identically in the reference VM:

- `Attempting to add duplicate service name to permission map:
  "com.palm.lunastats"` — two role files declare the same name.
- `Unable to get permission from JSON (.../com.palm.nodejs.json)` — that file
  has a `role` section and no `permissions`, which the parser requires.

### `(null)` clients rejected on the bus

Anonymous clients with no registered name. They are `luna-send` invocations and
calls LunaSysMgr makes during startup, before it acquires its bus name. Not
symptomatic.

---

## Not built

`BrowserServer`, `BrowserAdapter` and `WebKitSupplemental` are the browser path
and depend on real NPAPI, not just an include path. `luna-sysmgr-ce` (the
TouchPad's 3.0.5) is not built on purpose: Open webOS's `luna-sysmgr` is the
reference implementation, and the CE one stays in the tree for reference only.
