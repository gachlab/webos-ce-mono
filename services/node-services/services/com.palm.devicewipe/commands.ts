// com.palm.devicewipe: a remote wipe issued from the HP webOS account.
//
// HP's service (never released) received the request over XMPP pubsub, checked
// it against the account's token and HP's servers, optionally backed up, and
// then called com.palm.storage/erase/Wipe. Without the account or the servers
// no wipe can be issued or verified, so deviceWipe refuses every request, as
// HP's refused one it could not verify. The local erase stays where it always
// was, in com.palm.storage (#41).

import { mojoError, mojoHandler, type Command } from "#kit/mojoservice.ts";

export const devicewipeCommands = (log: (message: string) => void): Command[] => [
    {
        name: "deviceWipe",
        handler: mojoHandler(() => {
            log("deviceWipe refused: no webOS account server to verify it");
            throw mojoError("WIPE_NOT_ISSUED", "Wipe not issued: there is no webOS account server to verify it");
        }),
    },
    // HP's registered for the pubsub node here; there is no pubsub service.
    { name: "registerPubSub", handler: mojoHandler(() => undefined) },
];
