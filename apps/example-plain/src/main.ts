// A card for this device, written without our runtime.
//
// No lit-html. No `defineElement`. No `startCard`. Nothing from
// @webos/ui-kit except the import that defines the controls -- after which
// they are ordinary custom elements and `document.createElement` is enough.
//
// This card exists to be checked, not to be used (#65). enyo's mistake was not
// having layers, it was making the top one compulsory: that is why porting an
// HP app today means rewriting it, and it is what we are not going to repeat.
// So the claim "you could write this in React tomorrow" gets a file that
// proves it, and `tests/plain-card.cpp` fails the day it stops being true.
//
// The two things it demonstrates:
//
//   1. The controls dress themselves. Nothing here hands the kit a
//      stylesheet -- importing it is what styles it -- so a <wos-row> built by
//      hand comes out looking like one built by our renderer.
//   2. `connectCard` is all the platform asks of a card: the lifecycle, the
//      back gesture, and telling WebAppMgr the card is on screen. What draws is
//      the caller's business, and here it is 30 lines of DOM.

import { connectCard, type CardService } from "@webos/api/infra/app/connect-card.ts";
import { createState } from "@webos/api/helpers/create-state.ts";
import { createConnectionManager, type ConnectionStatus } from "@webos/api/infra/luna/connectionmanager.ts";
import { openBus } from "@webos/api/infra/luna/open-bus.ts";
import { createWatch } from "@webos/api/helpers/watch.ts";

// Everything from @webos/ui-kit, and it is a side effect: the import defines
// the elements and styles them. There is no API to call.
import "@webos/ui-kit/kit/kit.ts";

// --- the logic, which is the same shape as any other card's ------------------

type Shown = { status: ConnectionStatus | null };

const createPlainService = (deps: { luna: ReturnType<typeof openBus> }): CardService<Shown> => {
    const state = createState<Shown>({ name: "plain:waiting", data: { status: null } });
    const connection = createConnectionManager(deps.luna);
    const watching = createWatch(() =>
        connection.watchStatus(
            (status) => state.patch({ status }, "plain:ready"),
            (error) => state.set({ name: "plain:failed", data: { status: null }, error: error.message }),
        ),
    );
    return {
        getState: state.get,
        onStateChange: state.subscribe,
        onShown: () => watching.start(),
        onHidden: () => watching.stop(),
        dispose: () => {
            watching.stop();
            state.clear();
        },
    };
};

// --- the drawing, which is not ----------------------------------------------

const root = document.getElementById("card") as HTMLElement;

const header = document.createElement("wos-header");
header.setAttribute("title", "Plain");

const row = document.createElement("wos-row");
const button = document.createElement("wos-button");
button.setAttribute("kind", "affirmative");
button.textContent = "Close";
button.addEventListener("click", () => card.app.close());

root.append(header, row, button);

const service = createPlainService({ luna: openBus() });

const card = connectCard<Shown, CardService<Shown>>({
    service,
    paint: (state) => {
        const status = state.data.status;
        row.setAttribute("title", state.error ?? (status?.online ? "Online" : "Offline"));
        row.setAttribute(
            "detail",
            state.name === "plain:waiting" ? "asking com.palm.connectionmanager..."
                : status?.through ? `over ${status.through}${status.ssid ? ` (${status.ssid})` : ""}`
                : "nothing is carrying a connection",
        );
    },
});
