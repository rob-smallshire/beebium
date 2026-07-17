import { describe, it, expect, vi } from "vitest";
import { Disc, Drive, DriveState } from "../src/disc.js";
import { DiscError } from "../src/exceptions.js";

// Proto enum values matching the generated code
const ProtoDiscDriveState = {
    DISC_DRIVE_STATE_EMPTY: 0,
    DISC_DRIVE_STATE_LOADED: 1,
    DISC_DRIVE_STATE_EJECTING: 2,
};

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

function makeDiscStub(overrides?: Record<string, (req: any) => any>) {
    return createMockStub({
        getDriveStatus: () => ({
            hasDiscController: true,
            controllerType: "Acorn1770",
            isSocketed: true,
            installedControllerId: "acorn-1770",
            drives: [
                {
                    drive: 0,
                    state: ProtoDiscDriveState.DISC_DRIVE_STATE_LOADED,
                    discName: "test.ssd",
                    discUrl: "file:///tmp/test.ssd",
                    motorOn: false,
                    currentTrack: 0,
                    writeProtected: false,
                    disc: {
                        name: "test.ssd",
                        sides: 1,
                        format: "SSD",
                        writeProtected: false,
                    },
                },
                {
                    drive: 1,
                    state: ProtoDiscDriveState.DISC_DRIVE_STATE_EMPTY,
                    discName: "",
                    discUrl: "",
                    motorOn: false,
                    currentTrack: 0,
                    writeProtected: false,
                    disc: undefined,
                },
            ],
        }),
        insertDisc: () => ({
            success: true,
            error: "",
            disc: {
                name: "game.ssd",
                sides: 1,
                tracksPerSide: 40,
                sectorsPerTrack: 10,
                sectorSize: 256,
                writeProtected: false,
            },
        }),
        ejectDisc: () => ({ accepted: true, error: "" }),
        listAvailableControllers: () => ({
            controllers: [
                {
                    id: "acorn-1770",
                    displayName: "Acorn 1770",
                    fdcChip: "WD1770",
                    description: "Standard Acorn 1770 controller",
                },
                {
                    id: "watford-ddfs",
                    displayName: "Watford DDFS",
                    fdcChip: "WD1772",
                    description: "Watford double density",
                },
            ],
        }),
        ...overrides,
    });
}

describe("Disc", () => {
    describe("drive", () => {
        it("drive(0) returns Drive with correct drive number", async () => {
            const stub = makeDiscStub();
            const disc = new Disc(stub as any);
            const d = disc.drive(0);
            expect(d).toBeInstanceOf(Drive);
            // Verify it queries the correct drive
            const status = await d.getStatus();
            expect(status.drive).toBe(0);
        });

        it("drive(1) returns Drive", async () => {
            const stub = makeDiscStub();
            const disc = new Disc(stub as any);
            const d = disc.drive(1);
            expect(d).toBeInstanceOf(Drive);
            const status = await d.getStatus();
            expect(status.drive).toBe(1);
        });
    });

    describe("getStatus", () => {
        it("maps proto drive states correctly", async () => {
            const stub = makeDiscStub();
            const disc = new Disc(stub as any);
            const status = await disc.getStatus();
            expect(status.hasDiscController).toBe(true);
            expect(status.controllerType).toBe("Acorn1770");
            expect(status.isSocketed).toBe(true);
            expect(status.installedControllerId).toBe("acorn-1770");
            expect(status.drives).toHaveLength(2);
            expect(status.drives[0]!.state).toBe(DriveState.LOADED);
            expect(status.drives[1]!.state).toBe(DriveState.EMPTY);
        });

        it("maps EJECTING state", async () => {
            const stub = makeDiscStub({
                getDriveStatus: () => ({
                    hasDiscController: true,
                    controllerType: "Acorn1770",
                    isSocketed: true,
                    installedControllerId: "acorn-1770",
                    drives: [{
                        drive: 0,
                        state: ProtoDiscDriveState.DISC_DRIVE_STATE_EJECTING,
                        discName: "test.ssd",
                        discUrl: "",
                        motorOn: true,
                        currentTrack: 5,
                        writeProtected: false,
                        disc: undefined,
                    }],
                }),
            });
            const disc = new Disc(stub as any);
            const status = await disc.getStatus();
            expect(status.drives[0]!.state).toBe(DriveState.EJECTING);
        });

        it("maps disc metadata", async () => {
            const stub = makeDiscStub();
            const disc = new Disc(stub as any);
            const status = await disc.getStatus();
            const d0 = status.drives[0]!;
            expect(d0.disc).toBeDefined();
            expect(d0.disc!.name).toBe("test.ssd");
            expect(d0.disc!.sides).toBe(1);
            expect(d0.disc!.format).toBe("SSD");
            expect(d0.disc!.writeProtected).toBe(false);
        });

        it("maps undefined disc metadata for empty drive", async () => {
            const stub = makeDiscStub();
            const disc = new Disc(stub as any);
            const status = await disc.getStatus();
            expect(status.drives[1]!.disc).toBeUndefined();
        });
    });

    describe("getAvailableControllers", () => {
        it("maps controller info", async () => {
            const stub = makeDiscStub();
            const disc = new Disc(stub as any);
            const controllers = await disc.getAvailableControllers();
            expect(controllers).toHaveLength(2);
            expect(controllers[0]).toEqual({
                id: "acorn-1770",
                displayName: "Acorn 1770",
                fdcChip: "WD1770",
                description: "Standard Acorn 1770 controller",
            });
            expect(controllers[1]!.id).toBe("watford-ddfs");
        });
    });
});

describe("Drive", () => {
    describe("insert", () => {
        it("converts file path to file:// URL", async () => {
            const stub = makeDiscStub();
            const disc = new Disc(stub as any);
            const d = disc.drive(0);
            await d.insert("/tmp/game.ssd");
            expect(stub.insertDisc).toHaveBeenCalledWith(
                expect.objectContaining({
                    drive: 0,
                    url: expect.stringMatching(/^file:\/\//),
                    writeProtectOverride: false,
                }),
                expect.any(Function),
            );
            const callArgs = stub.insertDisc.mock.calls[0]![0];
            expect(callArgs.url).toContain("game.ssd");
        });

        it("passes URL as-is when it has a scheme", async () => {
            const stub = makeDiscStub();
            const disc = new Disc(stub as any);
            const d = disc.drive(0);
            await d.insert("file:///absolute/path/game.ssd");
            expect(stub.insertDisc).toHaveBeenCalledWith(
                expect.objectContaining({
                    url: "file:///absolute/path/game.ssd",
                }),
                expect.any(Function),
            );
        });

        it("passes http URL as-is", async () => {
            const stub = makeDiscStub();
            const disc = new Disc(stub as any);
            const d = disc.drive(0);
            await d.insert("http://example.com/game.ssd");
            expect(stub.insertDisc).toHaveBeenCalledWith(
                expect.objectContaining({
                    url: "http://example.com/game.ssd",
                }),
                expect.any(Function),
            );
        });

        it("returns disc metadata on success", async () => {
            const stub = makeDiscStub();
            const disc = new Disc(stub as any);
            const d = disc.drive(0);
            const meta = await d.insert("/tmp/game.ssd");
            expect(meta.name).toBe("game.ssd");
            expect(meta.sides).toBe(1);
        });

        it("throws DiscError on failure", async () => {
            const stub = makeDiscStub({
                insertDisc: () => ({ success: false, error: "bad format", disc: undefined }),
            });
            const disc = new Disc(stub as any);
            const d = disc.drive(0);
            await expect(d.insert("/tmp/bad.img")).rejects.toThrow(DiscError);
            await expect(d.insert("/tmp/bad.img")).rejects.toThrow("Insert failed on drive 0: bad format");
        });

        it("passes writeProtect flag", async () => {
            const stub = makeDiscStub();
            const disc = new Disc(stub as any);
            const d = disc.drive(1);
            await d.insert("/tmp/game.ssd", true);
            expect(stub.insertDisc).toHaveBeenCalledWith(
                expect.objectContaining({
                    drive: 1,
                    writeProtectOverride: true,
                }),
                expect.any(Function),
            );
        });
    });

    describe("eject", () => {
        it("sends correct eject options", async () => {
            const stub = makeDiscStub();
            const disc = new Disc(stub as any);
            const d = disc.drive(0);
            await d.eject({ immediate: true, quiescenceMs: 500, forceAfterMs: 10000 });
            expect(stub.ejectDisc).toHaveBeenCalledWith(
                {
                    drive: 0,
                    immediate: true,
                    quiescenceMs: 500,
                    forceAfterMs: 10000,
                },
                expect.any(Function),
            );
        });

        it("uses defaults when no options", async () => {
            const stub = makeDiscStub();
            const disc = new Disc(stub as any);
            const d = disc.drive(0);
            await d.eject();
            expect(stub.ejectDisc).toHaveBeenCalledWith(
                {
                    drive: 0,
                    immediate: false,
                    quiescenceMs: 0,
                    forceAfterMs: 0,
                },
                expect.any(Function),
            );
        });

        it("throws DiscError when eject not accepted", async () => {
            const stub = makeDiscStub({
                ejectDisc: () => ({ accepted: false, error: "drive empty" }),
            });
            const disc = new Disc(stub as any);
            const d = disc.drive(0);
            await expect(d.eject()).rejects.toThrow(DiscError);
        });
    });
});
