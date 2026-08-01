// Copyright © 2025 Robert Smallshire <robert@smallshire.org.uk>
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

import Foundation
import Network

/// A machine discovered via Bonjour
struct DiscoveredMachine: Identifiable, Hashable {
    let id: String           // Bonjour service name (unique within type)
    let instanceName: String // Machine name from service instance name
    let modelType: String    // "BBC Model B", etc. from TXT record
    let host: String         // Resolved hostname (without .local suffix for display)
    let port: Int
    let isLocal: Bool        // True if running on this Mac
    let uuid: String         // Machine UUID from TXT record
    let role: String         // "host" or "parasite" from TXT record
    let econetStation: Int?  // Econet station number (1-254), nil if not enabled
    let econetNet: Int?      // Econet network number, nil if not in AUN mode
    let econetAunPort: Int?  // AUN UDP port, nil if not in AUN mode

    var target: ConnectionTarget {
        ConnectionTarget(host: host, port: port)
    }

    /// Display hostname (cleaned up for UI)
    var displayHost: String {
        // Remove .local suffix if present
        if host.hasSuffix(".local.") {
            return String(host.dropLast(7))
        } else if host.hasSuffix(".local") {
            return String(host.dropLast(6))
        }
        return host
    }
}

/// Discovers Beebium cores on the network via Bonjour/mDNS
@MainActor
class DiscoveryClient: ObservableObject {
    /// The app-wide browser.
    ///
    /// Shared because more than one thing needs to know what is on the
    /// network: the Connect dialog lists it, naming a new machine avoids it,
    /// and a window says so when its machine's name is not unique. Browsing
    /// once for all of them beats a browser per dialog.
    static let shared = DiscoveryClient()

    @Published private(set) var machines: [DiscoveredMachine] = []
    @Published private(set) var isDiscoveryAvailable: Bool = true

    /// Names of machines currently visible on the network.
    ///
    /// Whatever discovery happens to know this instant -- never a wait for it
    /// to find more. A name is worth avoiding if we already know about it,
    /// but not worth stalling a machine's launch for, and if Bonjour is
    /// unavailable this is simply empty and naming carries on regardless.
    var knownMachineNames: Set<String> {
        Set(machines.map(\.instanceName).filter { !$0.isEmpty })
    }

    /// Machines on the network other than the one with this UUID.
    ///
    /// A machine advertises itself, so it finds its own name out there; the
    /// UUID is what distinguishes "someone else is called this" from "that is
    /// me".
    func machinesOtherThan(uuid: String) -> [DiscoveredMachine] {
        machines.filter { $0.uuid != uuid }
    }

    /// Local host machines (running on this Mac, excluding parasites)
    var localMachines: [DiscoveredMachine] {
        machines.filter { $0.isLocal && $0.role == "host" }
    }

    /// Remote host machines (running on other hosts, excluding parasites)
    var remoteMachines: [DiscoveredMachine] {
        machines.filter { !$0.isLocal && $0.role == "host" }
    }

    private var browser: NWBrowser?
    private var resolvers: [String: NetService] = [:]
    private var delegateWrappers: [String: NetServiceDelegateWrapper] = [:]  // Must retain delegates
    private var pendingResults: [String: NWBrowser.Result] = [:]
    private let localHostname: String

    init() {
        // Get local hostname for comparison
        localHostname = ProcessInfo.processInfo.hostName.lowercased()
    }

    /// Start discovering machines
    func startDiscovery() {
        guard browser == nil else { return }

        let descriptor = NWBrowser.Descriptor.bonjour(type: "_beebium._tcp", domain: "local.")
        let parameters = NWParameters()
        parameters.includePeerToPeer = true

        browser = NWBrowser(for: descriptor, using: parameters)

        browser?.stateUpdateHandler = { [weak self] state in
            Task { @MainActor [weak self] in
                self?.handleBrowserState(state)
            }
        }

        browser?.browseResultsChangedHandler = { [weak self] results, changes in
            Task { @MainActor [weak self] in
                self?.handleBrowseResults(results, changes: changes)
            }
        }

        browser?.start(queue: .main)
    }

    /// Stop discovering machines
    func stopDiscovery() {
        browser?.cancel()
        browser = nil

        // Cancel all pending resolutions
        for (_, resolver) in resolvers {
            resolver.stop()
        }
        resolvers.removeAll()
        delegateWrappers.removeAll()
        pendingResults.removeAll()
    }

    // MARK: - Private

    private func handleBrowserState(_ state: NWBrowser.State) {
        switch state {
        case .ready:
            isDiscoveryAvailable = true
        case .failed(let error):
            NSLog("[Discovery] Browser failed: \(error)")
            isDiscoveryAvailable = false
        case .cancelled:
            isDiscoveryAvailable = false
        default:
            break
        }
    }

    private func handleBrowseResults(_ results: Set<NWBrowser.Result>, changes: Set<NWBrowser.Result.Change>) {
        for change in changes {
            switch change {
            case .added(let result):
                handleServiceAdded(result)
            case .removed(let result):
                handleServiceRemoved(result)
            case .changed(old: _, new: let newResult, flags: _):
                // Service updated - re-resolve
                handleServiceRemoved(newResult)
                handleServiceAdded(newResult)
            case .identical:
                break
            @unknown default:
                break
            }
        }
    }

    private func handleServiceAdded(_ result: NWBrowser.Result) {
        guard case .service(let name, let type, let domain, _) = result.endpoint else {
            return
        }

        let serviceKey = "\(name).\(type)\(domain)"
        pendingResults[serviceKey] = result

        // Use NetService to resolve - it gives us host, port, and TXT record
        let resolver = NetService(domain: domain, type: type, name: name)
        let wrapper = NetServiceDelegateWrapper(client: self, serviceKey: serviceKey)
        delegateWrappers[serviceKey] = wrapper  // Retain the delegate
        resolver.delegate = wrapper
        resolvers[serviceKey] = resolver
        resolver.resolve(withTimeout: 5.0)
    }

    private func handleServiceRemoved(_ result: NWBrowser.Result) {
        guard case .service(let name, let type, let domain, _) = result.endpoint else {
            return
        }

        let serviceKey = "\(name).\(type)\(domain)"

        // Stop any pending resolution
        if let resolver = resolvers.removeValue(forKey: serviceKey) {
            resolver.stop()
        }
        delegateWrappers.removeValue(forKey: serviceKey)
        pendingResults.removeValue(forKey: serviceKey)

        // Remove from machines list
        machines.removeAll { $0.id == serviceKey }
    }

    fileprivate func handleServiceResolved(_ service: NetService, serviceKey: String) {
        guard let hostName = service.hostName, service.port > 0 else {
            NSLog("[Discovery] Service \(service.name) resolved without host/port")
            return
        }

        // Parse TXT record
        let txtData = service.txtRecordData() ?? Data()
        let txtDict = parseTXTRecord(txtData)

        // Determine if local by comparing hostnames
        let normalizedHost = hostName.lowercased()
        let isLocal = normalizedHost.hasPrefix(localHostname) ||
                      normalizedHost == "localhost." ||
                      normalizedHost == "localhost" ||
                      hostName == "127.0.0.1"

        let machine = DiscoveredMachine(
            id: serviceKey,
            instanceName: service.name,
            modelType: txtDict["model"] ?? "Unknown Model",
            host: hostName,
            port: service.port,
            isLocal: isLocal,
            uuid: txtDict["uuid"] ?? "",
            role: txtDict["role"] ?? "host",
            econetStation: txtDict["econet_station"].flatMap(Int.init),
            econetNet: txtDict["econet_net"].flatMap(Int.init),
            econetAunPort: txtDict["econet_aun_port"].flatMap(Int.init)
        )

        // Update or add to machines list
        if let existingIndex = machines.firstIndex(where: { $0.id == serviceKey }) {
            machines[existingIndex] = machine
        } else {
            machines.append(machine)
        }

        // Sort: local first, then by name
        machines.sort { lhs, rhs in
            if lhs.isLocal != rhs.isLocal {
                return lhs.isLocal
            }
            return lhs.instanceName.localizedCaseInsensitiveCompare(rhs.instanceName) == .orderedAscending
        }

        // Cleanup
        resolvers.removeValue(forKey: serviceKey)
        delegateWrappers.removeValue(forKey: serviceKey)
        pendingResults.removeValue(forKey: serviceKey)
    }

    fileprivate func handleServiceResolutionFailed(_ service: NetService, serviceKey: String, error: [String: NSNumber]) {
        NSLog("[Discovery] Failed to resolve \(service.name): \(error)")
        resolvers.removeValue(forKey: serviceKey)
        delegateWrappers.removeValue(forKey: serviceKey)
        pendingResults.removeValue(forKey: serviceKey)
    }

    private func parseTXTRecord(_ data: Data) -> [String: String] {
        guard !data.isEmpty else { return [:] }

        var result: [String: String] = [:]
        let dict = NetService.dictionary(fromTXTRecord: data)

        for (key, value) in dict {
            if let stringValue = String(data: value, encoding: .utf8) {
                result[key] = stringValue
            }
        }

        return result
    }
}

// MARK: - NetService Delegate Wrapper

/// Wrapper class to handle NetService delegate callbacks
private class NetServiceDelegateWrapper: NSObject, NetServiceDelegate {
    private weak var client: DiscoveryClient?
    private let serviceKey: String

    init(client: DiscoveryClient, serviceKey: String) {
        self.client = client
        self.serviceKey = serviceKey
    }

    func netServiceDidResolveAddress(_ sender: NetService) {
        Task { @MainActor [weak self] in
            guard let self = self, let client = self.client else { return }
            client.handleServiceResolved(sender, serviceKey: self.serviceKey)
        }
    }

    func netService(_ sender: NetService, didNotResolve errorDict: [String: NSNumber]) {
        Task { @MainActor [weak self] in
            guard let self = self, let client = self.client else { return }
            client.handleServiceResolutionFailed(sender, serviceKey: self.serviceKey, error: errorDict)
        }
    }
}
