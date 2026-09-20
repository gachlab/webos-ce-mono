// The Help settings card.

import { openBus } from "@webos/api/infra/luna/open-bus.ts";
import { t } from "@webos/api/i18n/translate.ts";
import { html } from "@webos/ui-kit/element.ts";
import { error as errorLine, note } from "@webos/ui-kit/kit/kit.ts";
import { startCard } from "@webos/ui-kit/start-card.ts";
import { createHelpService, type HelpData, type HelpService } from "./help.service.ts";
import type { State } from "@webos/api/helpers/create-state.ts";

const view = (state: State<HelpData>, service: HelpService) => {
    const data = state.data;
    return html`
        <div class="wos-card">
            <wos-header title=${t("Help")} light icon="header-icon-help.png"></wos-header>
            <div class="wos-body">
                ${note(t("Opens help topics in the browser."))}
                <div class="wos-group">
                    <div class="wos-group-title">${t("Topics")}</div>
                    <div class="wos-list">
                        ${data.topics.map((topic) => html`
                            <wos-row title=${t(topic.title)} detail=${t(topic.detail)}
                                @select=${() => service.onOpenTopic(topic.id)}></wos-row>
                        `)}
                    </div>
                </div>
                ${data.message ? errorLine(data.message) : ""}
                ${data.busy ? html`<wos-spinner label=${t("Opening...")}></wos-spinner>` : ""}
            </div>
            <wos-app-menu ?open=${data.menuOpen}
                .items=${[{ value: "help", label: t("Help") }]}
                @close=${() => service.onMenuChoice("")}
                @choose=${(e: CustomEvent<{ value: string }>) =>
                    service.onMenuChoice(e.detail.value)}></wos-app-menu>
        </div>
    `;
};

const luna = openBus();
const service = createHelpService(luna);
const { app } = startCard({ service, view });
app.on("menu", () => service.onMenu());
