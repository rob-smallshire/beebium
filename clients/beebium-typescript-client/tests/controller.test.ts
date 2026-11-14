import { describe, it, expect, vi } from "vitest";
import { Controllers } from "../src/controller.js";
import { ControlType } from "../src/generated/controller.js";

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

describe("Controllers", () => {
    it("lists, sets and gets a joystick's controls in the device's terms", async () => {
        const stub = createMockStub({
            listControllers: () => ({
                controllers: [
                    {
                        controllerId: "joystick",
                        displayName: "Joystick",
                        controls: [
                            { id: "stick1.x", displayName: "Stick 1 X", group: "Stick 1", kind: ControlType.AXIS },
                            { id: "fire1", displayName: "Fire 1", group: "Stick 1", kind: ControlType.BUTTON },
                        ],
                    },
                ],
            }),
            setControls: () => ({}),
            getControls: () => ({
                values: [
                    { id: "stick1.x", axis: 0xf000 },
                    { id: "fire1", pressed: true },
                ],
            }),
        });
        const controllers = new Controllers(stub as any);

        expect(await controllers.list()).toEqual([
            {
                controllerId: "joystick",
                displayName: "Joystick",
                controls: [
                    { id: "stick1.x", displayName: "Stick 1 X", group: "Stick 1", kind: "axis" },
                    { id: "fire1", displayName: "Fire 1", group: "Stick 1", kind: "button" },
                ],
            },
        ]);

        await controllers.set("joystick", { "stick1.x": 0xf000, fire1: true });
        expect(stub.setControls).toHaveBeenCalledWith(
            {
                controllerId: "joystick",
                values: [
                    { id: "stick1.x", axis: 0xf000 },
                    { id: "fire1", pressed: true },
                ],
            },
            expect.any(Function),
        );

        const values = await controllers.get("joystick");
        expect(stub.getControls).toHaveBeenCalledWith({ controllerId: "joystick" }, expect.any(Function));
        expect([...values]).toEqual([
            ["stick1.x", 0xf000],
            ["fire1", true],
        ]);
    });
});
