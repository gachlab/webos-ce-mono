// Certificate Manager against a fake com.palm.certificatemanager.

import assert from "node:assert/strict";
import { describe, test } from "node:test";

import { createFakeLuna, type FakeLuna } from "@webos/api/infra/luna/fake.service.ts";
import { createCertificateService } from "../src/certificate.service.ts";
import { certificateOf } from "../src/luna/certificate.ts";
import type { Payload } from "@webos/api/infra/luna/service.ts";

const CERT = "luna://com.palm.certificatemanager/";
const LIST = `${CERT}listcertificates`;
const DETAILS = `${CERT}getcertificatedetails`;
const ADD = `${CERT}addcertificate`;
const DELETE = `${CERT}deletecertificate`;

const settle = () => new Promise((resolve) => setImmediate(resolve));

const payloads = (luna: FakeLuna, uri: string): Payload[] =>
    luna.calls.filter((call) => call.uri === uri).map((call) => call.payload);

const setup = () => {
    const luna = createFakeLuna();
    luna.answer(LIST, () => ({
        returnValue: true,
        userCertificateStore: [
            {
                certificateId: "cert-1",
                commonname: "Work CA",
                issuerCommonName: "Example Issuer",
            },
        ],
    }));
    luna.answer(DETAILS, () => ({
        returnValue: true,
        certificateId: "cert-1",
        commonname: "Work CA",
        issuerCommonName: "Example Issuer",
    }));
    luna.answer(ADD, () => ({ returnValue: true }));
    luna.answer(DELETE, () => ({ returnValue: true }));
    const service = createCertificateService(luna);
    return { luna, service, data: () => service.getState().data };
};

describe("certificateOf", () => {
    test("prefers HP commonname / issuerCommonName fields", () => {
        const cert = certificateOf({
            certificateId: "a",
            commonname: "CN",
            issuerCommonName: "Issuer",
        });
        assert.equal(cert.commonName, "CN");
        assert.equal(cert.issuerName, "Issuer");
        assert.equal(cert.certificateId, "a");
        // Mutation: organization alone is a commonName fallback, not issuer.
        const org = certificateOf({
            certificateId: "2",
            commonname: "Laptop Wi-Fi",
            organization: "Example Corp",
        });
        assert.equal(org.commonName, "Laptop Wi-Fi");
        assert.notEqual(org.commonName, "Example Corp");
    });
});

describe("the certificate list", () => {
    test("loads userCertificateStore on shown", async () => {
        const { luna, service, data } = setup();
        service.onShown();
        await settle();
        assert.equal(data().screen, "list");
        assert.equal(data().certificates.length, 1);
        assert.equal(data().certificates[0]?.commonName, "Work CA");
        assert.equal(payloads(luna, LIST)[0]?.category, "user");
    });

    test("opening add then Trust requires a path and calls addcertificate", async () => {
        const { luna, service, data } = setup();
        service.onShown();
        await settle();
        service.onOpenAdd();
        assert.equal(data().screen, "add");
        service.onTrust();
        assert.match(data().message, /path/i);
        assert.equal(payloads(luna, ADD).length, 0);

        service.onAddField({ path: "/media/internal/work.pem", passphrase: "secret" });
        service.onTrust();
        await settle();
        assert.equal(payloads(luna, ADD).length, 1);
        assert.equal(payloads(luna, ADD)[0]?.certificateFilename, "/media/internal/work.pem");
        assert.equal(payloads(luna, ADD)[0]?.passphrase, "secret");
        assert.equal(data().screen, "list");
    });

    test("details then delete calls deletecertificate", async () => {
        const { luna, service, data } = setup();
        service.onShown();
        await settle();
        service.onOpenDetails("cert-1");
        await settle();
        assert.equal(data().screen, "details");
        assert.equal(data().details?.commonName, "Work CA");
        service.onDelete("cert-1");
        await settle();
        assert.equal(payloads(luna, DELETE)[0]?.certificateId, "cert-1");
        assert.equal(data().screen, "list");
    });
});
