import { describe, it, expect, vi } from "vitest";
import { EconetTransport } from "../src/econet_transport.js";
import { EconetError } from "../src/exceptions.js";

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

describe("EconetTransport", () => {
    describe("list", () => {
        it("returns an empty array when no transports are loaded", async () => {
            const stub = createMockStub({
                listTransports: () => ({ transports: [] }),
            });
            const transport = new EconetTransport(stub as any);
            expect(await transport.list()).toEqual([]);
        });

        it("returns a single active AUN transport", async () => {
            const stub = createMockStub({
                listTransports: () => ({
                    transports: [
                        {
                            name: "aun",
                            description: "AUN UDP transport",
                            active: true,
                            // The id is what a frontend passes to
                            // ExtensionUiService to drive the panel; it is
                            // opaque (a UUID), never the extension name.
                            id: "42eba4e4-bcd5-4362-b8f5-6c7b44d333fc",
                            hasUi: true,
                        },
                    ],
                }),
            });
            const transport = new EconetTransport(stub as any);
            const transports = await transport.list();
            expect(transports).toHaveLength(1);
            expect(transports[0]).toEqual({
                name: "aun",
                description: "AUN UDP transport",
                active: true,
                id: "42eba4e4-bcd5-4362-b8f5-6c7b44d333fc",
                hasUi: true,
            });
        });

        it("returns multiple transports with mixed active flags", async () => {
            const stub = createMockStub({
                listTransports: () => ({
                    transports: [
                        { name: "aun", description: "AUN UDP", active: false },
                        { name: "piconet", description: "Piconet USB-CDC", active: true },
                    ],
                }),
            });
            const transport = new EconetTransport(stub as any);
            const transports = await transport.list();
            expect(transports).toHaveLength(2);
            expect(transports[0]!.active).toBe(false);
            expect(transports[1]!.active).toBe(true);
        });
    });

    describe("getActive", () => {
        it("returns undefined when no active transport", async () => {
            const stub = createMockStub({
                getActiveTransport: () => ({ active: undefined }),
            });
            const transport = new EconetTransport(stub as any);
            expect(await transport.getActive()).toBeUndefined();
        });

        it("returns the active AUN transport", async () => {
            const stub = createMockStub({
                getActiveTransport: () => ({
                    active: {
                        name: "aun",
                        description: "AUN UDP transport",
                        active: true,
                    },
                }),
            });
            const transport = new EconetTransport(stub as any);
            const active = await transport.getActive();
            expect(active).toBeDefined();
            expect(active!.name).toBe("aun");
            expect(active!.active).toBe(true);
        });

        it("returns the active Piconet transport", async () => {
            const stub = createMockStub({
                getActiveTransport: () => ({
                    active: {
                        name: "piconet",
                        description: "Piconet USB-CDC bridge",
                        active: true,
                    },
                }),
            });
            const transport = new EconetTransport(stub as any);
            const active = await transport.getActive();
            expect(active).toBeDefined();
            expect(active!.name).toBe("piconet");
        });
    });

    describe("routingId", () => {
        const entry = (name: string, id: string) => ({
            name,
            description: "",
            active: true,
            id,
            hasUi: false,
        });

        it("returns the id of the single transport with that name", async () => {
            const stub = createMockStub({
                listTransports: () => ({
                    transports: [entry("piconet", "piconet"), entry("aun", "aun-7")],
                }),
            });
            const transport = new EconetTransport(stub as any);
            expect(await transport.routingId("aun")).toBe("aun-7");
        });

        it("returns an empty id when no transport has that name", async () => {
            // The server then routes by service name and reports its own
            // NOT_FOUND for a transport that is not loaded.
            const stub = createMockStub({
                listTransports: () => ({ transports: [entry("piconet", "piconet")] }),
            });
            const transport = new EconetTransport(stub as any);
            expect(await transport.routingId("aun")).toBe("");
        });

        it("rejects a name shared by several transports, naming their ids", async () => {
            const stub = createMockStub({
                listTransports: () => ({
                    transports: [entry("aun", "aun"), entry("aun", "aun-1")],
                }),
            });
            const transport = new EconetTransport(stub as any);
            await expect(transport.routingId("aun")).rejects.toThrow(EconetError);
            await expect(transport.routingId("aun")).rejects.toThrow(/aun, aun-1/);
        });
    });
});
