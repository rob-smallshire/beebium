/**
 * Client for the job-file-printer extension's typed API.
 *
 * The job-file-printer extension writes what the BBC prints as one file per
 * print job in a directory on the host, a job ending after a quiet period in
 * emulated time. This client reads which job is being written and how many
 * have finished, and switches the printer online or offline -- the
 * scripting-friendly equivalent of the sidebar panel. Requires the server to
 * be launched with --job-file-printer directory=<dir>.
 *
 * The JobFilePrinter messages are tunnelled over the core's ExtensionRpc channel.
 */

import {
    JobFilePrinterGetStatusRequest,
    JobFilePrinterSetOnlineRequest,
    JobFilePrinterStatus as ProtoJobFilePrinterStatus,
} from "./generated/job_file_printer.js";
import type { ExtensionChannel } from "./extension_rpc.js";

/** The logical service name the job-file-printer extension's dispatcher registers. */
const SERVICE = "JobFilePrinter";

export interface JobFilePrinterStatus {
    /** Where job files are written. */
    directory: string;
    /** Is the printer accepting bytes? */
    online: boolean;
    /** The job file being written, or "" between jobs. */
    currentJobFile: string;
    /** Bytes written to the current job file. */
    bytesInJob: number;
    /** Jobs ended since launch. */
    jobsCompleted: number;
    /** Bytes written to all job files, after line-ending translation. */
    totalBytes: number;
}

/** Read the job file printer's status and switch it online or offline. */
export class JobFilePrinter {
    private readonly channel: ExtensionChannel;

    constructor(channel: ExtensionChannel) {
        this.channel = channel;
    }

    /** The directory, the job being written, the counts and the online state. */
    async getStatus(): Promise<JobFilePrinterStatus> {
        const payload = JobFilePrinterGetStatusRequest.encode(
            JobFilePrinterGetStatusRequest.fromPartial({}),
        ).finish();
        const reply = await this.channel.invoke(SERVICE, "GetStatus", payload);
        return fromProto(ProtoJobFilePrinterStatus.decode(reply));
    }

    /**
     * Switch the printer online or offline. Offline, the receiver holds the
     * byte the BBC strobed and withholds /ACK, so the guest waits as it would
     * for a switched-off printer; online again, it accepts the byte and the
     * print resumes. Returns the status after the change. Never blocks.
     */
    async setOnline(online: boolean): Promise<JobFilePrinterStatus> {
        const payload = JobFilePrinterSetOnlineRequest.encode(
            JobFilePrinterSetOnlineRequest.fromPartial({ online }),
        ).finish();
        const reply = await this.channel.invoke(SERVICE, "SetOnline", payload);
        return fromProto(ProtoJobFilePrinterStatus.decode(reply));
    }
}

function fromProto(response: ProtoJobFilePrinterStatus): JobFilePrinterStatus {
    return {
        directory: response.directory,
        online: response.online,
        currentJobFile: response.currentJobFile,
        bytesInJob: response.bytesInJob,
        jobsCompleted: response.jobsCompleted,
        totalBytes: response.totalBytes,
    };
}
