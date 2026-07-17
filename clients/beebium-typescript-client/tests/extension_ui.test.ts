import { describe, it, expect, vi } from "vitest";
import { EventEmitter } from "node:events";

import {
    ControlKind,
    DispatchResult,
    ExtensionUi,
    IndicatorState,
    View,
} from "../src/extension_ui.js";
import {
    Control as ProtoControl,
    Indicator_State,
    View as ProtoView,
} from "../src/generated/extension_ui.js";

/**
 * Fake ClientReadableStream shaped to the subset that BackgroundStreamHandle
 * and toAsyncIterable use: EventEmitter events (data, error, end) plus cancel().
 */
class FakeStream extends EventEmitter {
    public cancelled = false;
    cancel(): void {
        this.cancelled = true;
        this.emit("end");
    }
    /** Drive the stream: emit the given views in order, then close. */
    drive(views: ProtoView[]): void {
        setImmediate(() => {
            for (const v of views) {
                this.emit("data", v);
            }
            this.emit("end");
        });
    }
}

function makeViewProto(): ProtoView {
    const inner: ProtoControl = {
        id: "inner",
        group: {
            // label deliberately absent to exercise the undefined-label path
            label: undefined,
            controls: [],
        },
    };
    const label: ProtoControl = {
        id: "lbl",
        label: { text: "Hello", secondaryText: "world" },
    };
    const indicator: ProtoControl = {
        id: "ind",
        indicator: { state: Indicator_State.WARN, text: "watch out" },
    };
    const toggle: ProtoControl = {
        id: "tog",
        toggle: { label: "Enabled", value: true },
    };
    const button: ProtoControl = {
        id: "btn",
        button: { label: "Go", enabled: false },
    };
    const choice: ProtoControl = {
        id: "ch",
        choice: { label: "Mode", options: ["a", "b"], selectedIndex: 1 },
    };
    const textInput: ProtoControl = {
        id: "txt",
        textInput: { label: "Host", value: "127.0.0.1", placeholder: "ip" },
    };
    const root: ProtoControl = {
        id: "root",
        group: {
            label: "Outer",
            controls: [label, indicator, toggle, button, choice, textInput, inner],
        },
    };
    return {
        extensionId: "test",
        viewRevision: 7,
        root,
    };
}

describe("ExtensionUi", () => {
    describe("subscribeView", () => {
        it("yields a converted View with every Control variant", async () => {
            const stream = new FakeStream();
            const stub = {
                subscribeView: vi.fn(() => stream),
            };
            const client = new ExtensionUi(stub as any);

            const viewsIter = client.subscribeView("test");
            stream.drive([makeViewProto()]);

            const views: View[] = [];
            for await (const v of viewsIter) {
                views.push(v);
            }

            expect(views).toHaveLength(1);
            const view = views[0]!;
            expect(view.extensionId).toBe("test");
            expect(view.revision).toBe(7);
            expect(view.root.id).toBe("root");
            expect(view.root.kind).toBe(ControlKind.GROUP);
            expect(view.root.group).toBeDefined();
            expect(view.root.group!.label).toBe("Outer");

            const children = view.root.group!.children;
            expect(children).toHaveLength(7);
            const [lbl, ind, tog, btn, ch, txt, inner] = children;

            expect(lbl!.kind).toBe(ControlKind.LABEL);
            expect(lbl!.label!.text).toBe("Hello");
            expect(lbl!.label!.secondaryText).toBe("world");

            expect(ind!.kind).toBe(ControlKind.INDICATOR);
            expect(ind!.indicator!.state).toBe(IndicatorState.WARN);
            expect(ind!.indicator!.text).toBe("watch out");

            expect(tog!.kind).toBe(ControlKind.TOGGLE);
            expect(tog!.toggle!.label).toBe("Enabled");
            expect(tog!.toggle!.value).toBe(true);

            expect(btn!.kind).toBe(ControlKind.BUTTON);
            expect(btn!.button!.enabled).toBe(false);

            expect(ch!.kind).toBe(ControlKind.CHOICE);
            expect(ch!.choice!.options).toEqual(["a", "b"]);
            expect(ch!.choice!.selectedIndex).toBe(1);

            expect(txt!.kind).toBe(ControlKind.TEXT_INPUT);
            expect(txt!.textInput!.value).toBe("127.0.0.1");
            expect(txt!.textInput!.placeholder).toBe("ip");

            expect(inner!.kind).toBe(ControlKind.GROUP);
            expect(inner!.group!.label).toBeUndefined();
        });

        it("passes extensionId in the request", async () => {
            const stream = new FakeStream();
            const stub = { subscribeView: vi.fn(() => stream) };
            const client = new ExtensionUi(stub as any);
            const iter = client.subscribeView("piconet");
            stream.drive([]);
            for await (const _ of iter) {
                // drain
            }
            expect(stub.subscribeView).toHaveBeenCalledWith({
                extensionId: "piconet",
            });
        });
    });

    describe("dispatch", () => {
        function dispatchStub(response: { accepted: boolean; error?: string }) {
            return {
                dispatch: vi.fn((_request: any, callback: Function) => {
                    callback(null, { accepted: response.accepted, error: response.error ?? "" });
                }),
            };
        }

        it("maps boolean payload to boolValue", async () => {
            const stub = dispatchStub({ accepted: true });
            const client = new ExtensionUi(stub as any);
            const result = await client.dispatch("ext", "ctrl", 5, true);
            expect(result).toEqual<DispatchResult>({ accepted: true, error: "" });
            const sent = stub.dispatch.mock.calls[0]![0];
            expect(sent.extensionId).toBe("ext");
            expect(sent.controlId).toBe("ctrl");
            expect(sent.viewRevision).toBe(5);
            expect(sent.boolValue).toBe(true);
            expect(sent.stringValue).toBeUndefined();
            expect(sent.indexValue).toBeUndefined();
        });

        it("maps string payload to stringValue", async () => {
            const stub = dispatchStub({ accepted: true });
            const client = new ExtensionUi(stub as any);
            await client.dispatch("ext", "ctrl", 5, "hello");
            const sent = stub.dispatch.mock.calls[0]![0];
            expect(sent.stringValue).toBe("hello");
            expect(sent.boolValue).toBeUndefined();
            expect(sent.indexValue).toBeUndefined();
        });

        it("maps non-negative integer payload to indexValue", async () => {
            const stub = dispatchStub({ accepted: true });
            const client = new ExtensionUi(stub as any);
            await client.dispatch("ext", "ctrl", 5, 2);
            const sent = stub.dispatch.mock.calls[0]![0];
            expect(sent.indexValue).toBe(2);
            expect(sent.boolValue).toBeUndefined();
            expect(sent.stringValue).toBeUndefined();
        });

        it("leaves oneof unset for null payload", async () => {
            const stub = dispatchStub({ accepted: true });
            const client = new ExtensionUi(stub as any);
            await client.dispatch("ext", "ctrl", 5, null);
            const sent = stub.dispatch.mock.calls[0]![0];
            expect(sent.boolValue).toBeUndefined();
            expect(sent.stringValue).toBeUndefined();
            expect(sent.indexValue).toBeUndefined();
        });

        it("leaves oneof unset when payload is omitted (Button)", async () => {
            const stub = dispatchStub({ accepted: true });
            const client = new ExtensionUi(stub as any);
            await client.dispatch("ext", "ctrl", 5);
            const sent = stub.dispatch.mock.calls[0]![0];
            expect(sent.boolValue).toBeUndefined();
            expect(sent.stringValue).toBeUndefined();
            expect(sent.indexValue).toBeUndefined();
        });

        it("throws RangeError for negative integer payload", async () => {
            const client = new ExtensionUi({} as any);
            await expect(client.dispatch("ext", "ctrl", 5, -1)).rejects.toThrow(
                RangeError,
            );
            await expect(client.dispatch("ext", "ctrl", 5, -1)).rejects.toThrow(
                "non-negative",
            );
        });

        it("throws TypeError for non-integer number payload", async () => {
            const client = new ExtensionUi({} as any);
            await expect(client.dispatch("ext", "ctrl", 5, 3.14)).rejects.toThrow(
                TypeError,
            );
            await expect(client.dispatch("ext", "ctrl", 5, 3.14)).rejects.toThrow(
                "non-integer",
            );
        });

        it("propagates accepted=false and error from the server", async () => {
            const stub = dispatchStub({
                accepted: false,
                error: "stale event: client revision 1, current 7",
            });
            const client = new ExtensionUi(stub as any);
            const result = await client.dispatch("ext", "ctrl", 1, true);
            expect(result.accepted).toBe(false);
            expect(result.error).toContain("stale");
        });
    });

    describe("startBackgroundSubscription", () => {
        it("fires callback for each view and settles to not-running when stream ends", async () => {
            const stream = new FakeStream();
            const stub = { subscribeView: vi.fn(() => stream) };
            const client = new ExtensionUi(stub as any);
            const seen: View[] = [];

            const handle = client.startBackgroundSubscription("test", (v) => {
                seen.push(v);
            });

            stream.drive([makeViewProto(), makeViewProto()]);

            // Wait a tick for setImmediate-scheduled events to flush.
            await new Promise((resolve) => setImmediate(resolve));
            await new Promise((resolve) => setImmediate(resolve));

            expect(seen).toHaveLength(2);
            expect(seen[0]!.extensionId).toBe("test");
            expect(handle.isRunning).toBe(false);
        });

        it("stop() cancels the stream", async () => {
            const stream = new FakeStream();
            const stub = { subscribeView: vi.fn(() => stream) };
            const client = new ExtensionUi(stub as any);
            const handle = client.startBackgroundSubscription("test", () => {});
            expect(handle.isRunning).toBe(true);
            handle.stop();
            expect(stream.cancelled).toBe(true);
            expect(handle.isRunning).toBe(false);
        });
    });
});
