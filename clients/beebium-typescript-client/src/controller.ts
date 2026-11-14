/**
 * Controllers: the joysticks, handsets, mice and light pens a person holds,
 * for the Beebium TypeScript client.
 *
 * A controller publishes named controls, each an axis (0 to 65535, centre
 * 32768) or a button (pressed or not), in the device's own vocabulary: a
 * joystick's are `stick1.x`, `stick1.y`, `fire1`, `stick2.x`, `stick2.y` and
 * `fire2`. This module wraps ControllerService.
 */

import { promisify } from "./call-utils.js";
import {
    ControlType,
    type ControllerDescription,
    type ControllerServiceClient,
    type GetControlsRequest,
    type GetControlsResponse,
    type ListControllersRequest,
    type ListControllersResponse,
    type SetControlsRequest,
    type SetControlsResponse,
} from "./generated/controller.js";

/** Whether a control is an axis or a button. */
export type ControllerControlKind = "axis" | "button";

/** One control a controller publishes. */
export interface ControllerControl {
    id: string;
    displayName: string;
    group: string;
    kind: ControllerControlKind;
}

/** A controller attached to the machine, with its controls in order. */
export interface Controller {
    controllerId: string;
    displayName: string;
    controls: ControllerControl[];
}

function toController(description: ControllerDescription): Controller {
    return {
        controllerId: description.controllerId,
        displayName: description.displayName,
        controls: description.controls.map((control) => ({
            id: control.id,
            displayName: control.displayName,
            group: control.group,
            kind: control.kind === ControlType.AXIS ? "axis" : "button",
        })),
    };
}

/**
 * Client for ControllerService.
 *
 * The server refuses an unknown controller, an unknown control, a value of
 * the wrong kind or an axis above 65535 with a sentence, and a refused set
 * changes nothing.
 *
 * @example
 *     await bbc.controllers.set("joystick", { "stick1.x": 0xF000, fire1: true });
 *     const values = await bbc.controllers.get("joystick");
 */
export class Controllers {
    private readonly _stub: ControllerServiceClient;

    constructor(stub: ControllerServiceClient) {
        this._stub = stub;
    }

    /** The controllers attached to the machine; empty when there are none. */
    async list(): Promise<Controller[]> {
        const response = await promisify<ListControllersRequest, ListControllersResponse>(
            this._stub as unknown as Record<string, Function>,
            "listControllers",
            {},
        );
        return response.controllers.map(toController);
    }

    /**
     * Every control's value, in the controller's order: a number for an
     * axis, a boolean for a button.
     */
    async get(controllerId: string): Promise<Map<string, number | boolean>> {
        const response = await promisify<GetControlsRequest, GetControlsResponse>(
            this._stub as unknown as Record<string, Function>,
            "getControls",
            { controllerId },
        );
        const values = new Map<string, number | boolean>();
        for (const value of response.values) {
            if (value.axis !== undefined) {
                values.set(value.id, value.axis);
            } else {
                values.set(value.id, value.pressed ?? false);
            }
        }
        return values;
    }

    /**
     * Set controls, in order: a number sets an axis, a boolean a button. The
     * server applies all of them or, on an error, none.
     */
    async set(controllerId: string, values: Record<string, number | boolean>): Promise<void> {
        const request: SetControlsRequest = {
            controllerId,
            values: Object.entries(values).map(([id, value]) =>
                typeof value === "boolean" ? { id, pressed: value } : { id, axis: value },
            ),
        };
        await promisify<SetControlsRequest, SetControlsResponse>(
            this._stub as unknown as Record<string, Function>,
            "setControls",
            request,
        );
    }
}
