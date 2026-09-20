// The Certificate Manager settings card.

import { openBus } from "@webos/api/infra/luna/open-bus.ts";
import { t } from "@webos/api/i18n/translate.ts";
import { html } from "@webos/ui-kit/element.ts";
import { error as errorLine, note } from "@webos/ui-kit/kit/kit.ts";
import { startCard } from "@webos/ui-kit/start-card.ts";
import {
    createCertificateService, type CertificateData, type CertificateService,
} from "./certificate.service.ts";
import type { Certificate } from "./luna/certificate.ts";
import type { State } from "@webos/api/helpers/create-state.ts";

const list = (data: CertificateData, service: CertificateService) => html`
    ${data.certificates.length === 0
        ? note(t("Your certificate list is empty."))
        : ""}
    <div class="wos-group">
        <div class="wos-group-title">${t("Security certificates")}</div>
        <div class="wos-list">
            ${data.certificates.map((cert: Certificate) => {
                const open = data.swipeOpen === cert.certificateId;
                return html`
                    <wos-swipe-row
                        title=${cert.commonName}
                        detail=${cert.issuerName}
                        confirm=${t("Delete")}
                        ?open=${open}
                        @select=${() => service.onOpenDetails(cert.certificateId)}
                        @open=${(e: CustomEvent<{ open: boolean }>) =>
                            service.onSwipe(cert.certificateId, e.detail.open)}
                        @remove=${() => service.onDelete(cert.certificateId)}>
                    </wos-swipe-row>`;
            })}
            <wos-row title=${t("Add certificate...")}
                @select=${() => service.onOpenAdd()}></wos-row>
        </div>
    </div>
    ${data.message ? errorLine(data.message) : ""}
    ${data.busy ? html`<wos-spinner label=${t("Loading...")}></wos-spinner>` : ""}
`;

const details = (cert: Certificate, data: CertificateData, service: CertificateService) => html`
    <div class="wos-group">
        <div class="wos-group-title">${t("Certificate")}</div>
        <div class="wos-list">
            <wos-row title=${cert.commonName} detail=${t("Name")}></wos-row>
            ${cert.issuerName
                ? html`<wos-row title=${cert.issuerName} detail=${t("Issued by")}></wos-row>`
                : ""}
            ${cert.certificateFilename
                ? html`<wos-row title=${cert.certificateFilename} detail=${t("File")}></wos-row>`
                : ""}
        </div>
    </div>
    ${data.message ? errorLine(data.message) : ""}
    <div class="wos-group">
        <wos-button label=${t("Delete Certificate")} kind="negative"
            ?disabled=${data.busy}
            @press=${() => service.onDelete(cert.certificateId)}></wos-button>
    </div>
`;

const add = (data: CertificateData, service: CertificateService) => html`
    <div class="wos-group">
        <div class="wos-group-title">${t("Certificate file")}</div>
        <div class="wos-list">
            <wos-field label="" value=${data.path}
                placeholder=${t("certificateFilename path")}
                @change=${(e: CustomEvent<{ value: string }>) =>
                    service.onAddField({ path: e.detail.value })}></wos-field>
        </div>
    </div>
    <div class="wos-group">
        <div class="wos-group-title">${t("Passphrase (optional)")}</div>
        <div class="wos-list">
            <wos-field label="" value=${data.passphrase} type="password"
                placeholder=${t("Passphrase")}
                @change=${(e: CustomEvent<{ value: string }>) =>
                    service.onAddField({ passphrase: e.detail.value })}
                @done=${() => service.onTrust()}></wos-field>
        </div>
    </div>
    ${data.message ? errorLine(data.message) : ""}
`;

const headerTitle = (screen: CertificateData["screen"]): string => {
    switch (screen) {
    case "add": return t("Trust security certificate?");
    case "details": return t("Certificate details");
    default: return t("Certificate Manager");
    }
};

const view = (state: State<CertificateData>, service: CertificateService) => {
    const data = state.data;
    const onList = data.screen === "list";
    const foot = data.screen === "add";
    const body = data.screen === "add"
        ? add(data, service)
        : data.screen === "details" && data.details
            ? details(data.details, data, service)
            : list(data, service);
    return html`
        <div class="wos-card ${foot ? "certificate-has-footer" : ""}">
            <wos-header title=${headerTitle(data.screen)} light
                       icon="header-icon-certificate.png"
                       ?back=${!onList} @back=${() => service.onBack()}></wos-header>
            <div class="wos-body">${body}</div>
            ${foot ? html`
                <div class="certificate-footer">
                    <wos-button class="certificate-wide" label=${t("Cancel")}
                        @press=${() => service.onCancelAdd()}></wos-button>
                    <wos-activity-button class="certificate-wide" label=${t("Trust")}
                        kind="affirmative" ?busy=${data.busy}
                        @press=${() => service.onTrust()}></wos-activity-button>
                </div>` : ""}
            <wos-app-menu ?open=${data.menuOpen}
                .items=${[{ value: "help", label: t("Help") }]}
                @close=${() => service.onMenuChoice("")}
                @choose=${(e: CustomEvent<{ value: string }>) =>
                    service.onMenuChoice(e.detail.value)}></wos-app-menu>
        </div>
    `;
};

const luna = openBus();
const service = createCertificateService(luna);
const { app } = startCard({ service, view });
app.on("menu", () => service.onMenu());
