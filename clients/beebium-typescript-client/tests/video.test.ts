import { describe, it, expect, vi } from "vitest";
import { Video } from "../src/video.js";

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

describe("Video", () => {
    describe("getConfig", () => {
        it("maps width/height/framerateHz", async () => {
            const stub = createMockStub({
                getConfig: () => ({
                    width: 640,
                    height: 512,
                    framerateHz: 50,
                }),
            });
            const video = new Video(stub as any);
            const config = await video.getConfig();
            expect(config.width).toBe(640);
            expect(config.height).toBe(512);
            expect(config.framerateHz).toBe(50);
        });

        it("maps Mode 7 resolution", async () => {
            const stub = createMockStub({
                getConfig: () => ({
                    width: 480,
                    height: 500,
                    framerateHz: 50,
                }),
            });
            const video = new Video(stub as any);
            const config = await video.getConfig();
            expect(config.width).toBe(480);
            expect(config.height).toBe(500);
        });

        it("calls getConfig RPC with empty request", async () => {
            const stub = createMockStub({
                getConfig: () => ({
                    width: 640,
                    height: 512,
                    framerateHz: 50,
                }),
            });
            const video = new Video(stub as any);
            await video.getConfig();
            expect(stub.getConfig).toHaveBeenCalledWith({}, expect.any(Function));
        });
    });
});
