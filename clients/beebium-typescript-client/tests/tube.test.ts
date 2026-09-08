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

describe("Tube", () => {
    describe("getStatus", () => {
        it("maps all fields", async () => {
            const stub = createMockStub({
                getStatus: () => ({
                    hasTubeSocket: true,
                    enabled: true,
                    coprocessorConnected: true,
                    coprocessorType: "65C02",
                    coprocessorClockHz: 3000000,
                    sharedMemoryName: "/beebium-tube-123",
                    coprocessorGrpcAddress: "localhost:50052",
                }),
            });
            const tube = new Tube(stub as any);
            const status = await tube.getStatus();
            expect(status.hasTubeSocket).toBe(true);
            expect(status.enabled).toBe(true);
            expect(status.coprocessorConnected).toBe(true);
            expect(status.coprocessorType).toBe("65C02");
            expect(status.coprocessorClockHz).toBe(3000000);
            expect(status.sharedMemoryName).toBe("/beebium-tube-123");
            expect(status.coprocessorGrpcAddress).toBe("localhost:50052");
        });

        it("maps disabled tube", async () => {
            const stub = createMockStub({
                getStatus: () => ({
                    hasTubeSocket: true,
                    enabled: false,
                    coprocessorConnected: false,
                    coprocessorType: "",
                    coprocessorClockHz: 0,
                    sharedMemoryName: "",
                    coprocessorGrpcAddress: "",
                }),
            });
            const tube = new Tube(stub as any);
            const status = await tube.getStatus();
            expect(status.enabled).toBe(false);
            expect(status.coprocessorType).toBe("");
        });
    });

    describe("isEnabled", () => {
        it("delegates to getStatus and returns enabled field", async () => {
            const stub = createMockStub({
                getStatus: () => ({
                    hasTubeSocket: true,
                    enabled: true,
                    coprocessorConnected: false,
                    coprocessorType: "",
                    coprocessorClockHz: 0,
                    sharedMemoryName: "",
                    coprocessorGrpcAddress: "",
                }),
            });
            const tube = new Tube(stub as any);
            expect(await tube.isEnabled()).toBe(true);
        });

        it("returns false when disabled", async () => {
            const stub = createMockStub({
                getStatus: () => ({
                    hasTubeSocket: true,
                    enabled: false,
                    coprocessorConnected: false,
                    coprocessorType: "",
                    coprocessorClockHz: 0,
                    sharedMemoryName: "",
                    coprocessorGrpcAddress: "",
                }),
            });
            const tube = new Tube(stub as any);
            expect(await tube.isEnabled()).toBe(false);
        });
    });

    describe("isCoprocessorConnected", () => {
        it("returns true when connected", async () => {
            const stub = createMockStub({
                getStatus: () => ({
                    hasTubeSocket: true,
                    enabled: true,
                    coprocessorConnected: true,
                    coprocessorType: "65C02",
                    coprocessorClockHz: 3000000,
                    sharedMemoryName: "",
                    coprocessorGrpcAddress: "localhost:50052",
                }),
            });
            const tube = new Tube(stub as any);
            expect(await tube.isCoprocessorConnected()).toBe(true);
        });
    });

    describe("getCoprocessorGrpcAddress", () => {
        it("returns the address string", async () => {
            const stub = createMockStub({
                getStatus: () => ({
                    hasTubeSocket: true,
                    enabled: true,
                    coprocessorConnected: true,
                    coprocessorType: "65C02",
                    coprocessorClockHz: 3000000,
                    sharedMemoryName: "",
                    coprocessorGrpcAddress: "localhost:50052",
                }),
            });
            const tube = new Tube(stub as any);
            expect(await tube.getCoprocessorGrpcAddress()).toBe("localhost:50052");
        });

        it("returns empty string when not registered", async () => {
            const stub = createMockStub({
                getStatus: () => ({
                    hasTubeSocket: true,
                    enabled: false,
                    coprocessorConnected: false,
                    coprocessorType: "",
                    coprocessorClockHz: 0,
                    sharedMemoryName: "",
                    coprocessorGrpcAddress: "",
                }),
            });
            const tube = new Tube(stub as any);
            expect(await tube.getCoprocessorGrpcAddress()).toBe("");
        });
    });
});
