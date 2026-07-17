/**
 * Tube system abstraction for host-parasite debugging.
 *
 * Manages both host and parasite as a single unit for coordinated
 * execution control, breakpointing, and predicate-based stopping.
 */

import type { Beebium } from "./client.js";

export class TubeSystem {
    private readonly host: Beebium;
    private readonly parasite: Beebium;

    /**
     * Create a Tube system from existing host and parasite clients.
     *
     * @param host - The host BBC Micro client.
     * @param parasite - The parasite (second processor) client.
     */
    constructor(host: Beebium, parasite: Beebium) {
        this.host = host;
        this.parasite = parasite;
    }

    /**
     * Create a Tube system from the host.
     *
     * The parasite client shares the same gRPC connection as the host,
     * routing debugger calls to the ParasiteDebuggerControl service.
     */
    static async fromHost(host: Beebium): Promise<TubeSystem> {
        const parasite = await host.connectParasite();
        return new TubeSystem(host, parasite);
    }

    /** Get the host client. */
    getHost(): Beebium {
        return this.host;
    }

    /** Get the parasite client. */
    getParasite(): Beebium {
        return this.parasite;
    }

    /** Run both processors. Ensures both are running afterwards. */
    async run(): Promise<void> {
        if (!await this.host.debugger.isRunning()) await this.host.debugger.run();
        if (!await this.parasite.debugger.isRunning()) await this.parasite.debugger.run();
    }

    /** Stop both processors. */
    async stop(): Promise<void> {
        if (await this.host.debugger.isRunning()) await this.host.debugger.stop();
        if (await this.parasite.debugger.isRunning()) await this.parasite.debugger.stop();
    }

    /**
     * Run both processors until predicate returns true or the budget expires.
     *
     * Execution proceeds in chunks of emulated time. At the end of each
     * chunk, both processors stop (via a server-side cycle-budget
     * breakpoint on the host with stopCounterpart), the predicate is
     * evaluated, and if false, both resume.
     */
    async runUntil(
        predicate: () => Promise<boolean>,
        emulatedSeconds: number,
        chunkSeconds = 0.1,
    ): Promise<boolean> {
        const clockHz = await this.host.system.getClockSpeedHz() || 2_000_000;
        const totalBudget = Math.round(emulatedSeconds * clockHz);
        const chunkCycles = Math.round(chunkSeconds * clockHz);
        const startCycles = (await this.host.debugger.getState()).cycleCount;
        const deadlineCycles = startCycles + totalBudget;

        try {
            while ((await this.host.debugger.getState()).cycleCount < deadlineCycles) {
                const currentCycles = (await this.host.debugger.getState()).cycleCount;
                const chunkTarget = Math.min(currentCycles + chunkCycles, deadlineCycles);

                const bpId = await this.host.debugger.addBreakpoint(0x0000, {
                    endAddress: 0x10000,
                    condition: `cycles >= ${chunkTarget}`,
                    stopCounterpart: true,
                });

                await this.stop();
                const iter = this.host.debugger.watchExecutionState();
                const initial = await iter.next();
                const runSequence = initial.value!.state.sequence;
                await this.run();
                for await (const event of iter) {
                    if (!event.state.isRunning && event.state.sequence > runSequence) break;
                }

                if (await this.parasite.debugger.isRunning()) {
                    await this.parasite.debugger.stop();
                }
                await this.host.debugger.removeBreakpoint(bpId);

                if (await predicate()) {
                    return true;
                }
            }
            return predicate();
        } finally {
            await this.stop();
        }
    }

    /**
     * Run both processors for the given emulated time.
     */
    async runFor(emulatedSeconds: number): Promise<void> {
        await this.runUntil(async () => false, emulatedSeconds);
    }

    /**
     * Close the Tube system, stopping both processors.
     *
     * The host is not closed (the caller owns it). The parasite
     * shares the host's connection and does not need separate cleanup.
     */
    async close(): Promise<void> {
        await this.stop();
    }
}
