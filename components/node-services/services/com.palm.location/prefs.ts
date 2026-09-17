// The Location Services preferences, as HP's service kept them (in db8, as
// com.palm.location.LocationServicesPrefs; here a JSON file, since only this
// service reads them).

export interface Prefs {
    // Locate with GPS ("Locate Me Using... GPS").
    readonly useGps: boolean;
    // Locate with the network sources ("... Google Services"); only used once
    // the terms were accepted.
    readonly useGoogle: boolean;
    readonly isTermsOfUseAccepted: boolean;
    // Applications get the location without being asked about ("Auto
    // Locate"); otherwise each one is asked about once a session.
    readonly autoLocate: boolean;
    // Websites may ask for the location ("Always Ask"); false is "Never Share
    // Location".
    readonly webSetting: boolean;
    readonly geotagPhotos: boolean;
    readonly useBackgroundDataCollection: boolean;
    // Websites the user always allows, by origin.
    readonly allowedSites: readonly string[];
}

// Nothing leaves the device until the user says so: the network sources wait
// for the terms, and every application and website asks first.
export const DEFAULT_PREFS: Prefs = {
    useGps: true,
    useGoogle: false,
    isTermsOfUseAccepted: false,
    autoLocate: false,
    webSetting: true,
    geotagPhotos: false,
    useBackgroundDataCollection: false,
    allowedSites: [],
};

export interface PrefsFile {
    read(): string | undefined;
    write(text: string): void;
}

export interface PrefsStore {
    get(): Prefs;
    set(change: Partial<Prefs>): Prefs;
    // Calls `listener` after every change; returns what stops it.
    watch(listener: (prefs: Prefs) => void): () => void;
}

const load = (file: PrefsFile): Prefs => {
    try {
        const text = file.read();
        const stored = text ? JSON.parse(text) as Record<string, unknown> : {};
        const merged: Record<string, unknown> = { ...DEFAULT_PREFS };
        for (const [key, fallback] of Object.entries(DEFAULT_PREFS)) {
            const value = stored[key];
            if (Array.isArray(fallback) ? Array.isArray(value) && value.every((v) => typeof v === "string")
                : typeof value === typeof fallback) {
                merged[key] = value;
            }
        }
        return merged as unknown as Prefs;
    } catch {
        return DEFAULT_PREFS;
    }
};

export const createPrefs = (file: PrefsFile): PrefsStore => {
    let prefs = load(file);
    const listeners = new Set<(prefs: Prefs) => void>();
    return {
        get: () => prefs,
        set: (change) => {
            prefs = { ...prefs, ...change };
            file.write(JSON.stringify(prefs));
            for (const listener of [...listeners]) {
                listener(prefs);
            }
            return prefs;
        },
        watch: (listener) => {
            listeners.add(listener);
            return () => listeners.delete(listener);
        },
    };
};
