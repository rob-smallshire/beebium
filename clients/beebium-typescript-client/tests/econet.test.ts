import { describe, it, expect, vi } from "vitest";
import { EventEmitter } from "node:events";
import { Econet } from "../src/econet.js";
import { EconetError } from "../src/exceptions.js";
import type { GetEconetStatusResponse as ProtoGetEconetStatusResponse } from "../src/generated/econet.js";

/**
 * Fake ClientReadableStream shaped to the subset that toAsyncIterable uses:
 * EventEmitter events (data, error, end) plus cancel().
 */
class FakeStream extends EventEmitter {
    public cancelled = false;
    cancel(): void {
        this.cancelled = true;
        this.emit("end");
    }
    drive(responses: ProtoGetEconetStatusResponse[]): void {
        setImmediate(() => {
            for (const r of responses) {
                this.emit("data", r);
            }
            this.emit("end");
        });
    }
}

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

describe("Econet", () => {
    describe("getStatus", () => {
        it("maps all fields including optional adlc and handshake", async () => {
            const stub = createMockStub({
                getEconetStatus: () => ({
                    hasEconetSocket: true,
                    enabled: true,
                    stationId: 100,
                    aunMode: true,
                    connected: true,
                    adlc: {
                        cr1: 0x01,
                        cr2: 0x02,
                        cr3: 0x03,
                        cr4: 0x04,
                        sr1: 0x10,
                        sr2: 0x20,
                        irqOutput: true,
                        txFifoEmpty: true,
                        txFifoFull: false,
                        rxFifoEmpty: false,
                        rxFifoFull: false,
                        txFrameField: "DATA",
                        rxFrameField: "IDLE",
                        pseLevel: 3,
                        ctsInput: true,
                    },
                    handshake: {
                        stage: "SCOUT_ACK",
                        flagFillActive: true,
                    },
                }),
            });
            const econet = new Econet(stub as any);
            const status = await econet.getStatus();

            expect(status.hasEconetSocket).toBe(true);
            expect(status.enabled).toBe(true);
            expect(status.stationId).toBe(100);
            expect(status.aunMode).toBe(true);
            expect(status.connected).toBe(true);

            expect(status.adlc).toBeDefined();
            expect(status.adlc!.cr1).toBe(0x01);
            expect(status.adlc!.sr1).toBe(0x10);
            expect(status.adlc!.irqOutput).toBe(true);
            expect(status.adlc!.txFrameField).toBe("DATA");
            expect(status.adlc!.pseLevel).toBe(3);
            expect(status.adlc!.ctsInput).toBe(true);

            expect(status.handshake).toBeDefined();
            expect(status.handshake!.stage).toBe("SCOUT_ACK");
            expect(status.handshake!.flagFillActive).toBe(true);
        });

        it("maps undefined adlc and handshake", async () => {
            const stub = createMockStub({
                getEconetStatus: () => ({
                    hasEconetSocket: false,
                    enabled: false,
                    stationId: 0,
                    aunMode: false,
                    connected: false,
                    adlc: undefined,
                    handshake: undefined,
                }),
            });
            const econet = new Econet(stub as any);
            const status = await econet.getStatus();
            expect(status.adlc).toBeUndefined();
            expect(status.handshake).toBeUndefined();
        });
    });

    describe("enable", () => {
        it("sends correct stationId and returns actual port", async () => {
            const stub = createMockStub({
                enableEconet: () => ({
                    success: true,
                    error: "",
                    actualAunPort: 32769,
                }),
            });
            const econet = new Econet(stub as any);
            const port = await econet.enable(100);
            expect(port).toBe(32769);
            expect(stub.enableEconet).toHaveBeenCalledWith(
                { stationId: 100, aunPort: 0, noNetwork: false },
                expect.any(Function),
            );
        });

        it("passes options when provided", async () => {
            const stub = createMockStub({
                enableEconet: () => ({
                    success: true,
                    error: "",
                    actualAunPort: 40000,
                }),
            });
            const econet = new Econet(stub as any);
            await econet.enable(50, { aunPort: 40000, noNetwork: true });
            expect(stub.enableEconet).toHaveBeenCalledWith(
                { stationId: 50, aunPort: 40000, noNetwork: true },
                expect.any(Function),
            );
        });

        it("throws EconetError on failure", async () => {
            const stub = createMockStub({
                enableEconet: () => ({
                    success: false,
                    error: "no socket",
                    actualAunPort: 0,
                }),
            });
            const econet = new Econet(stub as any);
            await expect(econet.enable(100)).rejects.toThrow(EconetError);
            await expect(econet.enable(100)).rejects.toThrow("Enable Econet failed: no socket");
        });
    });

    describe("disable", () => {
        it("calls correct RPC", async () => {
            const stub = createMockStub({
                disableEconet: () => ({ success: true, error: "" }),
            });
            const econet = new Econet(stub as any);
            await econet.disable();
            expect(stub.disableEconet).toHaveBeenCalledWith({}, expect.any(Function));
        });

        it("throws EconetError on failure", async () => {
            const stub = createMockStub({
                disableEconet: () => ({ success: false, error: "not enabled" }),
            });
            const econet = new Econet(stub as any);
            await expect(econet.disable()).rejects.toThrow(EconetError);
        });
    });

    // addPeer / removePeer cases removed: those RPCs moved from
    // EconetService to AunService in the prior branch and the TS
    // client does not yet have an AunService wrapper. See
    // project_typescript_client_cutover.md in project memory.

    describe("watchStatus", () => {
        function makeStatus(
            overrides: Partial<ProtoGetEconetStatusResponse> = {},
        ): ProtoGetEconetStatusResponse {
            return {
                hasEconetSocket: true,
                enabled: false,
                stationId: 0,
                aunMode: false,
                connected: false,
                adlc: undefined,
                handshake: undefined,
                tickCount: 0n,
                cr10X82WriteCount: 0,
                rxFramesReceivedCount: 0,
                rxBlockedByResetCount: 0,
                scoutAckGeneratedCount: 0,
                txFramesFromBeebCount: 0,
                unexpectedTxResetCount: 0,
                txFromIdleCount: 0,
                maxHandshakeTimerSeen: 0,
                watchdogTimeoutCount: 0,
                sendStageLog: "",
                ticksWithTimerActive: 0n,
                readStretchParasiteTicks: 0n,
                ...overrides,
            } as ProtoGetEconetStatusResponse;
        }

        it("yields an EconetStatus per server message", async () => {
            const stream = new FakeStream();
            const stub = { watchEconetStatus: vi.fn(() => stream) };
            const econet = new Econet(stub as any);

            const iter = econet.watchStatus();
            stream.drive([
                makeStatus({ enabled: false }),
                makeStatus({ enabled: true, stationId: 42 }),
                makeStatus({ enabled: true, stationId: 42, connected: true }),
            ]);

            const statuses = [];
            for await (const s of iter) {
                statuses.push(s);
            }
            expect(statuses).toHaveLength(3);
            expect(statuses[0]!.enabled).toBe(false);
            expect(statuses[1]!.enabled).toBe(true);
            expect(statuses[1]!.stationId).toBe(42);
            expect(statuses[2]!.connected).toBe(true);
        });

        it("defaults minIntervalMs to 0", async () => {
            const stream = new FakeStream();
            const stub = { watchEconetStatus: vi.fn(() => stream) };
            const econet = new Econet(stub as any);

            const iter = econet.watchStatus();
            stream.drive([]);
            for await (const _ of iter) { /* drain */ }

            expect(stub.watchEconetStatus).toHaveBeenCalledWith({ minIntervalMs: 0 });
        });

        it("forwards explicit minIntervalMs", async () => {
            const stream = new FakeStream();
            const stub = { watchEconetStatus: vi.fn(() => stream) };
            const econet = new Econet(stub as any);

            const iter = econet.watchStatus({ minIntervalMs: 200 });
            stream.drive([]);
            for await (const _ of iter) { /* drain */ }

            expect(stub.watchEconetStatus).toHaveBeenCalledWith({ minIntervalMs: 200 });
        });
    });

    describe("convenience methods", () => {
        it("isEnabled delegates to getStatus", async () => {
            const stub = createMockStub({
                getEconetStatus: () => ({
                    hasEconetSocket: true,
                    enabled: true,
                    stationId: 100,
                    aunMode: true,
                    connected: true,
                    adlc: undefined,
                    handshake: undefined,
                }),
            });
            const econet = new Econet(stub as any);
            expect(await econet.isEnabled()).toBe(true);
        });

        it("getStationId delegates to getStatus", async () => {
            const stub = createMockStub({
                getEconetStatus: () => ({
                    hasEconetSocket: true,
                    enabled: true,
                    stationId: 42,
                    aunMode: false,
                    connected: false,
                    adlc: undefined,
                    handshake: undefined,
                }),
            });
            const econet = new Econet(stub as any);
            expect(await econet.getStationId()).toBe(42);
        });
    });

    describe("events", () => {
        it("maps frame events, preserving the true payload size", async () => {
            const stream = new FakeStream();
            const stub = { subscribeEconetEvents: vi.fn(() => stream) };
            const econet = new Econet(stub as any);

            const iter = econet.events();
            stream.drive([
                {
                    type: 1, // FRAME_SENT
                    sequence: 7n,
                    connected: false,
                    frame: {
                        frameType: "unicast",
                        destNet: 0,
                        destStn: 254,
                        srcNet: 0,
                        srcStn: 101,
                        port: 0x99,
                        controlByte: 0x80,
                        dataLength: 200,
                        data: new Uint8Array([1, 2, 3]),
                    },
                },
            ] as any);

            const events = [];
            for await (const event of iter) events.push(event);

            expect(events).toHaveLength(1);
            expect(events[0].type).toBe("frameSent");
            expect(events[0].sequence).toBe(7);
            expect(events[0].frame?.destStn).toBe(254);
            // Truncated payload; dataLength still reports the true size.
            expect(events[0].frame?.dataLength).toBe(200);
            expect(events[0].frame?.data).toHaveLength(3);
        });

        it("maps connection changes, which carry no frame", async () => {
            const stream = new FakeStream();
            const stub = { subscribeEconetEvents: vi.fn(() => stream) };
            const econet = new Econet(stub as any);

            const iter = econet.events();
            stream.drive([
                { type: 4, sequence: 1n, connected: true, frame: undefined },
            ] as any);

            const events = [];
            for await (const event of iter) events.push(event);

            expect(events[0].type).toBe("connectionChange");
            expect(events[0].connected).toBe(true);
            expect(events[0].frame).toBeUndefined();
        });
    });
});
