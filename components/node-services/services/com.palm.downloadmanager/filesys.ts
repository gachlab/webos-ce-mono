// filesysStatusCheck: how full the downloads filesystem is.
//
// HP's LunaDownloadMgr polled it and named the level by how full it was
// ("Low = %u%% , Med = %u%% , High = %u%% , Critical = %u%%", with a stop mark
// in KB). The system UI shows its DiskSpaceAlert for "low", "medium", "severe"
// and "limit", once per level except "limit", and reads amountRemainingKB.
// HP's default marks are not in the binary; these are ours. Each level also
// needs little room left in absolute terms: HP's percentages were for a 16 GB
// tablet, and on a desktop disk 95% full can still leave many gigabytes.

export type Alert = "none" | "low" | "medium" | "severe" | "limit";

export interface Space {
    readonly totalKB: number;
    readonly freeKB: number;
}

export interface Mark {
    // Percent full at which the alert starts...
    readonly percent: number;
    // ...once no more than this many KB are left.
    readonly freeKB: number;
}

export interface Marks {
    readonly low: Mark;
    readonly medium: Mark;
    readonly severe: Mark;
    // Below this many KB free, downloads cannot go on.
    readonly stopKB: number;
}

const GB = 1024 * 1024;

export const DEFAULT_MARKS: Marks = {
    low: { percent: 90, freeKB: 2 * GB },
    medium: { percent: 95, freeKB: 1 * GB },
    severe: { percent: 98, freeKB: GB / 2 },
    stopKB: 20 * 1024,
};

export const POLL_MS = 60_000;

export const alertFor = (space: Space, marks: Marks = DEFAULT_MARKS): Alert => {
    if (space.freeKB <= marks.stopKB) {
        return "limit";
    }
    const full = space.totalKB > 0 ? ((space.totalKB - space.freeKB) * 100) / space.totalKB : 0;
    const reached = (mark: Mark) => full >= mark.percent && space.freeKB <= mark.freeKB;
    if (reached(marks.severe)) {
        return "severe";
    }
    if (reached(marks.medium)) {
        return "medium";
    }
    if (reached(marks.low)) {
        return "low";
    }
    return "none";
};
