/**
 * Beebium TypeScript client library.
 *
 * Provides a typed interface for controlling BBC Micro emulator instances
 * via gRPC.
 */

export { Beebium } from "./client.js";
export { Connection } from "./connection.js";
export { ServerProcess, type ServerProcessOptions } from "./server-process.js";

export { Debugger, type ExecutionState, type ExecutionStateEvent, type Breakpoint, type StepResult, StopReason } from "./debugger.js";
export { CPU, type Registers, carry, zero, interruptDisable, decimal, breakFlag, overflow, negative, formatRegisters } from "./cpu.js";
export { Memory, AddressSpace, BusAccessor, PeekAccessor, Region, type MemoryRegionInfo } from "./memory.js";
export { Keyboard, type KeyboardState, type LockState } from "./keyboard.js";
export {
    Video,
    type VideoConfig,
    type Frame,
    type TeletextScreen,
    type PixelRegion,
    type ScreenText,
    type ScreenTextRun,
    type ScreenTextCell,
    type ScreenTextSearchMode,
    type ScreenTextCharactersMode,
    type ScreenGeometry,
    type ScreenBandGeometry,
    TELETEXT_ROWS,
    TELETEXT_COLUMNS,
} from "./video.js";
export { System, type Provenance, type MachineIdentity, type ServerStatusEvent, type ShutdownResponse, type ShutdownConditionStatus, type AdvertisementState, type PacingStats, ServerStatus, ShutdownMode } from "./system.js";
export { Disc, Drive, type DiscMetadata, type DriveStatus, type DiscControllerStatus, type DiscEvent, type DiscControllerInfo, DriveState, DiscEventType } from "./disc.js";
export { Econet, type EconetStatus, type AdlcStatus, type HandshakeStatus } from "./econet.js";
export { Serial, type SerialStatus } from "./serial.js";
export { ExtensionChannel } from "./extension_rpc.js";
export { RpcSerial, type RpcSerialStatus } from "./rpc_serial.js";
export {
    HostSerial,
    type HostSerialConfig,
    type HostSerialSetConfigOptions,
} from "./host_serial.js";
export { Tube, type TubeStatus } from "./tube.js";
export { Aun, type AunStatus, type PeerInfo, PeerSource } from "./aun.js";
export { Piconet, type PiconetStatus } from "./piconet.js";
export { EconetTransport, type TransportInfo } from "./econet_transport.js";
export {
    ExtensionUi,
    SubscriptionHandle,
    IndicatorState,
    ControlKind,
    type Label,
    type Indicator,
    type Toggle,
    type Button,
    type Choice,
    type TextInput,
    type Group,
    type Control,
    type View,
    type DispatchResult,
} from "./extension_ui.js";
export { Via, ViaId, type ViaState } from "./via.js";
export { Crtc, type CrtcState } from "./crtc.js";
export { VideoUla, type VideoUlaState } from "./video-ula.js";
export { AddressableLatch, type AddressableLatchState } from "./latch.js";
export { Sound, type SoundChannelState, type SoundGeneratorState } from "./sound.js";
export { TubeUlaInspection, type TubeUlaState } from "./tube-ula.js";
export { Basic } from "./basic.js";
export { TubeSystem } from "./tube-system.js";
export { cStr, parseCStr, pascalStr, parsePascalStr, paddedStr, parsePaddedStr } from "./strings.js";
export { readMode7Screen, screenContains, SCREEN_MODES, MODE7_BASE, MODE7_BYTES_PER_LINE, MODE7_LINES, type ReadFn, type ScreenModeInfo } from "./screen.js";

export {
    BeebiumError,
    ConnectionError,
    ServerStartupError,
    ServerNotFoundError,
    DebuggerError,
    InvalidConditionError,
    MemoryAccessError,
    TimeoutError,
    DiscError,
    EconetError,
    ProtocolMismatchError,
} from "./exceptions.js";

export const DEFAULT_GRPC_PORT = 0xBEEB;
export { VERSION } from "./version.js";
export { PROTOCOL_FINGERPRINT } from "./protocol_fingerprint.js";
