import { describe, it, expect, vi } from "vitest";
import { Tube } from "../src/tube.js";

function createMockStub(methods: Record<string, (req: any) => any>) {
    const stub: Record<string, any> = {};
    for (const [name, handler] of Object.entries(methods)) {
        stub[name] = vi.fn((request: any, callback: Function) => {
            try {
                const response = handler(request);
                callback(null, response);
            } catch (err) {
                callback(err);
            }
        });
    }
    return stub;
}

const ATTACHED = {
    hasTubeSocket: true,
    enabled: true,
    coprocessorConnected: true,
    coprocessorCpu: "65C02",
    coprocessorClockHz: 3000000,
    coprocessorExtensionId: "copro-1",
};

const NONE = {
    hasTubeSocket: true,
    enabled: false,
    coprocessorConnected: false,
    coprocessorCpu: "",
    coprocessorClockHz: 0,
    coprocessorExtensionId: "",
};

describe("Tube", () => {
    describe("getStatus", () => {
        it("maps all fields", async () => {
            const tube = new Tube(createMockStub({ getStatus: () => ATTACHED }) as any);
            expect(await tube.getStatus()).toStrictEqual(ATTACHED);
        });

        it("maps a Tube with no coprocessor", async () => {
            const tube = new Tube(createMockStub({ getStatus: () => NONE }) as any);
            expect(await tube.getStatus()).toStrictEqual(NONE);
        });
    });

    describe("isEnabled", () => {
        it("returns the enabled field", async () => {
            expect(await new Tube(createMockStub({ getStatus: () => ATTACHED }) as any).isEnabled())
                .toBe(true);
            expect(await new Tube(createMockStub({ getStatus: () => NONE }) as any).isEnabled())
                .toBe(false);
        });
    });

    describe("isCoprocessorConnected", () => {
        it("returns whether a coprocessor is attached", async () => {
            expect(await new Tube(createMockStub({ getStatus: () => ATTACHED }) as any)
                .isCoprocessorConnected()).toBe(true);
            expect(await new Tube(createMockStub({ getStatus: () => NONE }) as any)
                .isCoprocessorConnected()).toBe(false);
        });
    });
});
