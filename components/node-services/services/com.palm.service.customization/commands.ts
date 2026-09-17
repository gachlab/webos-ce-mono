// com.palm.service.customization: carrier and region defaults at first use.
//
// HP's service (never released) copied what the carrier and the region
// shipped -- bookmarks, contacts, email defaults, ringtones, wallpapers, from
// /usr/lib/luna/customization -- into db8, and every command answered
// {returnValue: true} whatever happened. There is no carrier and no
// customization partition here, so every command answers the same way with
// nothing to copy. First use (#24) calls populateDefaults; whatever defaults
// this port ends up shipping belong here.

import { mojoHandler, type Command } from "#kit/mojoservice.ts";

export const METHODS = [
    "customize", "checkAndPopulateDefaults", "customizeLoc", "populateDefaults", "populateMccMncDefaults",
    "platformQueryResults", "copyBinaries", "customizeNonLoc", "populateNonLocDefaults", "customizeSystem",
    "populateSystemDefaults", "resetNonLoc", "resetSystem", "resetLoc", "resetCarrier", "resetAllProgress",
    "postFirstUseInstall", "dbglevel",
] as const;

export const customizationCommands = (log: (message: string) => void): Command[] =>
    METHODS.map((name) => ({
        name,
        handler: mojoHandler(({ payload }) => {
            log(`${name} ${JSON.stringify(payload)}: nothing to customize`);
        }),
    }));
