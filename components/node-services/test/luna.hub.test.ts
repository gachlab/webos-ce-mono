// kit/luna.ts against a real ls-hubd, through HP's palmbus addon.
import { openHandle } from "#kit/palmbus.ts";
import { startTestBus } from "./hub.ts";
import { lunaSuite, SERVICES } from "./luna-suite.ts";

lunaSuite(async () => {
    const hub = await startTestBus({ services: SERVICES });
    return { openHandle, teardown: hub.stop };
});
