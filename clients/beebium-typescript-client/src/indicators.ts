/**
 * Hardware indicator (LED, motor activity) state access for the
 * Beebium TypeScript client.
 *
 * Indicators are named observable signals (`caps-lock-led`,
 * `shift-lock-led`, `floppy-0-activity-led`, etc.) with values in the
 * range 0..255. Some indicators are PWM/duty-cycle filtered on the
 * server, so intermediate values can appear during fast transitions --
 * the static helpers `isDefinitiveOn`, `isDefinitiveOff`, and
 * `isTransient` make those transients explicit at the call site.
 */

import { promisify } from "./call-utils.js";
import type {
    GetIndicatorsRequest,
    GetIndicatorsResponse,
    IndicatorInfo,
    IndicatorServiceClient,
    ListIndicatorsRequest,
    ListIndicatorsResponse,
} from "./generated/indicator.js";

/** Definitive-off threshold for an indicator value. */
export const INDICATOR_DEFINITIVE_OFF = 0;
/** Definitive-on threshold for an indicator value. */
export const INDICATOR_DEFINITIVE_ON = 255;

/**
 * Client for the IndicatorService.
 *
 * @example
 *     const brightness = await bbc.indicators.get("caps-lock-led");
 *     if (Indicators.isDefinitiveOn(brightness)) { ... }
 */
export class Indicators {
    private readonly _stub: IndicatorServiceClient;

    constructor(stub: IndicatorServiceClient) {
        this._stub = stub;
    }

    /** Get the current value of every registered indicator. */
    async getAll(): Promise<Map<string, number>> {
        const response = await promisify<GetIndicatorsRequest, GetIndicatorsResponse>(
            this._stub as unknown as Record<string, Function>,
            "getIndicators",
            { ifChangedSince: 0 },
        );
        const out = new Map<string, number>();
        for (const [name, value] of Object.entries(response.values)) {
            out.set(name, value as number);
        }
        return out;
    }

    /**
     * Get the current value of a single indicator.
     *
     * @throws Error if no indicator with that name is registered.
     */
    async get(name: string): Promise<number> {
        const values = await this.getAll();
        const value = values.get(name);
        if (value === undefined) {
            throw new Error(`No indicator named '${name}'`);
        }
        return value;
    }

    /** List all registered indicators with their metadata. */
    async list(): Promise<IndicatorInfo[]> {
        const response = await promisify<ListIndicatorsRequest, ListIndicatorsResponse>(
            this._stub as unknown as Record<string, Function>,
            "listIndicators",
            {},
        );
        return [...response.indicators];
    }

    /**
     * True if `value` represents an unambiguous "on" reading.
     *
     * PWM/duty-cycle filtered indicators can produce intermediate
     * values during transitions. Only 255 is treated as definitively
     * on so callers don't act on transients.
     */
    static isDefinitiveOn(value: number): boolean {
        return value === INDICATOR_DEFINITIVE_ON;
    }

    /** True if `value` represents an unambiguous "off" reading. */
    static isDefinitiveOff(value: number): boolean {
        return value === INDICATOR_DEFINITIVE_OFF;
    }

    /** True if `value` is between off and on (PWM-filter transient). */
    static isTransient(value: number): boolean {
        return value > INDICATOR_DEFINITIVE_OFF && value < INDICATOR_DEFINITIVE_ON;
    }
}
