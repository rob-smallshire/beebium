import { describe, it, expect } from "vitest";
import {
    BeebiumError,
    ConnectionError,
    ServerStartupError,
    ServerNotFoundError,
    DebuggerError,
    MemoryAccessError,
    TimeoutError,
    DiscError,
    EconetError,
} from "../src/exceptions.js";

describe("BeebiumError", () => {
    it("is an instance of Error", () => {
        const err = new BeebiumError("test");
        expect(err).toBeInstanceOf(Error);
    });

    it("has name 'BeebiumError'", () => {
        const err = new BeebiumError("test");
        expect(err.name).toBe("BeebiumError");
    });

    it("preserves the message", () => {
        const err = new BeebiumError("something went wrong");
        expect(err.message).toBe("something went wrong");
    });
});

const subclasses = [
    { Class: ConnectionError, name: "ConnectionError" },
    { Class: ServerStartupError, name: "ServerStartupError" },
    { Class: ServerNotFoundError, name: "ServerNotFoundError" },
    { Class: DebuggerError, name: "DebuggerError" },
    { Class: MemoryAccessError, name: "MemoryAccessError" },
    { Class: TimeoutError, name: "TimeoutError" },
    { Class: DiscError, name: "DiscError" },
    { Class: EconetError, name: "EconetError" },
] as const;

for (const { Class, name } of subclasses) {
    describe(name, () => {
        it("is an instance of Error", () => {
            const err = new Class("msg");
            expect(err).toBeInstanceOf(Error);
        });

        it("is an instance of BeebiumError", () => {
            const err = new Class("msg");
            expect(err).toBeInstanceOf(BeebiumError);
        });

        it(`has name '${name}'`, () => {
            const err = new Class("msg");
            expect(err.name).toBe(name);
        });

        it("preserves the message", () => {
            const err = new Class("detailed error info");
            expect(err.message).toBe("detailed error info");
        });

        it("instanceof checks work correctly through the hierarchy", () => {
            const err = new Class("test");
            expect(err instanceof Class).toBe(true);
            expect(err instanceof BeebiumError).toBe(true);
            expect(err instanceof Error).toBe(true);
        });
    });
}

describe("cross-class instanceof", () => {
    it("ConnectionError is not an instance of DiscError", () => {
        const err = new ConnectionError("test");
        expect(err).not.toBeInstanceOf(DiscError);
    });

    it("DiscError is not an instance of ConnectionError", () => {
        const err = new DiscError("test");
        expect(err).not.toBeInstanceOf(ConnectionError);
    });
});
