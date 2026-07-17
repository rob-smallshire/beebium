/**
 * Wrapper for ExtensionUiService.
 *
 * The Extension UI framework lets any extension declare a typed control
 * tree (the View) which the server streams to clients. Clients render
 * natively and post user actions back through Dispatch. This module
 * wraps the gRPC SubscribeView / Dispatch RPCs and converts the proto
 * types into ergonomic interfaces.
 *
 * See docs/discussion/extension-ui-architecture.md for the design.
 *
 * Usage:
 *     // One-shot: consume the first view of the piconet panel.
 *     for await (const view of bbc.extensionUi.subscribeView("piconet")) {
 *         for (const child of view.root.group?.children ?? []) {
 *             console.log(child.id, child.kind);
 *         }
 *         break;
 *     }
 *
 *     // Toggle the piconet "Enabled" control.
 *     await bbc.extensionUi.dispatch("piconet", "mode_toggle", view.revision, false);
 *
 *     // Background: receive view updates as they arrive.
 *     const handle = bbc.extensionUi.startBackgroundSubscription(
 *         "piconet",
 *         (v) => console.log("revision", v.revision),
 *     );
 *     // ...
 *     handle.stop();
 */

import type {
    ExtensionUiServiceClient,
    View as ProtoView,
    Control as ProtoControl,
    DispatchRequest as ProtoDispatchRequest,
    DispatchResponse as ProtoDispatchResponse,
} from "./generated/extension_ui.js";
import { promisify } from "./call-utils.js";
import { BackgroundStreamHandle, toAsyncIterable } from "./stream-utils.js";

/**
 * Semantic state for an Indicator control.
 *
 * Renderers map this to whatever colour / icon convention their
 * platform prefers; the server reports semantics, not presentation.
 */
export enum IndicatorState {
    UNKNOWN = 0,
    OK = 1,
    WARN = 2,
    ERROR = 3,
}

/** Tag for the seven control primitives plus the structural Group. */
export enum ControlKind {
    LABEL = "label",
    INDICATOR = "indicator",
    TOGGLE = "toggle",
    BUTTON = "button",
    CHOICE = "choice",
    TEXT_INPUT = "text_input",
    GROUP = "group",
    UNSET = "unset",
}

export interface Label {
    text: string;
    /**
     * Optional muted caption shown alongside the primary text. Used
     * for provenance / status / size metadata that's logically
     * attached to the primary line. Empty string means no caption.
     */
    secondaryText: string;
}

export interface Indicator {
    state: IndicatorState;
    text: string;
}

export interface Toggle {
    label: string;
    value: boolean;
}

export interface Button {
    label: string;
    enabled: boolean;
}

export interface Choice {
    label: string;
    options: readonly string[];
    selectedIndex: number;
}

export interface TextInput {
    label: string;
    value: string;
    placeholder: string;
}

/** A nested group of controls. label is undefined for unlabelled groups. */
export interface Group {
    label: string | undefined;
    children: readonly Control[];
}

/**
 * A control in the View tree.
 *
 * Exactly one of label, indicator, toggle, button, choice, textInput,
 * group is populated, indicated by the kind discriminator.
 */
export interface Control {
    id: string;
    kind: ControlKind;
    label?: Label;
    indicator?: Indicator;
    toggle?: Toggle;
    button?: Button;
    choice?: Choice;
    textInput?: TextInput;
    group?: Group;
}

/** A complete view of an extension's UI at one revision. */
export interface View {
    extensionId: string;
    revision: number;
    root: Control;
}

/** Return value from Extension UI dispatch. */
export interface DispatchResult {
    accepted: boolean;
    error: string;
}

function convertControl(proto: ProtoControl | undefined): Control {
    if (!proto) {
        return { id: "", kind: ControlKind.UNSET };
    }
    if (proto.label !== undefined) {
        return {
            id: proto.id,
            kind: ControlKind.LABEL,
            label: {
                text: proto.label.text,
                secondaryText: proto.label.secondaryText,
            },
        };
    }
    if (proto.indicator !== undefined) {
        return {
            id: proto.id,
            kind: ControlKind.INDICATOR,
            indicator: {
                state: proto.indicator.state as number as IndicatorState,
                text: proto.indicator.text,
            },
        };
    }
    if (proto.toggle !== undefined) {
        return {
            id: proto.id,
            kind: ControlKind.TOGGLE,
            toggle: {
                label: proto.toggle.label,
                value: proto.toggle.value,
            },
        };
    }
    if (proto.button !== undefined) {
        return {
            id: proto.id,
            kind: ControlKind.BUTTON,
            button: {
                label: proto.button.label,
                enabled: proto.button.enabled,
            },
        };
    }
    if (proto.choice !== undefined) {
        return {
            id: proto.id,
            kind: ControlKind.CHOICE,
            choice: {
                label: proto.choice.label,
                options: [...proto.choice.options],
                selectedIndex: proto.choice.selectedIndex,
            },
        };
    }
    if (proto.textInput !== undefined) {
        return {
            id: proto.id,
            kind: ControlKind.TEXT_INPUT,
            textInput: {
                label: proto.textInput.label,
                value: proto.textInput.value,
                placeholder: proto.textInput.placeholder,
            },
        };
    }
    if (proto.group !== undefined) {
        const children = proto.group.controls.map(convertControl);
        return {
            id: proto.id,
            kind: ControlKind.GROUP,
            group: {
                label: proto.group.label,
                children,
            },
        };
    }
    return { id: proto.id, kind: ControlKind.UNSET };
}

function convertView(proto: ProtoView): View {
    return {
        extensionId: proto.extensionId,
        revision: proto.viewRevision,
        root: convertControl(proto.root),
    };
}

/** Handle for a background view subscription. */
export class SubscriptionHandle {
    private readonly handle: BackgroundStreamHandle;

    constructor(handle: BackgroundStreamHandle) {
        this.handle = handle;
    }

    /** Signal the background subscription to stop. */
    stop(): void {
        this.handle.cancel();
    }

    get isRunning(): boolean {
        return this.handle.isRunning;
    }
}

/**
 * Wraps ExtensionUiService.
 *
 * Use subscribeView to iterate over View updates for a named
 * extension, and dispatch to post a user action back. The server
 * delivers the result of an action as the next yielded View on the
 * subscription, not in the dispatch return -- DispatchResult.accepted
 * only reports whether the framework's validation gauntlet (extension
 * exists, control id known, payload type matches, revision current)
 * passed.
 */
export class ExtensionUi {
    private readonly stub: ExtensionUiServiceClient;

    constructor(stub: ExtensionUiServiceClient) {
        this.stub = stub;
    }

    /**
     * Open a server-stream of View updates for one extension.
     *
     * The first View arrives immediately; subsequent Views arrive as
     * the extension's state changes. The stream ends with NOT_FOUND
     * if the named extension does not exist or has no UI.
     */
    async *subscribeView(extensionId: string): AsyncGenerator<View, void, undefined> {
        const stream = this.stub.subscribeView({ extensionId });
        for await (const proto of toAsyncIterable(stream)) {
            yield convertView(proto);
        }
    }

    /**
     * Subscribe in the background; invoke callback on each View.
     *
     * Returns a handle whose stop() cancels the stream. Stream errors
     * (e.g. server disconnect, NOT_FOUND) terminate delivery silently
     * -- check handle.isRunning if the caller needs to detect that.
     */
    startBackgroundSubscription(
        extensionId: string,
        callback: (view: View) => void,
    ): SubscriptionHandle {
        const stream = this.stub.subscribeView({ extensionId });
        const handle = new BackgroundStreamHandle(
            stream,
            (message: unknown) => callback(convertView(message as ProtoView)),
        );
        return new SubscriptionHandle(handle);
    }

    /**
     * Post a user action back to the server.
     *
     * The payload is type-dispatched to match the addressed control's
     * expected variant:
     *
     *   * boolean                  -> Toggle
     *   * string                   -> TextInput
     *   * non-negative integer     -> Choice (selected index)
     *   * null / undefined         -> Button (no payload)
     *
     * Returns { accepted: false, error } if the framework's validation
     * gauntlet rejects the request (stale revision, unknown control,
     * payload-type mismatch, unknown extension). The actual mutation of
     * the extension's state is observable on the SubscribeView stream
     * as the next pushed View, not in the return value.
     *
     * @throws RangeError if the payload is a negative number.
     * @throws TypeError if the payload is a non-integer number or any
     *     other unsupported type.
     */
    async dispatch(
        extensionId: string,
        controlId: string,
        viewRevision: number,
        payload: boolean | string | number | null | undefined = undefined,
    ): Promise<DispatchResult> {
        const request: ProtoDispatchRequest = {
            extensionId,
            controlId,
            viewRevision,
        };

        if (payload === null || payload === undefined) {
            // leave the oneof unset (Button)
        } else if (typeof payload === "boolean") {
            request.boolValue = payload;
        } else if (typeof payload === "string") {
            request.stringValue = payload;
        } else if (typeof payload === "number") {
            if (!Number.isInteger(payload)) {
                throw new TypeError(
                    `dispatch payload must be boolean, string, integer, null, or undefined; got non-integer number ${payload}`,
                );
            }
            if (payload < 0) {
                throw new RangeError(
                    `Choice index payload must be non-negative; got ${payload}`,
                );
            }
            request.indexValue = payload;
        } else {
            throw new TypeError(
                `dispatch payload must be boolean, string, integer, null, or undefined; got ${typeof payload}`,
            );
        }

        const response = await promisify<ProtoDispatchRequest, ProtoDispatchResponse>(
            this.stub as unknown as Record<string, Function>,
            "dispatch",
            request,
        );
        return { accepted: response.accepted, error: response.error };
    }
}
