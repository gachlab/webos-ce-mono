// com.palm.deviceprofile: who this device is.
//
// HP's service (never released) answered getDeviceProfile with {deviceInfo}
// built from the modem, the ROM's properties and /etc/palm-build-info, and
// getDeviceId with the modem's id or, lacking one, the nduId. Its callers here
// are the Accounts app's palmID screens (deviceInfo.nduId); HP's First use, App
// Catalog, Backup and Help read the same fields.
//
// There is no modem and no ROM: those fields are empty, as HP's were when a
// query failed. The rest come from the host and from this build.

import type { Payload } from "#kit/luna.ts";

export interface Facts {
    // A stable id for this install, in the form of HP's (40 hex digits).
    readonly nduId: string;
    // The host's DMI product name and version.
    readonly model: string;
    readonly hardwareVersion: string;
    // The CPU architecture, as `uname -m` names it.
    readonly machine: string;
    // /etc/palm/palm-build-info, as KEY=VALUE lines.
    readonly buildInfo: string;
}

export const parseBuildInfo = (text: string): Record<string, string> => {
    const values: Record<string, string> = {};
    for (const line of text.split("\n")) {
        const at = line.indexOf("=");
        if (at > 0) {
            values[line.slice(0, at).trim()] = line.slice(at + 1).trim();
        }
    }
    return values;
};

// Every field HP's device-info model reported, in its order.
export const deviceInfo = (facts: Facts): Payload => {
    const build = parseBuildInfo(facts.buildInfo);
    const softwareVersion = build.BUILDNAME && build.BUILDNUMBER ? `${build.BUILDNAME}-${build.BUILDNUMBER}` : "";
    return {
        deviceId: "",
        nduId: facts.nduId,
        phoneNumber: "",
        carrier: "",
        carrierROM: softwareVersion,
        network: "",
        dataNetwork: "",
        firmwareVersion: "",
        buildTime: build.BUILDTIME ?? "",
        softwareVersion,
        softwareBuildBranch: build.PRODUCT_VERSION_STRING ?? "",
        swUpdateTarget: "",
        deviceModel: facts.model,
        hardwareType: facts.machine,
        hardwareVersion: facts.hardwareVersion,
        platform: "Nova",
        platformVersion: "3.0.5",
        serialNumber: "",
        HPSerialNumber: "",
        productSku: "",
        WIFIoADDR: "",
        BToADDR: "",
        dmSets: "",
        serverAuthType: "",
        serverNonce: "",
        serverPwd: "",
        clientNonce: "",
        clientPwd: "",
        clientCredential: "",
        homeMcc: "",
        homeMnc: "",
        currentMcc: "",
        currentMnc: "",
    };
};

// HP's getDeviceId: the modem's id, or the nduId when there is none.
export const deviceIdOf = (info: Payload): string => {
    const id = typeof info.deviceId === "string" ? info.deviceId : "";
    return id || String(info.nduId ?? "");
};

// A new nduId: 20 random bytes in hex, the length of HP's.
export const newNduId = (random: (bytes: number) => Uint8Array): string =>
    Array.from(random(20), (byte) => byte.toString(16).padStart(2, "0")).join("");
