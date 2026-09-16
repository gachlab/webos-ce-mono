// kit/luna.ts against the in-memory bus: runs anywhere, no hub needed.
import { createFakeBus } from "./fake-bus.ts";
import { lunaSuite } from "./luna-suite.ts";

lunaSuite(async () => ({ openHandle: createFakeBus().openHandle, teardown: () => {} }));
