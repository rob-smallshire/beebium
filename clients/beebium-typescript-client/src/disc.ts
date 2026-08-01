/**
 * Disc management interface for the Beebium TypeScript client.
 *
 * Provides disc insertion/ejection, drive status queries, controller
 * management, and disc event streaming.
 */

import { resolve } from "node:path";
import type {
    DiscServiceClient,
    InsertDiscResponse as ProtoInsertDiscResponse,
    EjectDiscResponse as ProtoEjectDiscResponse,
    GetDriveStatusResponse as ProtoGetDriveStatusResponse,
    DriveStatus as ProtoDriveStatus,
    DiscMetadata as ProtoDiscMetadata,
    DiscEvent as ProtoDiscEvent,
    GetSpinUpDelayResponse as ProtoGetSpinUpDelayResponse,
    SetSpinUpDelayResponse as ProtoSetSpinUpDelayResponse,
    ListAvailableControllersResponse as ProtoListAvailableControllersResponse,
    DiscControllerTypeInfo as ProtoDiscControllerTypeInfo,
    InstallDiscControllerResponse as ProtoInstallDiscControllerResponse,
} from "./generated/disc.js";
import {
    DiscDriveState as ProtoDiscDriveState,
    DiscEventType as ProtoDiscEventType,
} from "./generated/disc.js";
import { promisify } from "./call-utils.js";
import { toAsyncIterable, BackgroundStreamHandle } from "./stream-utils.js";
import { DiscError, TimeoutError } from "./exceptions.js";

export enum DriveState {
    EMPTY = "empty",
    LOADED = "loaded",
    EJECTING = "ejecting",
}

export enum DiscEventType {
    INSERTED = "inserted",
    EJECTED = "ejected",
    FORCE_EJECTED = "force_ejected",
    EJECT_REQUESTED = "eject_requested",
    EJECT_CANCELLED = "eject_cancelled",
    MOTOR_ON = "motor_on",
    MOTOR_OFF = "motor_off",
}

export interface DiscMetadata {
    name: string;
    sides: number;
    writeProtected: boolean;
    format: string;
}

export interface DriveStatus {
    drive: number;
    state: DriveState;
    discName: string;
    discUrl: string;
    motorOn: boolean;
    currentTrack: number;
    writeProtected: boolean;
    disc: DiscMetadata | undefined;
}

export interface DiscControllerStatus {
    hasDiscController: boolean;
    controllerType: string;
    isSocketed: boolean;
    installedControllerId: string;
    drives: DriveStatus[];
}

export interface DiscEvent {
    type: DiscEventType;
    drive: number;
    timestampCycles: number;
    disc: DiscMetadata | undefined;
    discUrl: string;
}

export interface DiscControllerInfo {
    id: string;
    displayName: string;
    fdcChip: string;
    description: string;
}

function toDriveState(proto: ProtoDiscDriveState): DriveState {
    switch (proto) {
        case ProtoDiscDriveState.DISC_DRIVE_STATE_LOADED:
            return DriveState.LOADED;
        case ProtoDiscDriveState.DISC_DRIVE_STATE_EJECTING:
            return DriveState.EJECTING;
        case ProtoDiscDriveState.DISC_DRIVE_STATE_EMPTY:
        default:
            return DriveState.EMPTY;
    }
}

function toDiscEventType(proto: ProtoDiscEventType): DiscEventType {
    switch (proto) {
        case ProtoDiscEventType.DISC_EVENT_INSERTED:
            return DiscEventType.INSERTED;
        case ProtoDiscEventType.DISC_EVENT_EJECTED:
            return DiscEventType.EJECTED;
        case ProtoDiscEventType.DISC_EVENT_FORCE_EJECTED:
            return DiscEventType.FORCE_EJECTED;
        case ProtoDiscEventType.DISC_EVENT_EJECT_REQUESTED:
            return DiscEventType.EJECT_REQUESTED;
        case ProtoDiscEventType.DISC_EVENT_EJECT_CANCELLED:
            return DiscEventType.EJECT_CANCELLED;
        case ProtoDiscEventType.DISC_EVENT_MOTOR_ON:
            return DiscEventType.MOTOR_ON;
        case ProtoDiscEventType.DISC_EVENT_MOTOR_OFF:
            return DiscEventType.MOTOR_OFF;
        default:
            return DiscEventType.INSERTED;
    }
}

function toDiscMetadata(proto: ProtoDiscMetadata | undefined): DiscMetadata | undefined {
    if (!proto) return undefined;
    return {
        name: proto.name,
        sides: proto.sides,
        writeProtected: proto.writeProtected,
        format: proto.format,
    };
}

function toDriveStatus(proto: ProtoDriveStatus): DriveStatus {
    return {
        drive: proto.drive,
        state: toDriveState(proto.state),
        discName: proto.discName,
        discUrl: proto.discUrl,
        motorOn: proto.motorOn,
        currentTrack: proto.currentTrack,
        writeProtected: proto.writeProtected,
        disc: toDiscMetadata(proto.disc),
    };
}

function toDiscEvent(proto: ProtoDiscEvent): DiscEvent {
    return {
        type: toDiscEventType(proto.type),
        drive: proto.drive,
        timestampCycles: proto.timestampCycles,
        disc: toDiscMetadata(proto.disc),
        discUrl: proto.discUrl,
    };
}

function toDiscControllerInfo(proto: ProtoDiscControllerTypeInfo): DiscControllerInfo {
    return {
        id: proto.id,
        displayName: proto.displayName,
        fdcChip: proto.fdcChip,
        description: proto.description,
    };
}

/**
 * Resolves a path or URL to a disc image URL.
 *
 * If the input starts with "file://" or "http", it is used as-is.
 * Otherwise it is resolved to an absolute filesystem path and
 * prepended with "file://".
 */
function toDiscUrl(pathOrUrl: string): string {
    if (pathOrUrl.startsWith("file://") || pathOrUrl.startsWith("http")) {
        return pathOrUrl;
    }
    return "file://" + resolve(pathOrUrl);
}

/**
 * Represents a single floppy drive. Created via Disc.drive().
 */
export class Drive {
    private readonly discService: Disc;
    private readonly driveNum: number;

    constructor(discService: Disc, driveNum: number) {
        this.discService = discService;
        this.driveNum = driveNum;
    }

    /** Get the status of this drive. */
    async getStatus(): Promise<DriveStatus> {
        const status = await this.discService.getStatus();
        const driveStatus = status.drives.find((d) => d.drive === this.driveNum);
        if (!driveStatus) {
            throw new DiscError(`Drive ${this.driveNum} not found in status`);
        }
        return driveStatus;
    }

    /** Get the current state of this drive. */
    async getState(): Promise<DriveState> {
        return (await this.getStatus()).state;
    }

    /** Whether the drive is empty (no disc). */
    async isEmpty(): Promise<boolean> {
        return (await this.getState()) === DriveState.EMPTY;
    }

    /** Whether the drive has a loaded disc. */
    async isLoaded(): Promise<boolean> {
        return (await this.getState()) === DriveState.LOADED;
    }

    /** Whether the drive is in the process of ejecting. */
    async isEjecting(): Promise<boolean> {
        return (await this.getState()) === DriveState.EJECTING;
    }

    /**
     * Insert a disc image into this drive.
     *
     * The drive must be empty. Removing a disc is a deliberate {@link eject},
     * so that it goes through the safe eject path rather than being pulled
     * out from under a spinning motor.
     *
     * @param pathOrUrl - Local filepath or URL to the disc image.
     * @param writeProtect - Force write protection on the disc.
     * @returns Metadata of the inserted disc.
     * @throws DiscError - If the drive already holds a disc, or the image
     *   cannot be loaded.
     */
    async insert(pathOrUrl: string, writeProtect: boolean = false): Promise<DiscMetadata> {
        const url = toDiscUrl(pathOrUrl);
        const response = await promisify<
            { drive: number; url: string; writeProtectOverride: boolean },
            ProtoInsertDiscResponse
        >(
            this.discService.stub as unknown as Record<string, Function>,
            "insertDisc",
            { drive: this.driveNum, url, writeProtectOverride: writeProtect },
        );
        if (!response.success) {
            throw new DiscError(`Insert failed on drive ${this.driveNum}: ${response.error}`);
        }
        const metadata = toDiscMetadata(response.disc);
        if (!metadata) {
            throw new DiscError(`Insert succeeded but no disc metadata returned`);
        }
        return metadata;
    }

    /**
     * Eject the disc from this drive.
     *
     * @param options.immediate - Skip quiescence wait.
     * @param options.quiescenceMs - Motor-off time required before ejecting.
     * @param options.forceAfterMs - Force eject after this timeout.
     */
    async eject(options?: {
        immediate?: boolean;
        quiescenceMs?: number;
        forceAfterMs?: number;
    }): Promise<void> {
        const request: {
            drive: number;
            immediate: boolean;
            quiescenceMs: number;
            forceAfterMs: number;
        } = {
            drive: this.driveNum,
            immediate: options?.immediate ?? false,
            quiescenceMs: options?.quiescenceMs ?? 0,
            forceAfterMs: options?.forceAfterMs ?? 0,
        };
        const response = await promisify<typeof request, ProtoEjectDiscResponse>(
            this.discService.stub as unknown as Record<string, Function>,
            "ejectDisc",
            request,
        );
        if (!response.accepted) {
            throw new DiscError(`Eject failed on drive ${this.driveNum}: ${response.error}`);
        }
    }

    /**
     * Wait for the drive to become empty after an eject request.
     *
     * @param timeoutMs - Maximum time to wait.
     * @returns true if the drive became empty, false if timed out.
     */
    async waitForEject(timeoutMs: number = 15000): Promise<boolean> {
        const deadline = Date.now() + timeoutMs;
        const pollIntervalMs = 50;
        while (Date.now() < deadline) {
            const state = await this.getState();
            if (state === DriveState.EMPTY) {
                return true;
            }
            await new Promise<void>((r) => setTimeout(r, pollIntervalMs));
        }
        return false;
    }
}

/**
 * Disc management interface.
 *
 * Provides disc insertion/ejection, drive status queries, controller
 * management, and disc event streaming.
 */
export class Disc {
    /** @internal */
    readonly stub: DiscServiceClient;

    private _drive0: Drive | undefined;
    private _drive1: Drive | undefined;

    constructor(stub: DiscServiceClient) {
        this.stub = stub;
    }

    /** Get a Drive instance for the given drive number. */
    drive(num: number): Drive {
        return new Drive(this, num);
    }

    /** Drive 0 accessor (lazy). */
    get drive0(): Drive {
        if (!this._drive0) {
            this._drive0 = new Drive(this, 0);
        }
        return this._drive0;
    }

    /** Drive 1 accessor (lazy). */
    get drive1(): Drive {
        if (!this._drive1) {
            this._drive1 = new Drive(this, 1);
        }
        return this._drive1;
    }

    /** Get the status of all drives and the disc controller. */
    async getStatus(): Promise<DiscControllerStatus> {
        const response = await promisify<{}, ProtoGetDriveStatusResponse>(
            this.stub as unknown as Record<string, Function>,
            "getDriveStatus",
            {},
        );
        return {
            hasDiscController: response.hasDiscController,
            controllerType: response.controllerType,
            isSocketed: response.isSocketed,
            installedControllerId: response.installedControllerId,
            drives: response.drives.map(toDriveStatus),
        };
    }

    /** List available disc controller types. */
    async getAvailableControllers(): Promise<DiscControllerInfo[]> {
        const response = await promisify<{}, ProtoListAvailableControllersResponse>(
            this.stub as unknown as Record<string, Function>,
            "listAvailableControllers",
            {},
        );
        return response.controllers.map(toDiscControllerInfo);
    }

    /**
     * Install a disc controller into the socket.
     *
     * @param controllerId - CLI identifier (e.g. "acorn-1770").
     * @returns The controller type name that was installed.
     */
    async installController(controllerId: string): Promise<string> {
        const response = await promisify<{ controllerId: string }, ProtoInstallDiscControllerResponse>(
            this.stub as unknown as Record<string, Function>,
            "installDiscController",
            { controllerId },
        );
        if (!response.success) {
            throw new DiscError(`Install controller failed: ${response.error}`);
        }
        return response.controllerType;
    }

    /** Get whether the motor spin-up delay is enabled. */
    async getSpinUpDelayEnabled(): Promise<boolean> {
        const response = await promisify<{}, ProtoGetSpinUpDelayResponse>(
            this.stub as unknown as Record<string, Function>,
            "getSpinUpDelay",
            {},
        );
        return response.enabled;
    }

    /** Set the motor spin-up delay enabled/disabled. */
    async setSpinUpDelay(enabled: boolean): Promise<void> {
        const response = await promisify<{ enabled: boolean }, ProtoSetSpinUpDelayResponse>(
            this.stub as unknown as Record<string, Function>,
            "setSpinUpDelay",
            { enabled },
        );
        if (!response.success) {
            throw new DiscError(`Set spin-up delay failed: ${response.error}`);
        }
    }

    /** Stream disc events as an async iterable. */
    async *events(): AsyncIterable<DiscEvent> {
        const stream = this.stub.subscribeDiscEvents({});
        for await (const proto of toAsyncIterable(stream)) {
            yield toDiscEvent(proto);
        }
    }

    /**
     * Start a background disc event stream with a callback.
     *
     * Returns a handle that can be used to cancel the stream.
     */
    startBackgroundEvents(callback: (event: DiscEvent) => void): BackgroundStreamHandle {
        const stream = this.stub.subscribeDiscEvents({});
        return new BackgroundStreamHandle(
            stream,
            (message: unknown) => callback(toDiscEvent(message as ProtoDiscEvent)),
        );
    }
}
