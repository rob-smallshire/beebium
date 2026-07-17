import { describe, it, expect, vi } from "vitest";
import { EconetTransport } from "../src/econet_transport.js";

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
});
