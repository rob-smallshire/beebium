import { describe, it, expect, vi } from "vitest";
import { EventEmitter } from "node:events";
import { Serial } from "../src/serial.js";
import type { SerialStatus as ProtoSerialStatus } from "../src/generated/serial.js";

/** Fake ClientReadableStream: the subset toAsyncIterable uses. */
class FakeStream extends EventEmitter {
    cancel(): void {
        this.emit("end");
    }
    drive(responses: ProtoSerialStatus[]): void {
        setImmediate(() => {
            for (const r of responses) this.emit("data", r);
            this.emit("end");
        });
    }
}

function createMockStub(methods: Record<string, (req: any) => any>) {
    const stub: Record<string, any> = {};
    for (const [name, handler] of Object.entries(methods)) {
        stub[name] = vi.fn((request: any, callback: Function) => {
            try {
                callback(null, handler(request));
            } catch (err) {
                callback(err);
            }
        });
    }
    return stub;
}

function makeStatus(overrides: Partial<ProtoSerialStatus> = {}): ProtoSerialStatus {
    return {
        hasSerialSocket: true,
        aciaControl: 0x15,
        aciaStatus: 0x02,
        tdre: true,
        rdrf: false,
        notDcd: false,
        notCts: false,
        irqPending: false,
        ulaControl: 0x40,
        txBaud: 19200,
        rxBaud: 19200,
        rs423Selected: true,
        motorOn: false,
        txBitPeriod: 104,
        rxBitPeriod: 104,
        ...overrides,
    } as ProtoSerialStatus;
}

describe("Serial", () => {
    describe("getStatus", () => {
        it("maps all fields", async () => {
            const stub = createMockStub({
                getSerialStatus: () => makeStatus({ txBaud: 9600, motorOn: true }),
            });
            const serial = new Serial(stub as any);

            const status = await serial.getStatus();
            expect(status.hasSerialSocket).toBe(true);
            expect(status.aciaControl).toBe(0x15);
            expect(status.tdre).toBe(true);
            expect(status.rs423Selected).toBe(true);
            expect(status.txBaud).toBe(9600);
            expect(status.motorOn).toBe(true);
        });
    });

    describe("watchStatus", () => {
        it("yields a SerialStatus per server message", async () => {
            const stream = new FakeStream();
            const stub = { watchSerialStatus: vi.fn(() => stream) };
            const serial = new Serial(stub as any);

            const iter = serial.watchStatus();
            stream.drive([
                makeStatus({ rs423Selected: true }),
                makeStatus({ rs423Selected: false }),
            ]);

            const statuses = [];
            for await (const s of iter) statuses.push(s);
            expect(statuses).toHaveLength(2);
            expect(statuses[0]!.rs423Selected).toBe(true);
            expect(statuses[1]!.rs423Selected).toBe(false);
        });

        it("defaults minIntervalMs to 0", async () => {
            const stream = new FakeStream();
            const stub = { watchSerialStatus: vi.fn(() => stream) };
            const serial = new Serial(stub as any);

            const iter = serial.watchStatus();
            stream.drive([]);
            for await (const _ of iter) { /* drain */ }

            expect(stub.watchSerialStatus).toHaveBeenCalledWith({ minIntervalMs: 0 });
        });

        it("forwards explicit minIntervalMs", async () => {
            const stream = new FakeStream();
            const stub = { watchSerialStatus: vi.fn(() => stream) };
            const serial = new Serial(stub as any);

            const iter = serial.watchStatus({ minIntervalMs: 200 });
            stream.drive([]);
            for await (const _ of iter) { /* drain */ }

            expect(stub.watchSerialStatus).toHaveBeenCalledWith({ minIntervalMs: 200 });
        });
    });
});
