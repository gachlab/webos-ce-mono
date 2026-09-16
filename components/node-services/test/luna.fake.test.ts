// kit/luna.ts against the in-memory palmbus: runs anywhere, no hub needed.
import { createFakePalmbus } from "./fake-palmbus.ts";
import { lunaSuite } from "./luna-suite.ts";

lunaSuite(async () => ({ openHandle: createFakePalmbus().openHandle, teardown: () => {} }));
