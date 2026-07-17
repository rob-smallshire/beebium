import { describe, it, expect } from "vitest";
import { Connection } from "../src/connection.js";
import { ConnectionError } from "../src/exceptions.js";

describe("Connection", () => {
    it("stores target correctly", () => {
        const conn = new Connection("localhost:50051");
        expect(conn.target).toBe("localhost:50051");
    });

    it("isConnected is true initially", () => {
        const conn = new Connection("localhost:50051");
        expect(conn.isConnected).toBe(true);
    });

    it("close() makes isConnected false", () => {
        const conn = new Connection("localhost:50051");
        conn.close();
        expect(conn.isConnected).toBe(false);
    });

    it("close() is idempotent", () => {
        const conn = new Connection("localhost:50051");
        conn.close();
        conn.close(); // second call should not throw
        expect(conn.isConnected).toBe(false);
    });

    it("accessing debuggerStub after close() throws ConnectionError", () => {
        const conn = new Connection("localhost:50051");
        conn.close();
        expect(() => conn.debuggerStub).toThrow(ConnectionError);
        expect(() => conn.debuggerStub).toThrow("Connection is closed");
    });

    it("accessing systemStub after close() throws ConnectionError", () => {
        const conn = new Connection("localhost:50051");
        conn.close();
        expect(() => conn.systemStub).toThrow(ConnectionError);
    });

    it("accessing keyboardStub after close() throws ConnectionError", () => {
        const conn = new Connection("localhost:50051");
        conn.close();
        expect(() => conn.keyboardStub).toThrow(ConnectionError);
    });

    it("accessing videoStub after close() throws ConnectionError", () => {
        const conn = new Connection("localhost:50051");
        conn.close();
        expect(() => conn.videoStub).toThrow(ConnectionError);
    });

    it("accessing discStub after close() throws ConnectionError", () => {
        const conn = new Connection("localhost:50051");
        conn.close();
        expect(() => conn.discStub).toThrow(ConnectionError);
    });

    it("accessing deviceInspectionStub after close() throws ConnectionError", () => {
        const conn = new Connection("localhost:50051");
        conn.close();
        expect(() => conn.deviceInspectionStub).toThrow(ConnectionError);
    });

    it("accessing econetStub after close() throws ConnectionError", () => {
        const conn = new Connection("localhost:50051");
        conn.close();
        expect(() => conn.econetStub).toThrow(ConnectionError);
    });

    it("accessing tubeStub after close() throws ConnectionError", () => {
        const conn = new Connection("localhost:50051");
        conn.close();
        expect(() => conn.tubeStub).toThrow(ConnectionError);
    });

    it("accessing extensionRpcStub after close() throws ConnectionError", () => {
        const conn = new Connection("localhost:50051");
        conn.close();
        expect(() => conn.extensionRpcStub).toThrow(ConnectionError);
    });

    it("accessing econetTransportStub after close() throws ConnectionError", () => {
        const conn = new Connection("localhost:50051");
        conn.close();
        expect(() => conn.econetTransportStub).toThrow(ConnectionError);
    });

    it("accessing extensionUiStub after close() throws ConnectionError", () => {
        const conn = new Connection("localhost:50051");
        conn.close();
        expect(() => conn.extensionUiStub).toThrow(ConnectionError);
    });
});
