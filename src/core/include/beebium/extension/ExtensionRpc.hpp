// Copyright 2026 Robert Smallshire <robert@smallshire.org.uk>
//
// This file is part of Beebium.
//
// Beebium is free software: you can redistribute it and/or modify it under the terms of the
// GNU General Public License as published by the Free Software Foundation, either version 3 of the
// License, or (at your option) any later version. Beebium is distributed in the hope that it will
// be useful, but WITHOUT ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or
// FITNESS FOR A PARTICULAR PURPOSE. See the GNU General Public License for more details.
// You should have received a copy of the GNU General Public License along with Beebium.
// If not, see <https://www.gnu.org/licenses/>.

#ifndef BEEBIUM_EXTENSION_EXTENSION_RPC_HPP
#define BEEBIUM_EXTENSION_EXTENSION_RPC_HPP

#include "Export.hpp"

#include <functional>
#include <map>
#include <string>
#include <string_view>

// The programmatic sibling of ExtensionUi. An extension exposes a remote
// control API by registering one or more ExtensionRpcDispatchers; the core
// hosts a single real gRPC service (ExtensionRpc) that carries opaque
// serialized bytes and routes them here. The extension never links gRPC, so
// there is exactly one gRPC runtime in the process and no exe<->plugin
// boundary for gRPC objects to cross. See
// docs/discussion/extension-rpc-channel.md.
//
// The extension's own request/response messages are still ordinary protobuf
// (the developer defines a normal `service` in a .proto), but only the
// extension serializes/parses them; to the core and the channel they are
// opaque `std::string` byte buffers. Nothing in this header references gRPC
// or the protobuf runtime, so it is safe for beebium_extension_api.

namespace beebium {

// Status returned by an extension RPC handler. The numeric codes mirror
// grpc::StatusCode so the core maps one straight onto a gRPC status without
// the extension linking gRPC. code 0 == OK.
struct RpcStatus {
    int code = 0;
    std::string message;

    static RpcStatus ok() { return {}; }
    static RpcStatus error(int c, std::string m) { return {c, std::move(m)}; }
    bool is_ok() const { return code == 0; }
};

// gRPC status code values, named for convenience in handlers. The integers
// match grpc::StatusCode exactly (the core relies on that mapping).
enum RpcStatusCode : int {
    kRpcOk = 0,
    kRpcCancelled = 1,
    kRpcUnknown = 2,
    kRpcInvalidArgument = 3,
    kRpcDeadlineExceeded = 4,
    kRpcNotFound = 5,
    kRpcAlreadyExists = 6,
    kRpcPermissionDenied = 7,
    kRpcResourceExhausted = 8,
    kRpcFailedPrecondition = 9,
    kRpcAborted = 10,
    kRpcOutOfRange = 11,
    kRpcUnimplemented = 12,
    kRpcInternal = 13,
    kRpcUnavailable = 14,
    kRpcDataLoss = 15,
    kRpcUnauthenticated = 16,
};

// Turn a protobuf SerializeToString() result into an RpcStatus. Extension
// dispatchers route their response serialization through this so a failure
// surfaces as an internal error rather than being silently discarded.
inline RpcStatus serialized(bool serialize_ok) {
    return serialize_ok ? RpcStatus::ok()
                        : RpcStatus::error(kRpcInternal, "failed to serialize response");
}

// Per-call context, implemented by the core over grpc::ServerContext. Lets a
// handler observe cancellation/deadline and read request metadata without
// touching gRPC types.
class BEEBIUM_EXT_TYPE_VISIBLE RpcContext {
public:
    BEEBIUM_EXT_API virtual ~RpcContext();

    // True once the peer has gone away or the deadline has passed. A
    // server-streaming handler should poll this and stop producing.
    virtual bool is_cancelled() const = 0;

    // Request headers (auth, trace ids, ...). Empty if none.
    virtual const std::map<std::string, std::string>& metadata() const = 0;
};

// Server-streaming sink, implemented by the core over grpc::ServerWriter.
// write() returns false once the peer is gone, so a handler stops producing
// rather than blocking -- the emulation thread is never stalled because the
// handler runs on a gRPC worker thread, exactly as today's services do.
class BEEBIUM_EXT_TYPE_VISIBLE RpcResponseWriter {
public:
    BEEBIUM_EXT_API virtual ~RpcResponseWriter();

    virtual bool write(std::string_view serialized_response) = 0;
};

// A dispatcher an extension registers to serve one logical service (the
// `service` in its .proto, e.g. "RpcSerial"). A generated subclass (later
// phase) implements typed methods; a hand-written subclass switches on the
// method name and (de)serializes its own messages.
//
// As with ExtensionUi, BEEBIUM_EXT_TYPE_VISIBLE exposes the vtable/typeinfo
// to consumer shared libraries on POSIX; the BEEBIUM_EXT_API destructor
// anchors the vtable in beebium_extension_api on Windows.
class BEEBIUM_EXT_TYPE_VISIBLE ExtensionRpcDispatcher {
public:
    BEEBIUM_EXT_API virtual ~ExtensionRpcDispatcher();

    // The logical service this dispatcher serves, used to route calls.
    virtual std::string_view service_name() const = 0;

    // Unary call. `request`/`response` are serialized extension messages,
    // opaque to the core. Return a non-OK RpcStatus to fail the call.
    virtual RpcStatus invoke(std::string_view method,
                             std::string_view request,
                             std::string& response,
                             RpcContext& ctx) = 0;

    // Server-streaming call. Write zero or more serialized responses via
    // `writer`; stop when it returns false or ctx.is_cancelled(). Defaults to
    // UNIMPLEMENTED so unary-only dispatchers need not override it.
    BEEBIUM_EXT_API virtual RpcStatus server_stream(std::string_view method,
                                                    std::string_view request,
                                                    RpcResponseWriter& writer,
                                                    RpcContext& ctx);

    // The bus quiescer. A dispatcher handler runs on a gRPC worker thread; when
    // it mutates device state the emulation thread also touches every cycle, it
    // must halt that thread across the mutation. The server injects a quiescer
    // that does so (it forwards to Machine::with_emulation_paused) and wires it
    // into every dispatcher; a dispatcher author never sets it. Mutate device
    // state through with_bus_stopped(), exactly as a coprocessor runner mutates
    // its debug entries through with_execution_stopped().
    //
    // set by the server only:
    void set_bus_quiescer(std::function<void(const std::function<void()>&)> quiescer) {
        bus_quiescer_ = std::move(quiescer);
    }

protected:
    // Run `fn` with the emulation thread halted, via the server-supplied
    // quiescer. When none is set (a standalone unit test with no running
    // machine) `fn` runs directly, which is safe single-threaded.
    void with_bus_stopped(const std::function<void()>& fn) {
        if (bus_quiescer_) {
            bus_quiescer_(fn);
        } else {
            fn();
        }
    }

private:
    std::function<void(const std::function<void()>&)> bus_quiescer_;
};

}  // namespace beebium

#endif  // BEEBIUM_EXTENSION_EXTENSION_RPC_HPP
