// applicationManager/open for help.webosarchive.org topics.

import type { LunaService } from "@webos/api/infra/luna/service.ts";

export const HELP_BASE = "https://help.webosarchive.org/en-us/";

export interface HelpTopic {
    readonly id: string;
    readonly title: string;
    readonly detail: string;
    /** Path under HELP_BASE (empty string opens the base URL). */
    readonly path: string;
}

// HP Help TOC rows (Tips / Clips / Featured). Paths are optional suffixes.
export const HELP_TOPICS: readonly HelpTopic[] = [
    {
        id: "home",
        title: "Help Topics",
        detail: "Browse the help site",
        path: "",
    },
    {
        id: "tips",
        title: "Tips",
        detail: "Read short how-tos",
        path: "",
    },
    {
        id: "clips",
        title: "Clips",
        detail: "Watch short animations",
        path: "clips/",
    },
    {
        id: "featured",
        title: "Featured",
        detail: "Browse featured articles",
        path: "",
    },
    {
        id: "screen",
        title: "Screen & Lock",
        detail: "Brightness, wallpaper, unlock",
        path: "",
    },
    {
        id: "wifi",
        title: "Wi-Fi",
        detail: "Join and manage networks",
        path: "",
    },
];

export const topicUrl = (topic: HelpTopic, base = HELP_BASE): string => {
    if (!topic.path)
        return base;
    const root = base.endsWith("/") ? base : `${base}/`;
    const path = topic.path.replace(/^\//, "");
    return `${root}${path}`;
};

export interface HelpClient {
    openTarget(target: string): Promise<void>;
}

export const createHelp = (luna: LunaService): HelpClient => {
    const apps = "luna://com.palm.applicationManager/";

    return {
        async openTarget(target) {
            await luna.call(`${apps}open`, { target });
        },
    };
};
