// getReverseLocation's address, from OpenStreetMap's Nominatim. HP's came from
// Google, in these fields: `address` with its lines separated by ';', and the
// parts callers pick from it.

import type { Payload } from "#kit/luna.ts";

export const NOMINATIM_URL = "https://nominatim.openstreetmap.org/reverse";

interface NominatimAddress {
    readonly house_number?: string;
    readonly road?: string;
    readonly neighbourhood?: string;
    readonly suburb?: string;
    readonly city?: string;
    readonly town?: string;
    readonly village?: string;
    readonly state?: string;
    readonly postcode?: string;
    readonly country?: string;
}

const joined = (parts: readonly (string | undefined)[], separator: string) =>
    parts.filter((part): part is string => Boolean(part)).join(separator);

export const addressFromNominatim = (answer: { address?: NominatimAddress }): Payload | undefined => {
    const a = answer.address;
    if (!a) {
        return undefined;
    }
    const street = joined([a.house_number, a.road], " ");
    const substreet = a.neighbourhood ?? a.suburb ?? "";
    const city = a.city ?? a.town ?? a.village;
    const lines = [street, joined([city, joined([a.state, a.postcode], " ")], ", "), a.country ?? ""];
    const address = joined(lines, ";");
    if (!address) {
        return undefined;
    }
    return {
        address,
        street,
        substreet,
        zipcode: a.postcode ?? "",
        country: a.country ?? "",
    };
};

type Fetch = (url: string, init: RequestInit) => Promise<Response>;

// Nominatim asks every application to say who it is.
export const createReverse = (deps: { fetch: Fetch; userAgent: string; language: () => string; url?: string }) =>
    async (latitude: number, longitude: number, signal: AbortSignal): Promise<Payload | undefined> => {
        const url = new URL(deps.url ?? NOMINATIM_URL);
        url.search = new URLSearchParams({
            format: "jsonv2",
            lat: String(latitude),
            lon: String(longitude),
            addressdetails: "1",
        }).toString();
        const response = await deps.fetch(url.toString(), {
            headers: { "user-agent": deps.userAgent, "accept-language": deps.language() },
            signal,
        });
        if (!response.ok) {
            throw new Error(`${url.origin} answered ${response.status}`);
        }
        return addressFromNominatim(await response.json() as { address?: NominatimAddress });
    };
