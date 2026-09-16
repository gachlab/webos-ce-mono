// The JSON Schema dialect HP's services were written against: the early drafts
// Foundations' `Json.Schema.validate` implemented, where a property is
// required unless it says `"optional": true`.
//
// Only what those schemas use: type (a name, "any", or a list of names),
// properties, optional, items, additionalProperties and enum. A schema using
// anything else is refused, rather than silently passing everything.

export type Schema = {
    readonly type?: string | readonly string[];
    readonly properties?: Readonly<Record<string, Schema>>;
    readonly optional?: boolean;
    readonly items?: Schema;
    readonly additionalProperties?: boolean | Schema;
    readonly enum?: readonly unknown[];
    readonly description?: string;
};

export interface SchemaError {
    readonly property: string;
    readonly message: string;
}

export interface Validation {
    readonly valid: boolean;
    readonly errors: readonly SchemaError[];
}

const KNOWN = new Set(["type", "properties", "optional", "items", "additionalProperties", "enum", "description"]);

const typeOf = (value: unknown): string => {
    if (value === null) {
        return "null";
    }
    if (Array.isArray(value)) {
        return "array";
    }
    return typeof value;
};

const matchesType = (value: unknown, type: string): boolean => {
    switch (type) {
        case "any":
            return true;
        case "integer":
            return Number.isInteger(value);
        case "number":
            return typeof value === "number";
        default:
            return typeOf(value) === type;
    }
};

const check = (value: unknown, schema: Schema, path: string, errors: SchemaError[]): void => {
    for (const key of Object.keys(schema)) {
        if (!KNOWN.has(key)) {
            throw new Error(`unsupported schema keyword "${key}" at ${path || "the root"}`);
        }
    }
    if (schema.type !== undefined) {
        const types = typeof schema.type === "string" ? [schema.type] : schema.type;
        if (!types.some((type) => matchesType(value, type))) {
            errors.push({ property: path, message: `${typeOf(value)} value found, but a ${types.join(" or ")} is required` });
            return;
        }
    }
    if (schema.enum !== undefined && !schema.enum.some((option) => option === value)) {
        errors.push({ property: path, message: "does not have a value in the enumeration" });
    }
    if (schema.items !== undefined && Array.isArray(value)) {
        value.forEach((item, index) => check(item, schema.items!, `${path}[${index}]`, errors));
    }
    if (value !== null && typeof value === "object" && !Array.isArray(value)) {
        const object = value as Record<string, unknown>;
        const properties = schema.properties ?? {};
        for (const [name, property] of Object.entries(properties)) {
            const at = path ? `${path}.${name}` : name;
            if (object[name] === undefined) {
                if (!property.optional) {
                    errors.push({ property: at, message: "is missing and it is not optional" });
                }
            } else {
                check(object[name], property, at, errors);
            }
        }
        const extra = schema.additionalProperties;
        if (extra !== undefined && extra !== true) {
            for (const name of Object.keys(object).filter((key) => !(key in properties))) {
                const at = path ? `${path}.${name}` : name;
                if (extra === false) {
                    errors.push({ property: at, message: "is not defined in the schema and the schema does not allow additional properties" });
                } else {
                    check(object[name], extra, at, errors);
                }
            }
        }
    }
};

export const validate = (value: unknown, schema: Schema): Validation => {
    const errors: SchemaError[] = [];
    check(value, schema, "", errors);
    return { valid: errors.length === 0, errors };
};
