/**
 * Error hierarchy for the Beebium TypeScript client.
 */

/** Base error for all Beebium client errors. */
export class BeebiumError extends Error {
    constructor(message: string) {
        super(message);
        this.name = "BeebiumError";
    }
}

/** Failed to establish or maintain a gRPC connection to the server. */
export class ConnectionError extends BeebiumError {
    constructor(message: string) {
        super(message);
        this.name = "ConnectionError";
    }
}

/**
 * The server's wire protocol does not match this client's. Raised at connect
 * time when the server's protocol fingerprint differs from the client's
 * compiled-in fingerprint. Install a server and client built from the same
 * protocol.
 */
export class ProtocolMismatchError extends ConnectionError {
    constructor(message: string) {
        super(message);
        this.name = "ProtocolMismatchError";
    }
}

/** The emulator server failed to start. */
export class ServerStartupError extends BeebiumError {
    constructor(message: string) {
        super(message);
        this.name = "ServerStartupError";
    }
}

/** Could not locate a running emulator server. */
export class ServerNotFoundError extends BeebiumError {
    constructor(message: string) {
        super(message);
        this.name = "ServerNotFoundError";
    }
}

/** An error occurred during a debugger operation. */
export class DebuggerError extends BeebiumError {
    constructor(message: string) {
        super(message);
        this.name = "DebuggerError";
    }
}

/** A breakpoint or watchpoint condition expression is invalid. */
export class InvalidConditionError extends DebuggerError {
    constructor(message: string) {
        super(message);
        this.name = "InvalidConditionError";
    }
}

/** An invalid or failed memory access. */
export class MemoryAccessError extends BeebiumError {
    constructor(message: string) {
        super(message);
        this.name = "MemoryAccessError";
    }
}

/** An operation exceeded its deadline. */
export class TimeoutError extends BeebiumError {
    constructor(message: string) {
        super(message);
        this.name = "TimeoutError";
    }
}

/** An error related to disc operations. */
export class DiscError extends BeebiumError {
    constructor(message: string) {
        super(message);
        this.name = "DiscError";
    }
}

/** An error related to Econet operations. */
export class EconetError extends BeebiumError {
    constructor(message: string) {
        super(message);
        this.name = "EconetError";
    }
}
