// com.palm.certificatemanager — list / add / delete / details.

import type { LunaService, Payload } from "@webos/api/infra/luna/service.ts";

export interface Certificate {
    readonly certificateId: string;
    readonly commonName: string;
    readonly issuerName: string;
    readonly certificateFilename?: string | undefined;
}

export interface CertificateClient {
    list(): Promise<Certificate[]>;
    details(certificateId: string): Promise<Certificate>;
    add(certificateFilename: string, passphrase?: string): Promise<void>;
    remove(certificateId: string): Promise<void>;
    openTarget(target: string): Promise<void>;
}

const text = (value: unknown): string =>
    (typeof value === "string" ? value
        : typeof value === "number" && Number.isFinite(value) ? String(value)
        : "");

export const certificateOf = (raw: Payload): Certificate => {
    const subject = (raw.subject as Payload | undefined) ?? {};
    const issuer = (raw.issuer as Payload | undefined) ?? {};
    const commonName = text(raw.commonname) || text(raw.commonName)
        || text(subject.commonname) || text(subject.organization)
        || text(raw.organization) || text(raw.certificateFilename)
        || "Certificate";
    const issuerName = text(raw.issuerCommonName) || text(raw.issuerName)
        || text(issuer.commonname) || text(issuer.organization) || text(raw.issuer);
    const certificateFilename = text(raw.certificateFilename);
    return {
        certificateId: text(raw.certificateId) || text(raw.id),
        commonName,
        issuerName,
        ...(certificateFilename ? { certificateFilename } : {}),
    };
};

export const createCertificate = (luna: LunaService): CertificateClient => {
    const base = "luna://com.palm.certificatemanager/";
    const apps = "luna://com.palm.applicationManager/";

    return {
        async list() {
            const reply = await luna.call(`${base}listcertificates`, { category: "user" });
            const store = Array.isArray(reply.userCertificateStore)
                ? reply.userCertificateStore : [];
            return store.map((item) => certificateOf(item as Payload));
        },

        async details(certificateId) {
            const id = Number(certificateId);
            try {
                const reply = await luna.call(`${base}getcertificatedetails`, {
                    certificateId: Number.isFinite(id) ? id : certificateId,
                });
                return certificateOf(reply);
            } catch {
                const all = await this.list();
                const found = all.find((c) => c.certificateId === certificateId);
                if (!found)
                    throw new Error("Certificate not found");
                return found;
            }
        },

        async add(certificateFilename, passphrase) {
            const body: Payload = { certificateFilename };
            if (passphrase)
                body.passphrase = passphrase;
            await luna.call(`${base}addcertificate`, body);
        },

        async remove(certificateId) {
            const id = Number(certificateId);
            await luna.call(`${base}deletecertificate`, {
                certificateId: Number.isFinite(id) ? id : certificateId,
            });
        },

        async openTarget(target) {
            await luna.call(`${apps}open`, { target });
        },
    };
};
