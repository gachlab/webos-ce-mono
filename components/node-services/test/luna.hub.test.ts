// kit/luna.ts against a real ls-hubd, through native/lunabus.cpp.
import { openHandle } from "#kit/lunabus.ts";
import { startTestBus } from "./hub.ts";
import { lunaSuite, SERVICES } from "./luna-suite.ts";

lunaSuite(async () => {
    const hub = await startTestBus({ services: SERVICES });
    return { openHandle, teardown: hub.stop };
});
