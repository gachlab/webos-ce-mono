// com.palm.accountservices: the HP webOS (Palm profile) account.
//
// HP's service (never released) kept the profile's token in db8 and did the
// rest against HP's account servers, which are gone. So there is no webOS
// account, and the service says so the way HP's did when it had no token:
//
// * reading the local profile fails with NO_TOKEN;
// * anything about the account fails with ACCOUNT_NOT_DEFINED_ERROR, HP's
//   "Could not get local account info", which the Accounts app shows as "We
//   were unable to locate your account information";
// * anything that needed HP's servers to create or recover an account fails
//   with SERVER_UNAVAILABLE.
//
// updateDeviceProperties is real: HP's handed its arguments to
// com.palm.systemservice/setPreferences, and so does this. LunaSysMgr calls it
// at startup with {novacomEnabled}.
//
// When Backup (#28) and First use (#24) choose a backend, the account belongs
// here.

import type { Payload } from "#kit/luna.ts";
import { mojoError, mojoHandler, type Command } from "#kit/mojoservice.ts";

export const SYSTEM_SERVICE = "luna://com.palm.systemservice";

const noToken = () => mojoError("NO_TOKEN", "No webOS account token on this device");
const noAccount = () => mojoError("ACCOUNT_NOT_DEFINED_ERROR", "Could not get local account info");
const noServer = () => mojoError("SERVER_UNAVAILABLE", "The HP webOS account servers no longer exist");

const failing = (names: readonly string[], error: () => Error): Command[] =>
    names.map((name) => ({ name, handler: mojoHandler(() => { throw error(); }) }));

// The token and the local profile record.
export const TOKEN_METHODS = ["getAccountToken", "getLocalProfileProperties", "setLocalProfileProperties"] as const;

// About the account that would have been signed in.
export const ACCOUNT_METHODS = [
    "getAccountInfo", "updateAccountInfo", "getAggregatedAccountInfo", "changePassword", "changeAccountPassword",
    "changeEmail", "requestResendVerificationEmail", "updateChallengeQuestion", "getAccountSecurityQuestion",
    "isDeviceInUse", "dissociateCurrentDevice", "assignDeviceName", "assignDeviceNameNoAcctInfoArgs",
    "postLoginSettings", "refreshJabberInfo", "getWaitPeriods",
] as const;

// What only HP's servers could answer.
export const SERVER_METHODS = [
    "getServerUrl", "getURLForTerms", "getTermsAndConditions", "isEmailAvailable", "createNovaAccount",
    "authenticateAccount", "isUserValid", "authenticateAccountFromSecurityQuestion", "requestPasswordResetEmail",
    "getAllSecurityQuestions", "setTimeZoneFromIP",
] as const;

export interface AccountServicesDeps {
    readonly call: (uri: string, payload: Payload) => Promise<Payload>;
    readonly log: (message: string) => void;
}

export const accountServicesCommands = (deps: AccountServicesDeps): Command[] => [
    ...failing(TOKEN_METHODS, noToken),
    ...failing(ACCOUNT_METHODS, noAccount),
    ...failing(SERVER_METHODS, noServer),
    {
        name: "updateDeviceProperties",
        handler: mojoHandler(async ({ payload }) => {
            const { subscribe: _subscribe, ...properties } = payload;
            if (Object.keys(properties).length === 0) {
                throw mojoError("INVALID_REQUEST", "No input params");
            }
            try {
                await deps.call(`${SYSTEM_SERVICE}/setPreferences`, properties);
            } catch (error) {
                deps.log(`updateDeviceProperties: ${String(error)}`);
                throw mojoError("UPDATE_ERROR", "Could not update the device properties");
            }
        }),
    },
    // No account servers, so nothing to configure or to be told.
    { name: "getPreferences", handler: mojoHandler(() => ({})) },
    { name: "processMessage", handler: mojoHandler(() => undefined) },
    { name: "notifyAuthenticationFailure", handler: mojoHandler(() => undefined) },
];
