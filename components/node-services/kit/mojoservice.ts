// What HP's mojoservice framework did around every command, for services whose
// callers still expect it (components/mojoservice-frameworks/mojoservice).
//
// * A failure is answered the way controller_command.js answered it:
//   `errorCode` from the error, or -9999 with "MojoService: no errorCode
//   supplied " before the text when the error had none. Foundations' Assert
//   throws plain Errors, so most failures of HP's services looked like that.
//   (mojoservice also sent the stack as `exception`; nothing read it, so it is
//   not sent.)
// * A command that sets no timeout gets mojoservice's 60 seconds.
// * A service exits after 5 idle seconds (its default activityTimeout).
// * `__quit` answers and exits 100 ms later, as controller_service.js's did:
//   exiting at once loses the answer, which is still on its way out.

import { isLunaError, lunaError, type Bus, type Handler, type LunaError, type MethodOptions, type Payload } from "./luna.ts";

export const DEFAULT_COMMAND_TIMEOUT = 60;
export const DEFAULT_IDLE_MS = 5000;

// An error carrying mojoservice's `errorCode`, as Foundations' Err.create made.
export const mojoError = (errorCode: number | string, message: string): LunaError => lunaError(errorCode, message);

export const mojoFailure = (error: unknown): LunaError => {
    if (isLunaError(error)) {
        return error;
    }
    const message = error instanceof Error ? error.message
        : error !== null && typeof error === "object" && "message" in error ? String(error.message)
        : String(error);
    const code = error !== null && typeof error === "object" && "errorCode" in error
        ? (error as { errorCode: unknown }).errorCode : undefined;
    if (typeof code === "number" || typeof code === "string") {
        return lunaError(code, message);
    }
    return lunaError(-9999, `MojoService: no errorCode supplied ${message}`);
};

// A plain (non-generator) handler with mojoservice's error replies.
export const mojoHandler = <P extends Payload>(handler: (request: Parameters<Handler<P>>[0]) => Payload | void | Promise<Payload | void>): Handler<P> =>
    async (request) => {
        try {
            return await handler(request);
        } catch (error) {
            throw mojoFailure(error);
        }
    };

export interface Command<P extends Payload = Payload> {
    readonly name: string;
    readonly handler: Handler<P>;
    // Registered on the public bus too, as services.json's "public": true did.
    readonly public?: boolean;
    // Seconds; mojoservice's default when left out.
    readonly timeout?: number;
}

export interface Buses {
    readonly private: Bus;
    readonly public: Bus;
}

export const QUIT_DELAY_MS = 100;

// Registers every command on the private bus, the public ones on the public
// bus as well, plus `__quit` on both.
export const registerCommands = (buses: Buses, commands: readonly Command[], quit: () => void,
                                 later: (callback: () => void, ms: number) => void = setTimeout): void => {
    for (const command of commands) {
        const options: MethodOptions = { timeout: command.timeout ?? DEFAULT_COMMAND_TIMEOUT };
        buses.private.method(command.name, command.handler, options);
        if (command.public) {
            buses.public.method(command.name, command.handler, options);
        }
    }
    const quitHandler: Handler = () => {
        later(quit, QUIT_DELAY_MS);
        return {};
    };
    buses.private.method("__quit", quitHandler);
    buses.public.method("__quit", quitHandler);
};
