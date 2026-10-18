/**
 * The run-state table in docs/typescript-client.md says what the JSDoc says
 * (#191). Every client method that needs the machine running or stopped, or
 * leaves it one way, carries two JSDoc lines, `Requires: ...` and
 * `Leaves the machine: ...`; the table collects them, and this test requires
 * the two to agree.
 */

import { describe, it, expect } from "vitest";
import { readFileSync, readdirSync } from "node:fs";
import { join } from "node:path";

const SRC_DIRPATH = join(__dirname, "..", "src");
const DOC_FILEPATH = join(__dirname, "..", "..", "..", "docs", "typescript-client.md");

/** Every documented method's run-state lines, by Class.method. */
function documented(): Map<string, [string, string]> {
    const found = new Map<string, [string, string]>();
    for (const filename of readdirSync(SRC_DIRPATH).filter((f) => f.endsWith(".ts"))) {
        const text = readFileSync(join(SRC_DIRPATH, filename), "utf8");
        let currentClass = "";
        const docAndDecl = /export class (\w+)|\/\*\*((?:(?!\*\/)[\s\S])*)\*\/\s*(?:static\s+)?(?:async\s+)?\*?\s*(\w+)\s*[<(]/g;
        for (const match of text.matchAll(docAndDecl)) {
            if (match[1] !== undefined) {
                currentClass = match[1];
                continue;
            }
            const doc = match[2]!;
            const requires = [...doc.matchAll(/^\s*\* Requires: (.+)\.$/gm)].map((m) => m[1]!);
            const leaves = [...doc.matchAll(/^\s*\* Leaves the machine: (.+)\.$/gm)].map((m) => m[1]!);
            if (requires.length === 0 && leaves.length === 0) continue;
            const where = `${currentClass}.${match[3]}`;
            expect(requires.length, `${where} Requires:`).toBe(1);
            expect(leaves.length, `${where} Leaves the machine:`).toBe(1);
            found.set(where, [requires[0]!, leaves[0]!]);
        }
    }
    return found;
}

/** The doc's run-state table, by Class.method. */
function tabled(): Map<string, [string, string]> {
    const text = readFileSync(DOC_FILEPATH, "utf8");
    const section = text.split("## Run state")[1]!.split("\n## ")[0]!;
    const rows = new Map<string, [string, string]>();
    for (const line of section.split("\n")) {
        const match = /^\| `(\w+)\.(\w+)` \| (.+) \| (.+) \|$/.exec(line);
        if (match) rows.set(`${match[1]}.${match[2]}`, [match[3]!, match[4]!]);
    }
    return rows;
}

describe("run-state documentation", () => {
    it("the table and the JSDoc agree", () => {
        const doc = documented();
        const table = tabled();
        expect(doc.size).toBeGreaterThan(0);
        expect([...doc.keys()].filter((k) => !table.has(k)).sort()).toEqual([]);
        expect([...table.keys()].filter((k) => !doc.has(k)).sort()).toEqual([]);
        for (const [where, lines] of doc) {
            expect(table.get(where), where).toEqual(lines);
        }
    });

    it("the lines use the shared words", () => {
        for (const [where, [requires, leaves]] of documented()) {
            expect(["RUNNING", "STOPPED", "unchanged", "as it was on entry"], where).toContain(leaves);
            expect(
                ["nothing", "any run state", "a running machine", "a stopped machine"]
                    .some((start) => requires.startsWith(start)),
                where,
            ).toBe(true);
        }
    });
});
