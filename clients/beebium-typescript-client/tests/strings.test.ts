import { describe, it, expect } from "vitest";
import {
    cStr,
    parseCStr,
    pascalStr,
    parsePascalStr,
    paddedStr,
    parsePaddedStr,
} from "../src/strings.js";

describe("cStr", () => {
    it("encodes a basic string with null terminator", () => {
        const buf = cStr("HELLO");
        expect(buf.length).toBe(6);
        expect(buf.toString("ascii", 0, 5)).toBe("HELLO");
        expect(buf[5]).toBe(0);
    });

    it("encodes an empty string as a single null byte", () => {
        const buf = cStr("");
        expect(buf.length).toBe(1);
        expect(buf[0]).toBe(0);
    });

    it("encodes a string that contains a null byte verbatim plus terminator", () => {
        // "ascii" encoding of "\0" produces a zero byte in the payload
        const buf = cStr("A\x00B");
        // Buffer.from("A\x00B", "ascii") => [0x41, 0x00, 0x42], then + null
        expect(buf.length).toBe(4);
        expect(buf[0]).toBe(0x41);
        expect(buf[1]).toBe(0x00);
        expect(buf[2]).toBe(0x42);
        expect(buf[3]).toBe(0x00);
    });
});

describe("parseCStr", () => {
    it("decodes a basic null-terminated string", () => {
        const buf = Buffer.from([0x48, 0x49, 0x00]); // "HI\0"
        expect(parseCStr(buf)).toBe("HI");
    });

    it("returns the whole buffer when no null is found", () => {
        const buf = Buffer.from([0x41, 0x42, 0x43]); // "ABC"
        expect(parseCStr(buf)).toBe("ABC");
    });

    it("returns empty string when null is at the start", () => {
        const buf = Buffer.from([0x00, 0x41, 0x42]);
        expect(parseCStr(buf)).toBe("");
    });

    it("stops at the first null byte", () => {
        const buf = Buffer.from([0x41, 0x00, 0x42, 0x00]);
        expect(parseCStr(buf)).toBe("A");
    });
});

describe("pascalStr", () => {
    it("encodes a basic string with length prefix", () => {
        const buf = pascalStr("HI");
        expect(buf.length).toBe(3);
        expect(buf[0]).toBe(2); // length byte
        expect(buf.toString("ascii", 1, 3)).toBe("HI");
    });

    it("encodes an empty string", () => {
        const buf = pascalStr("");
        expect(buf.length).toBe(1);
        expect(buf[0]).toBe(0);
    });

    it("encodes a 255-byte string (maximum length)", () => {
        const s = "A".repeat(255);
        const buf = pascalStr(s);
        expect(buf.length).toBe(256);
        expect(buf[0]).toBe(255);
    });

    it("throws for a string longer than 255 bytes", () => {
        const s = "A".repeat(256);
        expect(() => pascalStr(s)).toThrow("string too long for pascalStr");
    });
});

describe("parsePascalStr", () => {
    it("decodes a basic length-prefixed string", () => {
        const buf = Buffer.from([3, 0x41, 0x42, 0x43]); // len=3, "ABC"
        expect(parsePascalStr(buf)).toBe("ABC");
    });

    it("throws RangeError for empty buffer", () => {
        expect(() => parsePascalStr(Buffer.alloc(0))).toThrow(RangeError);
        expect(() => parsePascalStr(Buffer.alloc(0))).toThrow(
            "cannot parse pascalStr from empty data",
        );
    });

    it("throws when buffer is too short for indicated length", () => {
        const buf = Buffer.from([5, 0x41, 0x42]); // claims 5 bytes, only 2
        expect(() => parsePascalStr(buf)).toThrow("data too short");
    });

    it("decodes empty pascal string (length byte is 0)", () => {
        const buf = Buffer.from([0, 0xFF, 0xFF]); // length 0, trailing garbage
        expect(parsePascalStr(buf)).toBe("");
    });
});

describe("paddedStr", () => {
    it("encodes a string padded with spaces", () => {
        const buf = paddedStr("HI", 5);
        expect(buf.length).toBe(5);
        expect(buf.toString("ascii")).toBe("HI   ");
    });

    it("encodes a string at exact width (no padding needed)", () => {
        const buf = paddedStr("ABCDE", 5);
        expect(buf.length).toBe(5);
        expect(buf.toString("ascii")).toBe("ABCDE");
    });

    it("throws when string exceeds width", () => {
        expect(() => paddedStr("TOOLONG", 3)).toThrow("exceeds width");
    });

    it("supports a custom pad character", () => {
        const buf = paddedStr("AB", 5, "*");
        expect(buf.toString("ascii")).toBe("AB***");
    });

    it("throws when pad encodes to multiple bytes", () => {
        // A character that encodes to more than 1 byte in UTF-8 but
        // ascii encoding maps it to a single byte. Use a two-char string instead.
        expect(() => paddedStr("A", 3, "**")).toThrow(
            "pad must be a single character",
        );
    });

    it("encodes empty string as all padding", () => {
        const buf = paddedStr("", 4, ".");
        expect(buf.toString("ascii")).toBe("....");
    });
});

describe("parsePaddedStr", () => {
    it("strips trailing spaces by default", () => {
        const buf = Buffer.from("HI   ", "ascii");
        expect(parsePaddedStr(buf)).toBe("HI");
    });

    it("strips trailing custom pad character", () => {
        const buf = Buffer.from("AB***", "ascii");
        expect(parsePaddedStr(buf, "*")).toBe("AB");
    });

    it("returns the full string when no padding is present", () => {
        const buf = Buffer.from("ABCDE", "ascii");
        expect(parsePaddedStr(buf)).toBe("ABCDE");
    });

    it("returns empty string when buffer is all padding", () => {
        const buf = Buffer.from("     ", "ascii");
        expect(parsePaddedStr(buf)).toBe("");
    });
});
