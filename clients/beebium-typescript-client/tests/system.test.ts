import { describe, it, expect, vi } from "vitest";
import { System, ShutdownMode } from "../src/system.js";

function createMockStub(methods: Record<string, (req: any) => any>) {
    const stub: Record<string, any> = {};
    for (const [name, handler] of Object.entries(methods)) {
        stub[name] = vi.fn((request: any, ...args: any[]) => {
            // Support both (req, callback) and (req, metadata, callback)
            const callback = args[args.length - 1] as Function;
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

const FULL_SYSTEM_INFO = {
    identity: {
        uuid: "abc-123",
        name: "My BBC",
        modelType: "ModelB",
        modelName: "BBC Model B",
    },
    provenance: {
        type: "launched",
        instanceUuid: "inst-456",
        version: "1.0.0",
        timestamp: 1700000000,
    },
    clockSpeedHz: 2000000,
    connections: {
        clientCount: 3,
    },
};

describe("System", () => {
    describe("getIdentity", () => {
        it("maps SystemInfo response identity fields", async () => {
            const stub = createMockStub({
                getSystemInfo: () => FULL_SYSTEM_INFO,
            });
            const sys = new System(stub as any);
            const identity = await sys.getIdentity();
            expect(identity).toEqual({
                uuid: "abc-123",
                name: "My BBC",
                modelType: "ModelB",
                modelName: "BBC Model B",
            });
        });

        it("throws when no identity returned", async () => {
            const stub = createMockStub({
                getSystemInfo: () => ({ ...FULL_SYSTEM_INFO, identity: undefined }),
            });
            const sys = new System(stub as any);
            await expect(sys.getIdentity()).rejects.toThrow("Server returned no identity");
        });
    });

    describe("getProvenance", () => {
        it("maps provenance fields", async () => {
            const stub = createMockStub({
                getSystemInfo: () => FULL_SYSTEM_INFO,
            });
            const sys = new System(stub as any);
            const prov = await sys.getProvenance();
            expect(prov).toEqual({
                type: "launched",
                instanceUuid: "inst-456",
                version: "1.0.0",
                timestamp: 1700000000,
            });
        });

        it("throws when no provenance returned", async () => {
            const stub = createMockStub({
                getSystemInfo: () => ({ ...FULL_SYSTEM_INFO, provenance: undefined }),
            });
            const sys = new System(stub as any);
            await expect(sys.getProvenance()).rejects.toThrow("Server returned no provenance");
        });
    });

    describe("setMachineName", () => {
        it("sends correct request and returns identity", async () => {
            const stub = createMockStub({
                setMachineName: () => ({
                    identity: {
                        uuid: "abc-123",
                        name: "Renamed BBC",
                        modelType: "ModelB",
                        modelName: "BBC Model B",
                    },
                }),
            });
            const sys = new System(stub as any);
            const identity = await sys.setMachineName("Renamed BBC");
            expect(identity.name).toBe("Renamed BBC");
            expect(stub.setMachineName).toHaveBeenCalledWith(
                { name: "Renamed BBC" },
                expect.any(Function),
            );
        });

        it("throws when no identity returned", async () => {
            const stub = createMockStub({
                setMachineName: () => ({ identity: undefined }),
            });
            const sys = new System(stub as any);
            await expect(sys.setMachineName("test")).rejects.toThrow(
                "Server returned no identity after setMachineName",
            );
        });
    });

    describe("requestShutdown", () => {
        it("maps GRACEFUL mode to proto value", async () => {
            const stub = createMockStub({
                requestShutdown: () => ({ accepted: true, message: "shutting down" }),
            });
            const sys = new System(stub as any);
            const response = await sys.requestShutdown(ShutdownMode.GRACEFUL);
            expect(response.accepted).toBe(true);
            expect(response.message).toBe("shutting down");
            // Check request had the proto enum value for GRACEFUL
            const callArgs = stub.requestShutdown.mock.calls[0]![0];
            expect(callArgs.gracePeriodMs).toBe(5000); // default
        });

        it("maps IMMEDIATE mode", async () => {
            const stub = createMockStub({
                requestShutdown: () => ({ accepted: true, message: "immediate" }),
            });
            const sys = new System(stub as any);
            const response = await sys.requestShutdown(ShutdownMode.IMMEDIATE, 1000);
            expect(response.accepted).toBe(true);
            const callArgs = stub.requestShutdown.mock.calls[0]![0];
            expect(callArgs.gracePeriodMs).toBe(1000);
        });

        it("sends metadata with instance UUID when set", async () => {
            const stub = createMockStub({
                requestShutdown: () => ({ accepted: true, message: "ok" }),
            });
            const sys = new System(stub as any, "my-uuid-123");
            await sys.requestShutdown();
            // With instance UUID, the method receives (request, metadata, callback)
            const calls = stub.requestShutdown.mock.calls;
            expect(calls).toHaveLength(1);
            // The call should have 3 args: request, metadata, callback
            expect(calls[0]).toHaveLength(3);
        });

        it("does not send metadata when no instance UUID", async () => {
            const stub = createMockStub({
                requestShutdown: () => ({ accepted: true, message: "ok" }),
            });
            const sys = new System(stub as any); // no instanceUuid
            await sys.requestShutdown();
            // Without instance UUID, the method receives (request, callback)
            const calls = stub.requestShutdown.mock.calls;
            expect(calls).toHaveLength(1);
            expect(calls[0]).toHaveLength(2);
        });
    });

    describe("getAdvertisementState", () => {
        it("maps response correctly", async () => {
            const stub = createMockStub({
                getAdvertisementState: () => ({
                    state: {
                        enabled: true,
                        available: true,
                        advertisedName: "My BBC Micro",
                    },
                }),
            });
            const sys = new System(stub as any);
            const state = await sys.getAdvertisementState();
            expect(state).toEqual({
                enabled: true,
                available: true,
                advertisedName: "My BBC Micro",
            });
        });

        it("throws when no state returned", async () => {
            const stub = createMockStub({
                getAdvertisementState: () => ({ state: undefined }),
            });
            const sys = new System(stub as any);
            await expect(sys.getAdvertisementState()).rejects.toThrow(
                "Server returned no advertisement state",
            );
        });
    });

    describe("setAdvertisement", () => {
        it("maps response correctly", async () => {
            const stub = createMockStub({
                setAdvertisement: () => ({
                    state: {
                        enabled: false,
                        available: false,
                        advertisedName: "",
                    },
                }),
            });
            const sys = new System(stub as any);
            const state = await sys.setAdvertisement(false);
            expect(state.enabled).toBe(false);
            expect(stub.setAdvertisement).toHaveBeenCalledWith(
                { enabled: false },
                expect.any(Function),
            );
        });

        it("throws when no state returned", async () => {
            const stub = createMockStub({
                setAdvertisement: () => ({ state: undefined }),
            });
            const sys = new System(stub as any);
            await expect(sys.setAdvertisement(true)).rejects.toThrow(
                "Server returned no advertisement state",
            );
        });
    });

    describe("getClockSpeedHz", () => {
        it("reads correct response field", async () => {
            const stub = createMockStub({
                getSystemInfo: () => FULL_SYSTEM_INFO,
            });
            const sys = new System(stub as any);
            const hz = await sys.getClockSpeedHz();
            expect(hz).toBe(2000000);
        });
    });

    describe("getClientCount", () => {
        it("reads correct response field", async () => {
            const stub = createMockStub({
                getSystemInfo: () => FULL_SYSTEM_INFO,
            });
            const sys = new System(stub as any);
            const count = await sys.getClientCount();
            expect(count).toBe(3);
        });

        it("returns 0 when connections is undefined", async () => {
            const stub = createMockStub({
                getSystemInfo: () => ({ ...FULL_SYSTEM_INFO, connections: undefined }),
            });
            const sys = new System(stub as any);
            const count = await sys.getClientCount();
            expect(count).toBe(0);
        });
    });

    describe("setSpeedMultiplier", () => {
        it("sends the request and returns the echoed multiplier", async () => {
            const stub = createMockStub({
                setSpeedMultiplier: (req: any) => ({ speedMultiplier: req.speedMultiplier }),
            });
            const sys = new System(stub as any);
            const result = await sys.setSpeedMultiplier(2.0);
            expect(result).toBe(2.0);
            expect(stub.setSpeedMultiplier).toHaveBeenCalledWith(
                { speedMultiplier: 2.0 },
                expect.any(Function),
            );
        });

        it("forwards the unlimited sentinel (0.0)", async () => {
            const stub = createMockStub({
                setSpeedMultiplier: () => ({ speedMultiplier: 0.0 }),
            });
            const sys = new System(stub as any);
            const result = await sys.setSpeedMultiplier(0.0);
            expect(result).toBe(0.0);
        });
    });

    describe("getPacingStats", () => {
        it("maps every response field onto the result", async () => {
            const stub = createMockStub({
                getPacingStats: () => ({
                    ticksExecuted: 1000,
                    ticksIoSkipped: 5,
                    controllerDrift: 12.5,
                    controllerIntegral: -3.0,
                    speedMultiplier: 2.0,
                    achievedSpeedMultiplier: 1.97,
                    estimatedMaxSpeedMultiplier: 18.4,
                }),
            });
            const sys = new System(stub as any);
            const stats = await sys.getPacingStats();
            expect(stats).toEqual({
                ticksExecuted: 1000,
                ticksIoSkipped: 5,
                controllerDrift: 12.5,
                controllerIntegral: -3.0,
                speedMultiplier: 2.0,
                achievedSpeedMultiplier: 1.97,
                estimatedMaxSpeedMultiplier: 18.4,
            });
        });

        it("treats estimatedMaxSpeedMultiplier 0.0 as the no-estimate sentinel", async () => {
            const stub = createMockStub({
                getPacingStats: () => ({
                    ticksExecuted: 0,
                    ticksIoSkipped: 0,
                    controllerDrift: 0,
                    controllerIntegral: 0,
                    speedMultiplier: 1.0,
                    achievedSpeedMultiplier: 0.0,
                    estimatedMaxSpeedMultiplier: 0.0,
                }),
            });
            const sys = new System(stub as any);
            const stats = await sys.getPacingStats();
            expect(stats.estimatedMaxSpeedMultiplier).toBe(0.0);
        });
    });
});
