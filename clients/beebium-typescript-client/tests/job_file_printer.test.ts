import { describe, it, expect, vi } from "vitest";
import { JobFilePrinter } from "../src/job_file_printer.js";
import type { ExtensionChannel } from "../src/extension_rpc.js";
import {
    JobFilePrinterSetOnlineRequest,
    JobFilePrinterStatus,
} from "../src/generated/job_file_printer.js";

type InvokeHandler = (service: string, method: string, payload: Uint8Array) => Uint8Array;

function mockChannel(handler: InvokeHandler) {
    const invoke = vi.fn(
        async (service: string, method: string, payload: Uint8Array) =>
            handler(service, method, payload),
    );
    return { channel: { invoke } as unknown as ExtensionChannel, invoke };
}

function encodeStatus(fields: Partial<JobFilePrinterStatus>): Uint8Array {
    return JobFilePrinterStatus.encode(JobFilePrinterStatus.fromPartial(fields)).finish();
}

describe("JobFilePrinter", () => {
    it("getStatus decodes the directory, the job, the counts and online state", async () => {
        const { channel, invoke } = mockChannel(() =>
            encodeStatus({
                directory: "/tmp/jobs",
                online: true,
                currentJobFile: "/tmp/jobs/job-20261024T153045Z.prn",
                bytesInJob: 4,
                jobsCompleted: 2,
                totalBytes: 12,
            }),
        );
        const printer = new JobFilePrinter(channel);

        const status = await printer.getStatus();
        expect(invoke).toHaveBeenCalledWith("JobFilePrinter", "GetStatus", expect.anything());
        expect(status).toEqual({
            directory: "/tmp/jobs",
            online: true,
            currentJobFile: "/tmp/jobs/job-20261024T153045Z.prn",
            bytesInJob: 4,
            jobsCompleted: 2,
            totalBytes: 12,
        });
    });

    it("setOnline tunnels the requested state and returns the new status", async () => {
        let seen: JobFilePrinterSetOnlineRequest | undefined;
        const { channel, invoke } = mockChannel((_service, _method, payload) => {
            seen = JobFilePrinterSetOnlineRequest.decode(payload);
            return encodeStatus({ directory: "/tmp/jobs", online: false });
        });
        const printer = new JobFilePrinter(channel);

        const status = await printer.setOnline(false);
        expect(invoke).toHaveBeenCalledWith("JobFilePrinter", "SetOnline", expect.anything());
        expect(seen!.online).toBe(false);
        expect(status.online).toBe(false);
    });
});
