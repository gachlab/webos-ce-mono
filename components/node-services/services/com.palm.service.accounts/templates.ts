// Account templates: every "*.json" one directory below each template root,
// each read in its localized version, validated, and sorted by name.
//
// Rewritten from HP's accounts.js and models/template-model.js. The localized
// lookup is Globalization.ResourceBundle's (loadable-frameworks/globalization).

import { join } from "node:path";

import { validate, type Schema } from "#kit/json-schema.ts";
import type { Template } from "./accounts.ts";

export const TEMPLATE_ROOTS = ["/usr/palm/public/accounts", "/media/cryptofs/apps/usr/palm/accounts"];

export interface TemplateFiles {
    // Names in a directory, or [] when it cannot be read.
    readonly list: (dir: string) => string[];
    readonly isDirectory: (path: string) => boolean;
    readonly isFile: (path: string) => boolean;
    // The file's text, or undefined when there is none.
    readonly read: (path: string) => string | undefined;
}

export interface TemplateDeps {
    readonly files: TemplateFiles;
    readonly roots: readonly string[];
    readonly fileSchema: Schema;
    readonly templateSchema: Schema;
    readonly log: (message: string) => void;
}

export interface Locale {
    readonly language: string;
    readonly region?: string;
    readonly carrier?: string;
    // "en_us", as Globalization.Locale.parseLocaleString gives it.
    readonly locale: string;
}

export const parseLocale = (locale: string): Locale => ({
    language: locale.slice(0, 2),
    ...(locale.length >= 5 ? { region: locale.slice(3, 5) } : {}),
    ...(locale.length >= 7 ? { carrier: locale.slice(6) } : {}),
    locale: locale.length >= 7 ? `${locale.slice(0, 5)}_${locale.slice(6)}` : locale.slice(0, 5),
});

// The first of: resources/<lang>/<region>/<carrier>, resources/<lang>/<region>,
// resources/<lang>, resources/<locale>, resources/en, and the file itself.
export const localizedCandidates = (root: string, file: string, locale: Locale): string[] => {
    const resources = join(root, "resources");
    const language = join(resources, locale.language);
    const region = join(language, locale.region ?? "");
    return [
        ...(locale.carrier ? [join(region, locale.carrier, file)] : []),
        join(region, file),
        join(language, file),
        join(resources, locale.locale, file),
        join(resources, "en", file),
        join(root, file),
    ];
};

const readLocalized = (files: TemplateFiles, root: string, file: string, locale: Locale): string | undefined => {
    for (const candidate of localizedCandidates(root, file, locale)) {
        const text = files.read(candidate);
        if (text !== undefined) {
            return text;
        }
    }
    return undefined;
};

const templatePaths = (deps: TemplateDeps) =>
    deps.roots.flatMap((root) => deps.files.list(root)
        .map((dir) => join(root, dir))
        .filter((dir) => deps.files.isDirectory(dir))
        .flatMap((dir) => deps.files.list(dir)
            .filter((file) => /\.json$/i.test(file) && deps.files.isFile(join(dir, file)))
            .map((file) => ({ dir, file }))));

const localeAllows = (template: Payload, locale: string): boolean => {
    const allowed = template.allowed_locales as string[] | undefined;
    const disallowed = template.disallowed_locales as string[] | undefined;
    return !(allowed && !allowed.includes(locale)) && !(disallowed && disallowed.includes(locale));
};

type Payload = Record<string, unknown>;

const absolutize = (prefix: string, icons: unknown): unknown =>
    icons !== null && typeof icons === "object"
        ? Object.fromEntries(Object.entries(icons).map(([size, path]) => [size, prefix + String(path)]))
        : icons;

// One template as the service keeps it: validated, with icon paths made
// absolute. Throws on a template that does not match the schema.
export const makeTemplate = (raw: Payload, dir: string, schema: Schema): Template => {
    const result = validate(raw, schema);
    if (!result.valid) {
        throw new Error(`validation errors in template: ${String(raw.templateId)} errors=${JSON.stringify(result.errors)}`);
    }
    const prefix = dir.endsWith("/") ? dir : `${dir}/`;
    const template = { ...raw } as Template;
    if (template.icon !== undefined) {
        template.icon = absolutize(prefix, template.icon);
    }
    if (template.capabilityProviders) {
        template.capabilityProviders = template.capabilityProviders.map((provider) =>
            provider.icon === undefined ? provider : { ...provider, icon: absolutize(prefix, provider.icon) });
    }
    return template;
};

const byName = (a: Template, b: Template) =>
    (a.loc_name ?? "").toLocaleUpperCase().localeCompare((b.loc_name ?? "").toLocaleUpperCase());

export const createTemplateLoader = (deps: TemplateDeps) => (localeString: string): Template[] => {
    const locale = parseLocale(localeString);
    const found = new Map<string, Template>();
    const paths = templatePaths(deps);
    deps.log(`Found ${paths.length} account templates`);
    for (const { dir, file } of paths) {
        try {
            const text = readLocalized(deps.files, dir, file, locale);
            if (text === undefined) {
                throw new Error("no readable version of the file");
            }
            const parsed: unknown = JSON.parse(text);
            // A bare template, or (the older form) an array of them.
            const templates = Array.isArray(parsed) ? parsed : [parsed];
            const result = validate(templates, deps.fileSchema);
            if (!result.valid) {
                throw new Error(`failed validation: ${JSON.stringify(result.errors)}`);
            }
            for (const raw of templates as Payload[]) {
                if (!localeAllows(raw, locale.locale)) {
                    deps.log(`Ignoring template ${String(raw.templateId)}: not for locale ${locale.locale}`);
                    continue;
                }
                const template = makeTemplate(raw, dir, deps.templateSchema);
                if (found.has(template.templateId)) {
                    deps.log(`*** WARNING: ignoring duplicate account template: ${template.templateId}`);
                } else {
                    found.set(template.templateId, template);
                }
            }
        } catch (error) {
            deps.log(`Parse error for file: '${join(dir, file)}': ${error instanceof Error ? error.message : String(error)}`);
        }
    }
    return [...found.values()].sort(byName);
};
