import { describe, it, expect } from "vitest";
import { promisify, promisifyWithMetadata } from "../src/call-utils.js";
import { ConnectionError } from "../src/exceptions.js";

describe("promisify", () => {
    it("resolves when callback succeeds", async () => {
        const mockClient: Record<string, Function> = {
            testMethod: (_request: unknown, callback: (err: null, response: { value: number }) => void) => {
                callback(null, { value: 42 });
            },
        };

        const result = await promisify<{}, { value: number }>(mockClient, "testMethod", {});
        expect(result).toEqual({ value: 42 });
    });

    it("rejects with ConnectionError when callback has error", async () => {
        const mockClient: Record<string, Function> = {
            failMethod: (_request: unknown, callback: (err: Error, response: null) => void) => {
                callback(new Error("server unavailable") as any, null);
            },
        };

        await expect(
            promisify<{}, unknown>(mockClient, "failMethod", {}),
        ).rejects.toThrow(ConnectionError);
        await expect(
            promisify<{}, unknown>(mockClient, "failMethod", {}),
        ).rejects.toThrow("gRPC call failMethod failed: server unavailable");
    });

    it("rejects with ConnectionError when method does not exist", async () => {
        const mockClient: Record<string, Function> = {};

        await expect(
            promisify<{}, unknown>(mockClient, "nonExistent", {}),
        ).rejects.toThrow(ConnectionError);
        await expect(
            promisify<{}, unknown>(mockClient, "nonExistent", {}),
        ).rejects.toThrow('Method "nonExistent" not found on client');
    });

    it("passes the request to the method", async () => {
        let receivedRequest: unknown = null;
        const mockClient: Record<string, Function> = {
            echoMethod: (request: unknown, callback: (err: null, response: unknown) => void) => {
                receivedRequest = request;
                callback(null, request);
            },
        };

        await promisify<{ data: string }, { data: string }>(
            mockClient,
            "echoMethod",
            { data: "hello" },
        );
        expect(receivedRequest).toEqual({ data: "hello" });
    });
});

describe("promisifyWithMetadata", () => {
    it("resolves when callback succeeds", async () => {
        const mockClient: Record<string, Function> = {
            testMethod: (
                _request: unknown,
                _metadata: unknown,
                callback: (err: null, response: { value: number }) => void,
            ) => {
                callback(null, { value: 99 });
            },
        };

        // Create a minimal metadata-like object
        const metadata = {} as any;
        const result = await promisifyWithMetadata<{}, { value: number }>(
            mockClient,
            "testMethod",
            {},
            metadata,
        );
        expect(result).toEqual({ value: 99 });
    });

    it("rejects with ConnectionError when callback has error", async () => {
        const mockClient: Record<string, Function> = {
            failMethod: (
                _request: unknown,
                _metadata: unknown,
                callback: (err: Error, response: null) => void,
            ) => {
                callback(new Error("deadline exceeded") as any, null);
            },
        };

        const metadata = {} as any;
        await expect(
            promisifyWithMetadata<{}, unknown>(mockClient, "failMethod", {}, metadata),
        ).rejects.toThrow(ConnectionError);
        await expect(
            promisifyWithMetadata<{}, unknown>(mockClient, "failMethod", {}, metadata),
        ).rejects.toThrow("gRPC call failMethod failed: deadline exceeded");
    });

    it("rejects with ConnectionError when method does not exist", async () => {
        const mockClient: Record<string, Function> = {};
        const metadata = {} as any;

        await expect(
            promisifyWithMetadata<{}, unknown>(mockClient, "missing", {}, metadata),
        ).rejects.toThrow(ConnectionError);
        await expect(
            promisifyWithMetadata<{}, unknown>(mockClient, "missing", {}, metadata),
        ).rejects.toThrow('Method "missing" not found on client');
    });

    it("passes metadata to the method", async () => {
        let receivedMetadata: unknown = null;
        const mockClient: Record<string, Function> = {
            metaMethod: (
                _request: unknown,
                metadata: unknown,
                callback: (err: null, response: unknown) => void,
            ) => {
                receivedMetadata = metadata;
                callback(null, {});
            },
        };

        const metadata = { key: "value" } as any;
        await promisifyWithMetadata<{}, unknown>(mockClient, "metaMethod", {}, metadata);
        expect(receivedMetadata).toEqual({ key: "value" });
    });
});
