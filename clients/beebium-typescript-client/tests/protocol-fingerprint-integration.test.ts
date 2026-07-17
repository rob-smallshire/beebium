/**
 * Protocol fingerprint handshake test.
 *
 * Beebium.launch / Beebium.connect verify the server's protocol fingerprint
 * against the client's compiled-in PROTOCOL_FINGERPRINT and reject a mismatch.
 * Here we confirm a matched server connects and reports the same fingerprint.
 */

import { describe, it, expect, afterEach } from "vitest";
import { Beebium, PROTOCOL_FINGERPRINT } from "../src/index.js";

describe("protocol fingerprint", () => {
    let bbc: Beebium | undefined;

    afterEach(async () => {
        await bbc?.close();
        bbc = undefined;
    });

    it("a matched server connects and reports the client's fingerprint", async () => {
        // launch() succeeding already means the handshake passed; assert the
        // reported value too.
        bbc = await Beebium.launch({ model: "B" });
        expect(await bbc.system.getProtocolFingerprint()).toBe(PROTOCOL_FINGERPRINT);
    });
});
