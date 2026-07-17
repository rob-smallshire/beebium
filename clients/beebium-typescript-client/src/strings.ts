/**
 * String encoding utilities for BBC Micro memory access.
 *
 * Provides functions for encoding and decoding strings in formats
 * commonly used on the BBC Micro:
 *
 * - C-style null-terminated strings (ASCIIZ)
 * - Pascal-style length-prefixed strings (max 255 bytes)
 * - Fixed-width padded strings
 *
 * These compose with the memory read/write methods:
 *
 *     import { cStr, pascalStr, paddedStr } from "./strings.js";
 *     import { parseCStr, parsePascalStr, parsePaddedStr } from "./strings.js";
 *
 *     // Writing
 *     await bbc.memory.address.bus.write(0x1000, cStr("HELLO"));
 *     await bbc.memory.address.bus.write(0x2000, pascalStr("WORLD"));
 *     await bbc.memory.address.bus.write(0x3000, paddedStr("TEST", 8));
 *
 *     // Reading
 *     const name = parseCStr(await bbc.memory.address.bus.read(0x1000, 256));
 *     const msg = parsePascalStr(await bbc.memory.address.bus.read(0x2000, 256));
 *     const field = parsePaddedStr(await bbc.memory.address.bus.read(0x3000, 8));
 */

/**
 * Encode a string as null-terminated bytes (C-style / ASCIIZ).
 *
 * @param s - The string to encode.
 * @param encoding - Character encoding (default "ascii").
 * @returns Buffer containing the encoded string followed by a null terminator.
 */
export function cStr(s: string, encoding: BufferEncoding = "ascii"): Buffer {
    const encoded = Buffer.from(s, encoding);
    const result = Buffer.alloc(encoded.length + 1);
    encoded.copy(result);
    result[encoded.length] = 0;
    return result;
}

/**
 * Decode a null-terminated string from bytes.
 *
 * Reads up to the first null byte. If no null is found, decodes the
 * entire buffer.
 *
 * @param data - The bytes to decode.
 * @param encoding - Character encoding (default "ascii").
 * @returns The decoded string (excluding the null terminator).
 */
export function parseCStr(data: Buffer, encoding: BufferEncoding = "ascii"): string {
    const nullPos = data.indexOf(0);
    if (nullPos === -1) {
        return data.toString(encoding);
    }
    return data.subarray(0, nullPos).toString(encoding);
}

/**
 * Encode a string as length-prefixed bytes (Pascal-style).
 *
 * The result is a single length byte followed by the string bytes.
 * Maximum string length is 255 bytes.
 *
 * @param s - The string to encode.
 * @param encoding - Character encoding (default "ascii").
 * @returns Buffer containing a length byte followed by the encoded string.
 * @throws Error if the encoded string exceeds 255 bytes.
 */
export function pascalStr(s: string, encoding: BufferEncoding = "ascii"): Buffer {
    const encoded = Buffer.from(s, encoding);
    if (encoded.length > 255) {
        throw new Error(
            `string too long for pascalStr: ${encoded.length} bytes (max 255)`,
        );
    }
    const result = Buffer.alloc(1 + encoded.length);
    result[0] = encoded.length;
    encoded.copy(result, 1);
    return result;
}

/**
 * Decode a length-prefixed string from bytes.
 *
 * Reads the length from the first byte, then decodes that many
 * subsequent bytes.
 *
 * @param data - The bytes to decode (must have at least 1 byte).
 * @param encoding - Character encoding (default "ascii").
 * @returns The decoded string.
 * @throws RangeError if data is empty.
 * @throws Error if data is shorter than the indicated length.
 */
export function parsePascalStr(data: Buffer, encoding: BufferEncoding = "ascii"): string {
    if (data.length === 0) {
        throw new RangeError("cannot parse pascalStr from empty data");
    }
    const length = data[0]!;
    if (data.length < 1 + length) {
        throw new Error(
            `data too short: need ${1 + length} bytes, got ${data.length}`,
        );
    }
    return data.subarray(1, 1 + length).toString(encoding);
}

/**
 * Encode a string as fixed-width padded bytes.
 *
 * The string is right-padded to the specified width.
 *
 * @param s - The string to encode.
 * @param width - The fixed width in bytes.
 * @param pad - The padding character (default space).
 * @param encoding - Character encoding (default "ascii").
 * @returns Buffer containing the encoded string padded to the specified width.
 * @throws Error if the encoded string exceeds the specified width.
 * @throws Error if pad is not a single character.
 */
export function paddedStr(
    s: string,
    width: number,
    pad = " ",
    encoding: BufferEncoding = "ascii",
): Buffer {
    const encoded = Buffer.from(s, encoding);
    if (encoded.length > width) {
        throw new Error(
            `string length ${encoded.length} exceeds width ${width}`,
        );
    }
    const padByte = Buffer.from(pad, encoding);
    if (padByte.length !== 1) {
        throw new Error(
            `pad must be a single character, got ${padByte.length} bytes`,
        );
    }
    const result = Buffer.alloc(width, padByte[0]);
    encoded.copy(result);
    return result;
}

/**
 * Decode a padded string by stripping trailing pad characters.
 *
 * @param data - The bytes to decode.
 * @param pad - The padding character to strip (default space).
 * @param encoding - Character encoding (default "ascii").
 * @returns The decoded string with trailing pad characters removed.
 */
export function parsePaddedStr(
    data: Buffer,
    pad = " ",
    encoding: BufferEncoding = "ascii",
): string {
    const decoded = data.toString(encoding);
    // Strip trailing pad characters
    let end = decoded.length;
    while (end > 0 && decoded[end - 1] === pad) {
        end--;
    }
    return decoded.substring(0, end);
}
