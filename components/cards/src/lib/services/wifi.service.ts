// The Wi-Fi settings card: one state machine, five screens.
//
// What it does is what the card before it did (components/wifi-app on enyo's
// lib/wifi), and the vocabulary is HP's: the network list that rescans every
// twelve seconds, the join screen with its "Sign In" button, the address
// settings of the joined network, the known networks, and "When Device
// Sleeps". The difference is where the behaviour lives: here, in one file that
// can be read and tested, instead of spread across a 1500-line library whose
// failure handlers were never written.
//
// Deliberately not carried over from that library:
//   * WAPI. com.palm.wifi answers "wapi":"disabled" -- there is no such radio
//     here -- so its two security types and their screens were dead code.
//   * A blocking alert() when a scan fails. The card says so in its own line.
//   * Its unguarded reads: a target for the joined network waited for a null
//     check that was missing, and a scan applied results after it was stopped.

import { createState, type State, type StateHolder, type Unsubscribe } from "#lib/helpers/create-state.ts";
import { createWatch } from "#lib/helpers/watch.ts";
import { createNavigation } from "#lib/services/navigation.service.ts";
import { createWifi, joined, type Network, type SavedProfile,
         type Security, type WifiStatus } from "#lib/infra/luna/wifi.ts";
import { createConnectionManager } from "#lib/infra/luna/connectionmanager.ts";
import { LunaCallError, errorTextOf, type LunaService } from "#lib/infra/luna/service.ts";
import { t } from "#lib/i18n/translate.ts";
import type { LaunchParams } from "#lib/infra/app/service.ts";

export type WifiScreen = "list" | "join" | "address" | "known" | "settings";

export interface JoinFields {
    readonly ssid: string;
    readonly security: Security;
    readonly password: string;
    readonly userName: string;
    // WEP's key index, 0 to 3.
    readonly keyIndex: number;
    // Whether the name and security are fixed, which they are when the join
    // was started from a network in the list.
    readonly fixed: boolean;
}

export interface AddressFields {
    readonly automatic: boolean;
    readonly ip: string;
    readonly subnet: string;
    readonly gateway: string;
    readonly dns1: string;
    readonly dns2: string;
}

export interface WifiData {
    readonly screen: WifiScreen;
    // The radio, and what the user last asked it to be: the switch stays put
    // and disabled until the radio catches up.
    readonly radio: boolean;
    // undefined once the radio is where it was put.
    readonly radioWanted: boolean | undefined;
    readonly scanning: boolean;
    readonly networks: Network[];
    readonly status?: WifiStatus;
    // The line above the list: what the card has to say about where it is.
    readonly caption: string;
    readonly join?: JoinFields;
    // While a join is in flight, and what to say when it failed.
    readonly joining: boolean;
    readonly joinMessage: string;
    readonly address?: AddressFields;
    readonly addressProfile?: SavedProfile;
    readonly addressBusy: boolean;
    readonly known?: SavedProfile[];
    // A known-networks list that could not be read at all.
    readonly knownUnreadable: boolean;
    readonly sleep: string;
}

export interface WifiService {
    getState(): State<WifiData>;
    onStateChange(listener: (state: State<WifiData>) => void): Unsubscribe;
    onShown(params?: LaunchParams): void;
    onHidden(): void;
    onBack(): boolean;
    dispose(): void;

    // The list
    onRadio(on: boolean): void;
    onNetwork(ssid: string): void;
    onJoinOther(): void;
    // The join screen
    onJoinField(change: Partial<JoinFields>): void;
    onJoin(): void;
    onCancelJoin(): void;
    // The address screen
    onAddressField(change: Partial<AddressFields>): void;
    onSaveAddress(): void;
    onForget(): void;
    // Known networks and settings
    onOpenKnown(): void;
    onForgetKnown(profileId: number): void;
    onOpenSettings(): void;
    onSleep(mode: string): void;
    // The app menu
    onMenu(): void;
    onMenuChoice(value: string): void;
    readonly menuOpen: () => boolean;
}

// HP's, and the card's: the list rescans every twelve seconds.
export const SCAN_MS = 12_000;

// What a failed join says, by the name the service gives the failure. HP's
// words, kept.
export const joinFailureText = (error: string | undefined): string => {
    switch (error) {
    case "ApNotFound":
        return t("No network of that name with that security setting was found.");
    case "IncorrectPasskey":
        return t("The password you entered is not correct. Try again.");
    case "IncorrectPassword":
        return t("The username or password you entered is not correct. Try again.");
    case "ServerCertificateRequired":
        return t("You need a security certificate to join this network. Contact your network administrator.");
    default:
        return t("Unable to connect. Try again.");
    }
};

// Whether "Sign In" can be pressed: HP's rules for each security type, which
// are the ones the service will accept.
export const canJoin = (fields: JoinFields): boolean => {
    const { ssid, security, password, userName } = fields;
    if (!ssid || ssid.length > 32) {
        return false;
    }
    const hex = (text: string) => /^[0-9a-fA-F]+$/.test(text);
    switch (security) {
    case "none":
        return true;
    case "wep":
        return (password.length === 5 || password.length === 13) || (hex(password) && (password.length === 10 || password.length === 26));
    case "wpa-personal":
        return (password.length >= 8 && password.length <= 63) || (hex(password) && password.length === 64);
    case "enterprise":
        return userName.length > 0 && password.length > 0;
    }
};

// A dotted quad, as the address screen accepts one.
export const isAddress = (text: string): boolean =>
    /^(\d{1,3})\.(\d{1,3})\.(\d{1,3})\.(\d{1,3})$/.test(text)
    && text.split(".").every((part) => Number(part) <= 255);

// "Done" on the address screen: the address and the mask are needed, the rest
// are optional but have to be addresses if they are there.
export const canSaveAddress = (fields: AddressFields): boolean => {
    if (fields.automatic) {
        return true;
    }
    const optional = (text: string) => text === "" || isAddress(text);
    return isAddress(fields.ip) && isAddress(fields.subnet)
        && optional(fields.gateway) && optional(fields.dns1) && optional(fields.dns2);
};

// HP's: typing an address into an empty mask fills in the classful one.
export const maskFor = (ip: string): string => {
    const first = Number(ip.split(".")[0]);
    if (!isAddress(ip)) {
        return "";
    }
    return first < 128 ? "255.0.0.0" : first < 192 ? "255.255.0.0" : first < 224 ? "255.255.255.0" : "";
};

const blankJoin = (ssid = "", security: Security = "none", fixed = false): JoinFields =>
    ({ ssid, security, password: "", userName: "", keyIndex: 0, fixed });

export interface WifiDeps {
    readonly luna: LunaService;
    readonly setInterval: (callback: () => void, ms: number) => unknown;
    readonly clearInterval: (handle: unknown) => void;
    readonly log?: (message: string) => void;
}

export const createWifiService = (deps: WifiDeps): WifiService => {
    const wifi = createWifi(deps.luna);
    const connection = createConnectionManager(deps.luna);
    const screens = createNavigation<WifiScreen>("list");
    const state: StateHolder<WifiData> = createState<WifiData>({
        name: "wifi:list",
        data: {
            screen: "list", radio: true, radioWanted: undefined, scanning: false, networks: [], caption: "",
            joining: false, joinMessage: "", addressBusy: false, knownUnreadable: false, sleep: "",
        },
    });
    let gone = false;
    let scan: unknown;
    let menu = false;
    // A target naming the joined network can only be opened once the card
    // knows which network that is.
    let pending: { ssid: string; profileId?: number } | undefined;

    const show = (screen: WifiScreen) => {
        if (screens.now() !== screen) {
            screens.open(screen);
        }
        state.patch({ screen }, `wifi:${screen}`);
    };

    screens.onChange(() => state.patch({ screen: screens.now() }, `wifi:${screens.now()}`));

    const stopScanning = () => {
        deps.clearInterval(scan);
        scan = undefined;
    };

    const scanOnce = async () => {
        if (gone) {
            return;
        }
        try {
            const networks = await wifi.findNetworks();
            if (!gone) {
                state.patch({ networks, scanning: false });
            }
        } catch (error) {
            if (!gone) {
                // HP's library popped a blocking alert() here.
                state.patch({ scanning: false, caption: t("The network list could not be read.") });
                deps.log?.(error instanceof Error ? error.message : String(error));
            }
        }
    };

    const startScanning = () => {
        if (scan !== undefined || !state.get().data.radio) {
            return;
        }
        state.patch({ scanning: state.get().data.networks.length === 0 });
        void scanOnce();
        scan = deps.setInterval(() => void scanOnce(), SCAN_MS);
    };

    // What the line above the list says, which depends on where the card is.
    const captionFor = (data: WifiData): string => {
        if (data.screen === "join") {
            return data.join?.fixed ? t("Join #{name}", { name: data.join.ssid }) : t("Join Other Network");
        }
        if (data.screen === "address") {
            const status = data.status;
            if (!status || !status.ssid) {
                return "";
            }
            const where = t("Connected to #{name}.", { name: status.ssid });
            return status.bssid
                ? `${where} ${t("BSSID #{bssid}, Channel #{channel}.", { bssid: status.bssid, channel: status.channel })}`
                : where;
        }
        return "";
    };

    const retell = () => state.patch({ caption: captionFor(state.get().data) });

    const onStatus = (status: WifiStatus) => {
        if (gone) {
            return;
        }
        const data = state.get().data;
        const radioChanged = status.on !== data.radio;
        state.patch({
            status,
            radio: status.on,
            // The switch is usable again once the radio is where it was put.
            ...(data.radioWanted !== undefined && data.radioWanted === status.on ? { radioWanted: undefined } : {}),
        });

        if (!status.on) {
            stopScanning();
            state.patch({ networks: [], scanning: false });
            show("list");
            retell();
            return;
        }
        if (radioChanged) {
            startScanning();
        }

        // A join that was under way: what happened to it.
        if (data.screen === "join" && data.joining && status.ssid === data.join?.ssid) {
            if (joined(status.connectState)) {
                state.patch({ joining: false, joinMessage: "" });
                // As HP's did: a network that joined takes the card back to
                // the list, not to its address settings.
                show("list");
                void scanOnce();
            } else if (status.connectState === "associationFailed") {
                state.patch({ joining: false, joinMessage: joinFailureText(status.lastError) });
            }
        }

        // The list shows what the radio is doing, without waiting for the
        // next scan.
        const networks = state.get().data.networks;
        const at = networks.findIndex((network) => network.ssid === status.ssid);
        if (at >= 0 && status.ssid) {
            const updated: Network = {
                ...networks[at]!,
                ...(status.connectState ? { connectState: status.connectState } : {}),
                ...(status.lastError ? { lastError: status.lastError } : {}),
                ...(status.profileId ? { profileId: status.profileId } : {}),
            };
            // The one being joined goes to the top, as HP's list did.
            state.patch({ networks: [updated, ...networks.filter((_, i) => i !== at)] });
        }

        if (state.get().data.screen === "address") {
            retell();
        }
        openPendingTarget();
    };

    const watch = createWatch(() => wifi.watchStatus(onStatus, (error) => deps.log?.(error.message)));

    const openAddress = async (profileId: number) => {
        try {
            const { profile, address } = await wifi.profile(profileId);
            if (gone) {
                return;
            }
            state.patch({
                addressProfile: profile,
                address: {
                    automatic: !profile.staticIp,
                    ip: address?.ip ?? "", subnet: address?.subnet ?? "", gateway: address?.gateway ?? "",
                    dns1: address?.dns1 ?? "", dns2: address?.dns2 ?? "",
                },
            });
            show("address");
            retell();
        } catch (error) {
            state.patch({ caption: errorOf(error) });
        }
    };

    const openPendingTarget = () => {
        const target = pending;
        if (!target) {
            return;
        }
        // A target that names the profile can be opened at once; one that only
        // names the joined network waits until the card knows which that is.
        const profileId = target.profileId ?? state.get().data.status?.profileId;
        if (!profileId) {
            return;
        }
        pending = undefined;
        void openAddress(profileId);
    };

    const errorOf = (error: unknown): string =>
        error instanceof LunaCallError ? errorTextOf(error.reply)
            : error instanceof Error ? error.message : String(error);

    const readKnown = async () => {
        try {
            const known = await wifi.profiles();
            if (!gone) {
                state.patch({ known, knownUnreadable: false });
            }
        } catch (error) {
            if (!gone) {
                // "No known networks." is for a list that could not be read;
                // an empty one is an empty group, as on the phone.
                state.patch({ known: [], knownUnreadable: true });
                deps.log?.(errorOf(error));
            }
        }
    };

    const readSleep = async () => {
        try {
            const mode = await connection.wakeOnWifi();
            if (!gone) {
                state.patch({ sleep: mode });
            }
        } catch (error) {
            deps.log?.(errorOf(error));
        }
    };

    return {
        getState: state.get,
        onStateChange: state.subscribe,

        onShown: (params) => {
            watch.start();
            startScanning();
            const target = params?.target as { ssid?: string; securityType?: string; profileId?: number;
                                               connectState?: string } | undefined;
            if (!target?.ssid) {
                return;
            }
            if (target.connectState === "ipConfigured" || target.connectState === "ipFailed") {
                pending = { ssid: target.ssid, ...(target.profileId ? { profileId: target.profileId } : {}) };
                openPendingTarget();
                return;
            }
            if (target.securityType) {
                state.patch({ join: blankJoin(target.ssid, target.securityType as Security, true),
                              joinMessage: "", joining: false });
                show("join");
                retell();
            }
        },

        onHidden: () => {
            watch.stop();
            stopScanning();
        },

        onBack: () => {
            if (menu) {
                menu = false;
                state.patch({});
                return true;
            }
            if (screens.now() === "list") {
                return false;
            }
            screens.back();
            if (screens.now() === "list") {
                startScanning();
            }
            retell();
            return true;
        },

        onRadio: (on) => {
            state.patch({ radioWanted: on });
            void wifi.setRadio(on).catch((error: unknown) => {
                // The switch goes back where it was: the radio never moved.
                deps.log?.(errorOf(error));
                state.patch({ radioWanted: undefined });
            });
        },

        onNetwork: (ssid) => {
            const network = state.get().data.networks.find((one) => one.ssid === ssid);
            if (!network) {
                return;
            }
            // Joined, or joined and unable to get an address: its settings.
            if (joined(network.connectState) || network.connectState === "ipFailed") {
                const profileId = network.profileId ?? state.get().data.status?.profileId;
                if (profileId) {
                    void openAddress(profileId);
                }
                return;
            }
            // Remembered, and not just failed: join it without asking again.
            if (network.profileId && !network.lastError) {
                markJoining(ssid);
                void wifi.joinSaved(network.profileId).catch((error: unknown) => {
                    state.patch({ caption: errorOf(error) });
                });
                return;
            }
            if (network.security === "none") {
                markJoining(ssid);
                void wifi.join(ssid, "none", {}).catch((error: unknown) => {
                    state.patch({ caption: errorOf(error) });
                });
                return;
            }
            state.patch({ join: blankJoin(ssid, network.security, true), joinMessage: "", joining: false });
            show("join");
            retell();
        },

        onJoinOther: () => {
            stopScanning();
            state.patch({ join: blankJoin(), joinMessage: "", joining: false });
            show("join");
            retell();
        },

        onJoinField: (change) => {
            const join = state.get().data.join;
            if (!join) {
                return;
            }
            const next = { ...join, ...change };
            // HP's: an address typed with an empty mask fills the mask in --
            // the same idea here is that changing the security clears what
            // belonged to the one before.
            state.patch({ join: next, joinMessage: "" });
        },

        onJoin: () => {
            const join = state.get().data.join;
            if (!join || !canJoin(join)) {
                return;
            }
            stopScanning();
            state.patch({ joining: true, joinMessage: "" });
            void wifi.join(join.ssid, join.security, {
                password: join.password,
                userName: join.userName,
            }).catch((error: unknown) => {
                // A refusal is the service's own words, as the card before
                // this one showed them.
                state.patch({ joining: false, joinMessage: errorOf(error) });
            });
        },

        onCancelJoin: () => {
            state.patch({ joining: false, joinMessage: "" });
            screens.back();
            startScanning();
            retell();
        },

        onAddressField: (change) => {
            const address = state.get().data.address;
            if (!address) {
                return;
            }
            const next = { ...address, ...change };
            // Typing an address into an empty mask fills in the classful one.
            if (change.ip !== undefined && next.subnet === "") {
                state.patch({ address: { ...next, subnet: maskFor(change.ip) } });
                return;
            }
            state.patch({ address: next });
        },

        onSaveAddress: () => {
            const { address, addressProfile } = state.get().data;
            if (!address || !addressProfile || !canSaveAddress(address)) {
                return;
            }
            state.patch({ addressBusy: true });
            const wanted = address.automatic
                ? undefined
                : { ip: address.ip, subnet: address.subnet, gateway: address.gateway,
                    dns1: address.dns1, dns2: address.dns2 };
            void wifi.setAddress(addressProfile.profileId, wanted)
                .then(() => {
                    if (!gone) {
                        state.patch({ addressBusy: false });
                        screens.back();
                        startScanning();
                        retell();
                    }
                })
                .catch((error: unknown) => {
                    if (!gone) {
                        // Kept on screen until the card moves: what the service
                        // refused is the only thing that explains it.
                        state.patch({ addressBusy: false,
                                      caption: t("The address settings were not applied: #{why}",
                                                 { why: errorOf(error) }) });
                    }
                });
        },

        onForget: () => {
            const profile = state.get().data.addressProfile;
            if (!profile) {
                return;
            }
            void wifi.forget(profile.profileId).catch((error: unknown) => deps.log?.(errorOf(error)));
            screens.back();
            startScanning();
            void scanOnce();
            retell();
        },

        onOpenKnown: () => {
            show("known");
            void readKnown();
        },

        onForgetKnown: (profileId) => {
            void wifi.forget(profileId)
                .catch((error: unknown) => deps.log?.(errorOf(error)))
                // The list is read again after the delete has happened, not
                // alongside it.
                .finally(() => {
                    if (!gone && state.get().data.screen === "known") {
                        void readKnown();
                    }
                });
        },

        onOpenSettings: () => {
            show("settings");
            void readSleep();
        },

        onSleep: (mode) => {
            const before = state.get().data.sleep;
            state.patch({ sleep: mode });
            void connection.setWakeOnWifi(mode)
                .then((now) => {
                    if (!gone) {
                        state.patch({ sleep: now });
                    }
                })
                .catch((error: unknown) => {
                    // Refused: the list goes back to what is really stored.
                    deps.log?.(errorOf(error));
                    if (!gone) {
                        state.patch({ sleep: before });
                        void readSleep();
                    }
                });
        },

        onMenu: () => {
            menu = true;
            state.patch({});
        },

        onMenuChoice: (value) => {
            menu = false;
            if (value === "settings") {
                show("settings");
                void readSleep();
            } else if (value === "known") {
                show("known");
                void readKnown();
            }
            state.patch({});
        },

        menuOpen: () => menu,

        dispose: () => {
            gone = true;
            watch.stop();
            stopScanning();
            state.clear();
        },
    };

    function markJoining(ssid: string): void {
        const networks = state.get().data.networks.map((network) =>
            (network.ssid === ssid ? { ...network, connectState: "associating" as const } : network));
        state.patch({ networks });
    }
};
