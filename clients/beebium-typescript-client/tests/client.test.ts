import { describe, it, expect, vi } from "vitest";
import { Beebium } from "../src/client.js";

// We cannot use Beebium.connect() or Beebium.launch() without a real server,
// so we test the lazy property pattern by constructing via internal means.
// The constructor is private, so we test what we can through the public API.

describe("Beebium", () => {
    describe("connect", () => {
        // We cannot fully test connect() without a server, but we can verify
        // the static method exists and has the expected signature.
        it("is a static async method", () => {
            expect(typeof Beebium.connect).toBe("function");
        });
    });

    describe("launch", () => {
        it("is a static async method", () => {
            expect(typeof Beebium.launch).toBe("function");
        });
    });

    describe("lazy property accessors", () => {
        // Since the constructor is private, we verify the class shape by
        // checking that property descriptors exist on the prototype.
        const proto = Beebium.prototype;
        const descriptors = Object.getOwnPropertyDescriptors(proto);

        const expectedGetters = [
            "debugger",
            "cpu",
            "memory",
            "keyboard",
            "video",
            "system",
            "disc",
            "econet",
            "tube",
            "systemVia",
            "userVia",
            "crtc",
            "videoUla",
            "addressableLatch",
            "sound",
            "tubeUla",
            "basic",
            "aun",
            "piconet",
            "transport",
            "extensionUi",
            "target",
        ];

        for (const name of expectedGetters) {
            it(`has getter '${name}'`, () => {
                expect(descriptors[name]).toBeDefined();
                expect(typeof descriptors[name]!.get).toBe("function");
            });
        }
    });

    describe("close", () => {
        it("is an async method on the prototype", () => {
            expect(typeof Beebium.prototype.close).toBe("function");
        });
    });

    describe("async dispose", () => {
        it("has Symbol.asyncDispose method", () => {
            expect(typeof Beebium.prototype[Symbol.asyncDispose]).toBe("function");
        });
    });
});
