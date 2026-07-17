import { describe, it, expect, vi } from "vitest";
import { Via, ViaId } from "../src/via.js";

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

const SAMPLE_VIA_STATE = {
    ora: 0x12,
    orb: 0x34,
    ddra: 0xFF,
    ddrb: 0x00,
    ira: 0x56,
    irb: 0x78,
    t1c: 0x1234,
    t1l: 0x5678,
    t2c: 0x9ABC,
    t2l: 0xDE,
    acr: 0xC0,
    pcr: 0x22,
    sr: 0x55,
    ifr: 0x60,
    ier: 0x7F,
    t1Pending: true,
    t2Pending: false,
    t1Pb7: 1,
    ca1: true,
    ca2: false,
    cb1: true,
    cb2: false,
};

describe("Via", () => {
    describe("System VIA", () => {
        it("calls getSystemViaState", async () => {
            const stub = createMockStub({
                getSystemViaState: () => SAMPLE_VIA_STATE,
            });
            const via = new Via(stub as any, ViaId.SYSTEM);
            const state = await via.getState();
            expect(stub.getSystemViaState).toHaveBeenCalledWith({}, expect.any(Function));
            expect(state.ora).toBe(0x12);
        });

        it("does not call getUserViaState", async () => {
            const stub = createMockStub({
                getSystemViaState: () => SAMPLE_VIA_STATE,
                getUserViaState: () => { throw new Error("should not be called"); },
            });
            const via = new Via(stub as any, ViaId.SYSTEM);
            await via.getState();
            expect(stub.getUserViaState).not.toHaveBeenCalled();
        });
    });

    describe("User VIA", () => {
        it("calls getUserViaState", async () => {
            const stub = createMockStub({
                getUserViaState: () => SAMPLE_VIA_STATE,
            });
            const via = new Via(stub as any, ViaId.USER);
            const state = await via.getState();
            expect(stub.getUserViaState).toHaveBeenCalledWith({}, expect.any(Function));
            expect(state.orb).toBe(0x34);
        });

        it("does not call getSystemViaState", async () => {
            const stub = createMockStub({
                getUserViaState: () => SAMPLE_VIA_STATE,
                getSystemViaState: () => { throw new Error("should not be called"); },
            });
            const via = new Via(stub as any, ViaId.USER);
            await via.getState();
            expect(stub.getSystemViaState).not.toHaveBeenCalled();
        });
    });

    describe("Response field mapping", () => {
        it("maps all fields correctly", async () => {
            const stub = createMockStub({
                getSystemViaState: () => SAMPLE_VIA_STATE,
            });
            const via = new Via(stub as any, ViaId.SYSTEM);
            const state = await via.getState();

            expect(state.ora).toBe(0x12);
            expect(state.orb).toBe(0x34);
            expect(state.ddra).toBe(0xFF);
            expect(state.ddrb).toBe(0x00);
            expect(state.ira).toBe(0x56);
            expect(state.irb).toBe(0x78);
            expect(state.t1c).toBe(0x1234);
            expect(state.t1l).toBe(0x5678);
            expect(state.t2c).toBe(0x9ABC);
            expect(state.t2l).toBe(0xDE);
            expect(state.acr).toBe(0xC0);
            expect(state.pcr).toBe(0x22);
            expect(state.sr).toBe(0x55);
            expect(state.ifr).toBe(0x60);
            expect(state.ier).toBe(0x7F);
            expect(state.t1Pending).toBe(true);
            expect(state.t2Pending).toBe(false);
            expect(state.t1Pb7).toBe(1);
            expect(state.ca1).toBe(true);
            expect(state.ca2).toBe(false);
            expect(state.cb1).toBe(true);
            expect(state.cb2).toBe(false);
        });
    });
});
