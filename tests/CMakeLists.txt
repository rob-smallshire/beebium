# Beebium tests

# Test assets directory
set(BEEBIUM_TEST_ASSETS_DIR "${CMAKE_CURRENT_SOURCE_DIR}/assets")

# 6502 tests
add_executable(test_6502 test_6502.cpp)
target_link_libraries(test_6502 PRIVATE 6502_lib Catch2::Catch2WithMain)
target_compile_definitions(test_6502 PRIVATE
    BEEBIUM_TEST_ASSETS_DIR="${BEEBIUM_TEST_ASSETS_DIR}"
)
catch_discover_tests(test_6502)

# 6502/65C02 dead cycle bus behaviour tests (no Beebium core dependency)
add_executable(test_6502_dead_cycles test_6502_dead_cycles.cpp)
target_link_libraries(test_6502_dead_cycles PRIVATE 6502_lib Catch2::Catch2WithMain)
catch_discover_tests(test_6502_dead_cycles)

# 6502 NMI edge detection with concurrent sources (no Beebium core dependency)
add_executable(test_6502_nmi_concurrent_sources test_6502_nmi_concurrent_sources.cpp)
target_link_libraries(test_6502_nmi_concurrent_sources PRIVATE 6502_lib Catch2::Catch2WithMain)
catch_discover_tests(test_6502_nmi_concurrent_sources)

# Memory tests
add_executable(test_memory test_memory.cpp)
target_link_libraries(test_memory PRIVATE beebium_core Catch2::Catch2WithMain)
catch_discover_tests(test_memory)

# Configurable slot tests
add_executable(test_configurable_slot test_configurable_slot.cpp)
target_link_libraries(test_configurable_slot PRIVATE beebium_core Catch2::Catch2WithMain)
catch_discover_tests(test_configurable_slot)

# Aliased banked memory tests (Model B 4-socket ROM configuration)
add_executable(test_aliased_banked_memory test_aliased_banked_memory.cpp)
target_link_libraries(test_aliased_banked_memory PRIVATE beebium_core Catch2::Catch2WithMain)
catch_discover_tests(test_aliased_banked_memory)

# Configurable banked memory tests (ROM/RAM board 16-slot configuration)
add_executable(test_configurable_banked_memory test_configurable_banked_memory.cpp)
target_link_libraries(test_configurable_banked_memory PRIVATE beebium_core Catch2::Catch2WithMain)
catch_discover_tests(test_configurable_banked_memory)

# Extension identity tests (id, label, qualified provider lookup)
add_executable(test_extension_identity test_extension_identity.cpp)
target_link_libraries(test_extension_identity PRIVATE beebium_ext_test_scratch_ram beebium_core Catch2::Catch2WithMain)
catch_discover_tests(test_extension_identity)

# Extension argument parser tests
add_executable(test_extension_arg_parser test_extension_arg_parser.cpp)
target_link_libraries(test_extension_arg_parser PRIVATE beebium_core Catch2::Catch2WithMain)
catch_discover_tests(test_extension_arg_parser)

# Serial port enumeration tests (POSIX-only; Windows enumerator uses
# QueryDosDeviceW which has no injection seam -- covered by the manual
# Slioch verification described in the plan)
if(NOT WIN32)
    add_executable(test_enumerate_ports test_enumerate_ports.cpp)
    target_link_libraries(test_enumerate_ports PRIVATE beebium_core Catch2::Catch2WithMain)
    catch_discover_tests(test_enumerate_ports)
endif()

# Extension common base class tests
add_executable(test_extension_base test_extension_base.cpp)
target_link_libraries(test_extension_base PRIVATE beebium_core Catch2::Catch2WithMain)
catch_discover_tests(test_extension_base)

# ExtensionResolver: merging multi-source manifest collections
add_executable(test_extension_resolver test_extension_resolver.cpp)
target_link_libraries(test_extension_resolver PRIVATE beebium_core Catch2::Catch2WithMain)
catch_discover_tests(test_extension_resolver)

add_executable(test_attachment_points test_attachment_points.cpp)
target_link_libraries(test_attachment_points PRIVATE beebium_core Catch2::Catch2WithMain)
catch_discover_tests(test_attachment_points)

# Inline CLI argument parsers from ServerMain.hpp (case tolerance, error messages)
add_executable(test_cli_arg_parsers test_cli_arg_parsers.cpp)
target_include_directories(test_cli_arg_parsers PRIVATE
    ${CMAKE_SOURCE_DIR}/src/server/include
)
target_compile_definitions(test_cli_arg_parsers PRIVATE
    BEEBIUM_VERSION="${PROJECT_VERSION}"
)
target_link_libraries(test_cli_arg_parsers PRIVATE
    beebium_core
    beebium_service
    Catch2::Catch2WithMain
)
catch_discover_tests(test_cli_arg_parsers)

# Sideways slot topology + --sideways validation against each machine variant.
add_executable(test_sideways_validation test_sideways_validation.cpp)
target_include_directories(test_sideways_validation PRIVATE
    ${CMAKE_SOURCE_DIR}/src/server/include
)
target_link_libraries(test_sideways_validation PRIVATE
    beebium_core
    Catch2::Catch2WithMain
)
catch_discover_tests(test_sideways_validation)

# Sideways ROM header parsing
add_executable(test_sideways_rom_header test_sideways_rom_header.cpp)
target_link_libraries(test_sideways_rom_header PRIVATE
    beebium_core
    Catch2::Catch2WithMain
)
target_compile_definitions(test_sideways_rom_header PRIVATE
    BEEBIUM_ROM_DIR="${BEEBIUM_ROM_DIR}"
    BEEBIUM_TEST_ASSETS_DIR="${BEEBIUM_TEST_ASSETS_DIR}"
)
catch_discover_tests(test_sideways_rom_header)

# ROMFS detection (CFS block parsing + corpus validation)
add_executable(test_romfs_detection test_romfs_detection.cpp)
target_link_libraries(test_romfs_detection PRIVATE
    beebium_core
    Catch2::Catch2WithMain
)
target_compile_definitions(test_romfs_detection PRIVATE
    BEEBIUM_ROM_DIR="${BEEBIUM_ROM_DIR}"
    BEEBIUM_TEST_ASSETS_DIR="${BEEBIUM_TEST_ASSETS_DIR}"
)
catch_discover_tests(test_romfs_detection)

# EconetTransportRegistry container tests
add_executable(test_econet_transport_registry test_econet_transport_registry.cpp)
target_link_libraries(test_econet_transport_registry PRIVATE beebium_core Catch2::Catch2WithMain)
catch_discover_tests(test_econet_transport_registry)

# AunEconetTransportExtension tests
add_executable(test_aun_econet_transport_extension test_aun_econet_transport_extension.cpp)
target_link_libraries(test_aun_econet_transport_extension PRIVATE beebium_ext_aun Catch2::Catch2WithMain)
catch_discover_tests(test_aun_econet_transport_extension)

# AunDiscoveryAnnouncer tests (uses an injected fake Advertiser)
add_executable(test_aun_discovery_announcer test_aun_discovery_announcer.cpp)
target_link_libraries(test_aun_discovery_announcer PRIVATE beebium_ext_aun Catch2::Catch2WithMain)
catch_discover_tests(test_aun_discovery_announcer)

# AunDiscoverySubscriber tests (driven via inject_* helpers, no real browser)
add_executable(test_aun_discovery_subscriber test_aun_discovery_subscriber.cpp)
target_link_libraries(test_aun_discovery_subscriber PRIVATE beebium_ext_aun Catch2::Catch2WithMain)
catch_discover_tests(test_aun_discovery_subscriber)

# End-to-end mDNS test: paired announcers / subscribers with real
# platform mDNS. Tagged [.mdns] so it's skipped by default in
# environments without a working responder.
add_executable(test_aun_mdns_e2e test_aun_mdns_e2e.cpp)
target_link_libraries(test_aun_mdns_e2e PRIVATE beebium_ext_aun Catch2::Catch2WithMain)
catch_discover_tests(test_aun_mdns_e2e)

# Browser smoke tests (state, lifecycle, real-mDNS round-trip on macOS)
add_executable(test_browser test_browser.cpp)
target_link_libraries(test_browser PRIVATE beebium_discovery Catch2::Catch2WithMain)
# --allow-running-no-tests: the [browser][integration] cases SKIP where there is
# no functional mDNS responder (e.g. the GitHub Windows Server runner, whose
# native DnsService* stack is absent -- DnsServiceRegister returns error 14).
# Without this flag a skipped Catch2 case exits non-zero ("no tests ran") and
# ctest reports it as Failed rather than Skipped. test_advertiser carries the
# same flag for the same reason.
catch_discover_tests(test_browser EXTRA_ARGS --allow-running-no-tests)

# SCSI ADFS protocol compatibility tests (fast, no boot required)
add_executable(test_scsi_adfs_protocol test_scsi_adfs_protocol.cpp)
target_link_libraries(test_scsi_adfs_protocol PRIVATE beebium_scsi_core Catch2::Catch2WithMain)
catch_discover_tests(test_scsi_adfs_protocol)

# Hard disk image (DAT+DSC) tests
add_executable(test_hard_disk_image test_hard_disk_image.cpp)
target_link_libraries(test_hard_disk_image PRIVATE beebium_scsi_core Catch2::Catch2WithMain)
catch_discover_tests(test_hard_disk_image)

# SCSI hard disc target tests
add_executable(test_scsi_hard_disc test_scsi_hard_disc.cpp)
target_link_libraries(test_scsi_hard_disc PRIVATE beebium_scsi_core Catch2::Catch2WithMain)
catch_discover_tests(test_scsi_hard_disc)

# Tests for the ScsiHardDiscExtension wrapper (label composition,
# config handling) -- separate from test_scsi_hard_disc which targets
# the SCSI device itself.
add_executable(test_scsi_hard_disc_extension test_scsi_hard_disc_extension.cpp)
target_link_libraries(test_scsi_hard_disc_extension PRIVATE
    beebium_ext_scsi_hard_disc
    Catch2::Catch2WithMain
)
catch_discover_tests(test_scsi_hard_disc_extension)

# Tests for ScsiHardDiscUi (build_view shape, capacity formatter).
add_executable(test_scsi_hard_disc_ui test_scsi_hard_disc_ui.cpp)
target_link_libraries(test_scsi_hard_disc_ui PRIVATE
    beebium_ext_scsi_hard_disc
    beebium_extension_ui_proto
    Catch2::Catch2WithMain
)
catch_discover_tests(test_scsi_hard_disc_ui)

# SCSI hard disc tests with real ADFS image
add_executable(test_scsi_hard_disc_adfs test_scsi_hard_disc_adfs.cpp)
target_link_libraries(test_scsi_hard_disc_adfs PRIVATE beebium_scsi_core Catch2::Catch2WithMain)
target_compile_definitions(test_scsi_hard_disc_adfs PRIVATE
    BEEBIUM_TEST_ASSETS_DIR="${BEEBIUM_TEST_ASSETS_DIR}"
)
catch_discover_tests(test_scsi_hard_disc_adfs)

# SCSI ADFS full boot end-to-end test
add_executable(test_scsi_adfs_boot test_scsi_adfs_boot.cpp)
target_link_libraries(test_scsi_adfs_boot PRIVATE
    beebium_ext_scsi_hard_disc
    Catch2::Catch2WithMain
)
target_compile_definitions(test_scsi_adfs_boot PRIVATE
    BEEBIUM_ROM_DIR="${BEEBIUM_ROM_DIR}"
    BEEBIUM_TEST_ASSETS_DIR="${BEEBIUM_TEST_ASSETS_DIR}"
)
catch_discover_tests(test_scsi_adfs_boot)

# SCSI test device tests
add_executable(test_scsi_test_device test_scsi_test_device.cpp)
target_link_libraries(test_scsi_test_device PRIVATE beebium_scsi_core Catch2::Catch2WithMain)
catch_discover_tests(test_scsi_test_device)

# SCSI bus protocol tests
add_executable(test_scsi_bus test_scsi_bus.cpp)
target_link_libraries(test_scsi_bus PRIVATE beebium_scsi_core Catch2::Catch2WithMain)
catch_discover_tests(test_scsi_bus)

# SCSI target registry tests
add_executable(test_scsi_target_registry test_scsi_target_registry.cpp)
target_link_libraries(test_scsi_target_registry PRIVATE beebium_scsi_core Catch2::Catch2WithMain)
catch_discover_tests(test_scsi_target_registry)

# Acorn SCSI host adapter integration tests (through hardware memory map)
add_executable(test_acorn_scsi_integration test_acorn_scsi_integration.cpp)
target_link_libraries(test_acorn_scsi_integration PRIVATE beebium_ext_acorn_scsi Catch2::Catch2WithMain)
catch_discover_tests(test_acorn_scsi_integration)

# 1 MHz bus port tests
add_executable(test_one_mhz_bus_port test_one_mhz_bus_port.cpp)
target_link_libraries(test_one_mhz_bus_port PRIVATE beebium_core Catch2::Catch2WithMain)
catch_discover_tests(test_one_mhz_bus_port)

# Extension registry tests
add_executable(test_extension_registry test_extension_registry.cpp)
target_link_libraries(test_extension_registry PRIVATE beebium_ext_test_scratch_ram beebium_core Catch2::Catch2WithMain)
catch_discover_tests(test_extension_registry)

# TestScratchRam unit tests
add_executable(test_scratch_ram test_scratch_ram.cpp)
target_link_libraries(test_scratch_ram PRIVATE beebium_ext_test_scratch_ram beebium_core Catch2::Catch2WithMain)
catch_discover_tests(test_scratch_ram)

# TestScratchRam integration tests (through full hardware memory map)
add_executable(test_scratch_ram_integration test_scratch_ram_integration.cpp)
target_link_libraries(test_scratch_ram_integration PRIVATE beebium_ext_test_scratch_ram beebium_core Catch2::Catch2WithMain)
catch_discover_tests(test_scratch_ram_integration)

# Model B ROM/RAM Board hardware tests
add_executable(test_model_b_romram_hardware test_model_b_romram_hardware.cpp)
target_link_libraries(test_model_b_romram_hardware PRIVATE beebium_core Catch2::Catch2WithMain)
catch_discover_tests(test_model_b_romram_hardware)

# IBM disc format tests (FM/MFM encoding, CRC)
add_executable(test_ibm_disc_format test_ibm_disc_format.cpp)
target_link_libraries(test_ibm_disc_format PRIVATE beebium_core Catch2::Catch2WithMain)
catch_discover_tests(test_ibm_disc_format)

# Disc track (pulse storage) tests
add_executable(test_disc_track test_disc_track.cpp)
target_link_libraries(test_disc_track PRIVATE beebium_core Catch2::Catch2WithMain)
catch_discover_tests(test_disc_track)

# Track builder tests
add_executable(test_track_builder test_track_builder.cpp)
target_link_libraries(test_track_builder PRIVATE beebium_core Catch2::Catch2WithMain)
catch_discover_tests(test_track_builder)

# Track decoder tests
add_executable(test_track_decoder test_track_decoder.cpp)
target_link_libraries(test_track_decoder PRIVATE beebium_core Catch2::Catch2WithMain)
catch_discover_tests(test_track_decoder)

# SSD/DSD format handler tests
add_executable(test_ssd_format_handler test_ssd_format_handler.cpp)
target_link_libraries(test_ssd_format_handler PRIVATE beebium_core Catch2::Catch2WithMain)
catch_discover_tests(test_ssd_format_handler)

# ADFS format handler tests
add_executable(test_adfs_format_handler test_adfs_format_handler.cpp)
target_link_libraries(test_adfs_format_handler PRIVATE beebium_core Catch2::Catch2WithMain)
catch_discover_tests(test_adfs_format_handler)

# HFE format handler tests
add_executable(test_hfe_format_handler test_hfe_format_handler.cpp)
target_link_libraries(test_hfe_format_handler PRIVATE beebium_core Catch2::Catch2WithMain)
catch_discover_tests(test_hfe_format_handler)

# Disc loading tests (real images and malformed images)
add_executable(test_disc_loading test_disc_loading.cpp)
target_link_libraries(test_disc_loading PRIVATE beebium_core Catch2::Catch2WithMain)
target_compile_definitions(test_disc_loading PRIVATE
    BEEBIUM_TEST_ASSETS_DIR="${BEEBIUM_TEST_ASSETS_DIR}"
)
catch_discover_tests(test_disc_loading)

# Pulse disc drive tests
add_executable(test_disc_drive test_disc_drive.cpp)
target_link_libraries(test_disc_drive PRIVATE beebium_core Catch2::Catch2WithMain)
catch_discover_tests(test_disc_drive)

# Pulse WD1770 tests
add_executable(test_wd1770 test_wd1770.cpp)
target_link_libraries(test_wd1770 PRIVATE beebium_core Catch2::Catch2WithMain)
catch_discover_tests(test_wd1770)

# MC6854 ADLC tests
add_executable(test_mc6854 test_mc6854.cpp)
target_link_libraries(test_mc6854 PRIVATE beebium_core Catch2::Catch2WithMain)
catch_discover_tests(test_mc6854)

# MC6850 ACIA (serial) tests
add_executable(test_mc6850 test_mc6850.cpp)
target_link_libraries(test_mc6850 PRIVATE beebium_core Catch2::Catch2WithMain)
catch_discover_tests(test_mc6850)

# Serial ULA (SERPROC) tests
add_executable(test_serial_ula test_serial_ula.cpp)
target_link_libraries(test_serial_ula PRIVATE beebium_core Catch2::Catch2WithMain)
catch_discover_tests(test_serial_ula)

# Serial socket + Model B memory-map integration tests
add_executable(test_serial_socket test_serial_socket.cpp)
target_link_libraries(test_serial_socket PRIVATE beebium_core Catch2::Catch2WithMain)
catch_discover_tests(test_serial_socket)

# Shared endpoint-URL parser ([scheme://]host:port)
add_executable(test_endpoint_url test_endpoint_url.cpp)
target_link_libraries(test_endpoint_url PRIVATE beebium_core Catch2::Catch2WithMain)
catch_discover_tests(test_endpoint_url)

# Shared TCP transport (TcpClientSerialPort) over an in-process loopback server
add_executable(test_tcp_client_serial_port test_tcp_client_serial_port.cpp)
target_link_libraries(test_tcp_client_serial_port
    PRIVATE beebium_core Threads::Threads Catch2::Catch2WithMain)
catch_discover_tests(test_tcp_client_serial_port)

# TCP server transport (TcpServerSerialPort: listen/accept-one)
add_executable(test_tcp_server_serial_port test_tcp_server_serial_port.cpp)
target_link_libraries(test_tcp_server_serial_port
    PRIVATE beebium_core Threads::Threads Catch2::Catch2WithMain)
catch_discover_tests(test_tcp_server_serial_port)

# IP232 escape codec (pure, golden-vector unit tests)
add_executable(test_ip232_codec test_ip232_codec.cpp)
target_include_directories(test_ip232_codec
    PRIVATE ${CMAKE_SOURCE_DIR}/src/extensions/ip232-serial)
target_link_libraries(test_ip232_codec PRIVATE beebium_core Catch2::Catch2WithMain)
catch_discover_tests(test_ip232_codec)

# RFC 2217 codec (pure, golden-vector unit tests)
add_executable(test_rfc2217_codec test_rfc2217_codec.cpp)
target_link_libraries(test_rfc2217_codec
    PRIVATE beebium_ext_rfc2217_common Catch2::Catch2WithMain)
catch_discover_tests(test_rfc2217_codec)

# RFC 2217 client endpoint (in-process echo-server fixture) + extension
add_executable(test_rfc2217_client_endpoint test_rfc2217_client_endpoint.cpp)
target_include_directories(test_rfc2217_client_endpoint
    PRIVATE ${CMAKE_SOURCE_DIR}/src/extensions/rfc2217-client-serial
            ${CMAKE_SOURCE_DIR}/src/extensions/rfc2217-common)
target_link_libraries(test_rfc2217_client_endpoint
    PRIVATE beebium_ext_rfc2217_client beebium_core Threads::Threads
            Catch2::Catch2WithMain)
catch_discover_tests(test_rfc2217_client_endpoint)

add_executable(test_rfc2217_client_extension test_rfc2217_client_extension.cpp)
target_include_directories(test_rfc2217_client_extension
    PRIVATE ${CMAKE_SOURCE_DIR}/src/extensions/rfc2217-client-serial)
target_link_libraries(test_rfc2217_client_extension
    PRIVATE beebium_ext_rfc2217_client beebium_core Threads::Threads
            Catch2::Catch2WithMain)
catch_discover_tests(test_rfc2217_client_extension)

# RFC 2217 server endpoint (real client<->server over TCP) + extension
add_executable(test_rfc2217_server_endpoint test_rfc2217_server_endpoint.cpp)
target_include_directories(test_rfc2217_server_endpoint
    PRIVATE ${CMAKE_SOURCE_DIR}/src/extensions/rfc2217-client-serial
            ${CMAKE_SOURCE_DIR}/src/extensions/rfc2217-server-serial)
target_link_libraries(test_rfc2217_server_endpoint
    PRIVATE beebium_ext_rfc2217_client beebium_ext_rfc2217_server beebium_core
            Threads::Threads Catch2::Catch2WithMain)
catch_discover_tests(test_rfc2217_server_endpoint)

add_executable(test_rfc2217_server_extension test_rfc2217_server_extension.cpp)
target_include_directories(test_rfc2217_server_extension
    PRIVATE ${CMAKE_SOURCE_DIR}/src/extensions/rfc2217-server-serial)
target_link_libraries(test_rfc2217_server_extension
    PRIVATE beebium_ext_rfc2217_server beebium_core Threads::Threads
            Catch2::Catch2WithMain)
catch_discover_tests(test_rfc2217_server_extension)

# RFC 2217 interop against a real pySerial client (opt-in: skips when absent)
add_executable(test_rfc2217_pyserial_client test_rfc2217_pyserial_client.cpp)
target_include_directories(test_rfc2217_pyserial_client
    PRIVATE ${CMAKE_SOURCE_DIR}/src/extensions/rfc2217-server-serial)
target_link_libraries(test_rfc2217_pyserial_client
    PRIVATE beebium_ext_rfc2217_server beebium_core Threads::Threads
            Catch2::Catch2WithMain)
catch_discover_tests(test_rfc2217_pyserial_client
    PROPERTIES SKIP_RETURN_CODE 4
)

# IP232 endpoint integration tests (against the in-process loopback server)
add_executable(test_ip232_serial_endpoint test_ip232_serial_endpoint.cpp)
target_link_libraries(test_ip232_serial_endpoint
    PRIVATE beebium_ext_ip232_serial beebium_core Threads::Threads
            Catch2::Catch2WithMain)
catch_discover_tests(test_ip232_serial_endpoint)

# IP232 extension (attach / config parsing / shutdown)
add_executable(test_ip232_serial_extension test_ip232_serial_extension.cpp)
target_include_directories(test_ip232_serial_extension
    PRIVATE ${CMAKE_SOURCE_DIR}/src/extensions/ip232-serial)
target_link_libraries(test_ip232_serial_extension
    PRIVATE beebium_ext_ip232_serial beebium_core Threads::Threads
            Catch2::Catch2WithMain)
catch_discover_tests(test_ip232_serial_extension)

# IP232 interop against a real tcpser (opt-in: skips cleanly when absent)
add_executable(test_ip232_tcpser_integration test_ip232_tcpser_integration.cpp)
target_include_directories(test_ip232_tcpser_integration
    PRIVATE ${CMAKE_SOURCE_DIR}/src/extensions/ip232-serial)
target_link_libraries(test_ip232_tcpser_integration
    PRIVATE beebium_ext_ip232_serial beebium_core Threads::Threads
            Catch2::Catch2WithMain)
# tcpser is optional: Catch2 returns 4 for a skipped run, which ctest then
# reports as skipped rather than failed.
catch_discover_tests(test_ip232_tcpser_integration
    PROPERTIES SKIP_RETURN_CODE 4
)

# Serial PTY transport integration tests (POSIX-only: PtyMaster +
# HostSerialEndpoint bridged through the SerialSocket to a PosixSerialPort).
if(NOT WIN32)
    add_executable(test_serial_pty test_serial_pty.cpp)
    target_include_directories(test_serial_pty PRIVATE
        ${CMAKE_SOURCE_DIR}/src/extensions/host-serial
    )
    target_link_libraries(test_serial_pty PRIVATE
        beebium_ext_host_serial Catch2::Catch2WithMain)
    catch_discover_tests(test_serial_pty)
endif()

# Econet socket tests
add_executable(test_econet_socket test_econet_socket.cpp)
target_link_libraries(test_econet_socket PRIVATE beebium_core Catch2::Catch2WithMain)
catch_discover_tests(test_econet_socket)

# NetworkBackend station-change hook tests
add_executable(test_network_backend_station_change test_network_backend_station_change.cpp)
target_link_libraries(test_network_backend_station_change PRIVATE beebium_core Catch2::Catch2WithMain)
catch_discover_tests(test_network_backend_station_change)

# Piconet tests. Most of these are platform-agnostic (pure protocol
# parsing, in-process FakePiconetDevice, mock-serial unit tests) and
# build on all platforms. A small number are platform-specific:
# test_posix_serial_port and test_piconet_pty_integration exercise
# PTY-backed POSIX plumbing; test_win32_serial_port and the
# FakePiconetDeviceOnPipe path exercise Win32 named-pipe plumbing.
# beebium_piconet_core itself now builds on every platform with the
# appropriate SerialPort implementation compiled in.

# Piconet protocol library: TX_RESULT enum and parser
add_executable(test_piconet_tx_result test_piconet_tx_result.cpp)
target_link_libraries(test_piconet_tx_result PRIVATE beebium_piconet_core Catch2::Catch2WithMain)
catch_discover_tests(test_piconet_tx_result)

# Piconet protocol library: base64 encoder/decoder
add_executable(test_piconet_base64 test_piconet_base64.cpp)
target_link_libraries(test_piconet_base64 PRIVATE beebium_piconet_core Catch2::Catch2WithMain)
catch_discover_tests(test_piconet_base64)

# Piconet protocol library: command formatters
add_executable(test_piconet_commands test_piconet_commands.cpp)
target_link_libraries(test_piconet_commands PRIVATE beebium_piconet_core Catch2::Catch2WithMain)
catch_discover_tests(test_piconet_commands)

# Piconet protocol library: event parser
add_executable(test_piconet_events test_piconet_events.cpp)
target_link_libraries(test_piconet_events PRIVATE beebium_piconet_core Catch2::Catch2WithMain)
catch_discover_tests(test_piconet_events)

# PiconetBackend TX path against MockPiconetSerial
add_executable(test_piconet_backend_tx test_piconet_backend_tx.cpp)
target_link_libraries(test_piconet_backend_tx PRIVATE beebium_piconet_core Catch2::Catch2WithMain)
target_include_directories(test_piconet_backend_tx PRIVATE ${CMAKE_CURRENT_SOURCE_DIR})
catch_discover_tests(test_piconet_backend_tx)

# PiconetBackend RX path (reader thread, line buffering, event dispatch)
add_executable(test_piconet_backend_rx test_piconet_backend_rx.cpp)
target_link_libraries(test_piconet_backend_rx PRIVATE beebium_piconet_core Catch2::Catch2WithMain)
target_include_directories(test_piconet_backend_rx PRIVATE ${CMAKE_CURRENT_SOURCE_DIR})
catch_discover_tests(test_piconet_backend_rx)

# PiconetBackend lifecycle (constructor handshake, station sync, destructor join)
add_executable(test_piconet_backend_lifecycle test_piconet_backend_lifecycle.cpp)
target_link_libraries(test_piconet_backend_lifecycle PRIVATE beebium_piconet_core Catch2::Catch2WithMain)
target_include_directories(test_piconet_backend_lifecycle PRIVATE ${CMAKE_CURRENT_SOURCE_DIR})
catch_discover_tests(test_piconet_backend_lifecycle)

# Piconet hardware contract tests (gated on BEEBIUM_PICONET_DEVICE env var).
# Skipped without a real Piconet device; when run, asserts that the
# firmware's behaviour matches piconet_protocol's expectations -- the
# fidelity anchor for FakePiconetDevice. SAFE WITHOUT A REAL ECONET WIRE.
add_executable(test_piconet_hardware test_piconet_hardware.cpp)
target_link_libraries(test_piconet_hardware PRIVATE beebium_piconet_core Catch2::Catch2WithMain)
# Every test in this binary calls SKIP() when BEEBIUM_PICONET_DEVICE
# is unset. Catch2 returns exit code 4 ("no tests ran") in that case,
# which ctest interprets as failure unless we pass through this flag.
catch_discover_tests(test_piconet_hardware EXTRA_ARGS --allow-running-no-tests)

# FakePiconetDevice unit tests (behavioural model of Piconet firmware).
add_executable(test_piconet_fake_device
    test_piconet_fake_device.cpp
    piconet/FakePiconetDevice.cpp)
target_link_libraries(test_piconet_fake_device PRIVATE beebium_piconet_core Catch2::Catch2WithMain)
target_include_directories(test_piconet_fake_device PRIVATE ${CMAKE_CURRENT_SOURCE_DIR})
catch_discover_tests(test_piconet_fake_device)

# Piconet integration: full FourWayHandshake -> PiconetBackend -> FakePiconetDevice stack.
add_executable(test_piconet_integration
    test_piconet_integration.cpp
    piconet/FakePiconetDevice.cpp)
target_link_libraries(test_piconet_integration PRIVATE beebium_piconet_core Catch2::Catch2WithMain)
target_include_directories(test_piconet_integration PRIVATE ${CMAKE_CURRENT_SOURCE_DIR})
catch_discover_tests(test_piconet_integration)

# Network-required Piconet hardware contract tests (gated on
# BEEBIUM_PICONET_DEVICE and BEEBIUM_PICONET_NETWORK_AVAILABLE).
# REQUIRES: Piconet attached to a real Econet wire with at least one peer.
add_executable(test_piconet_hardware_network test_piconet_hardware_network.cpp)
target_link_libraries(test_piconet_hardware_network PRIVATE beebium_piconet_core Catch2::Catch2WithMain)
# SKIP-on-missing-env-var pattern; see test_piconet_hardware above.
catch_discover_tests(test_piconet_hardware_network EXTRA_ARGS --allow-running-no-tests)

if(NOT WIN32)
    # POSIX serial port wrapper (production SerialPort implementation)
    add_executable(test_posix_serial_port test_posix_serial_port.cpp)
    target_link_libraries(test_posix_serial_port PRIVATE beebium_piconet_core Catch2::Catch2WithMain)
    catch_discover_tests(test_posix_serial_port)

    # PTY-bridged FakePiconetDevice integration tests (parity with the
    # in-process integration suite, exercising PosixSerialPort end-to-end).
    # Win32SerialPort has its own pipe-bridged coverage below.
    add_executable(test_piconet_pty_integration
        test_piconet_pty_integration.cpp
        piconet/FakePiconetDevice.cpp
        piconet/FakePiconetDeviceOnPty.cpp
        piconet/PtyPair.cpp)
    target_link_libraries(test_piconet_pty_integration PRIVATE beebium_piconet_core Catch2::Catch2WithMain)
    target_include_directories(test_piconet_pty_integration PRIVATE ${CMAKE_CURRENT_SOURCE_DIR})
    catch_discover_tests(test_piconet_pty_integration)

    # AUN-bridged Piconet test: two Beebium-style stacks peered on UDP
    # loopback, with no real hardware involved. POSIX-only because the
    # bridge implementation uses the same PTY plumbing as
    # FakePiconetDeviceOnPty.
    add_executable(test_piconet_aun_bridge
        test_piconet_aun_bridge.cpp
        piconet/AunBridgePiconetDevice.cpp)
    target_link_libraries(test_piconet_aun_bridge PRIVATE beebium_piconet_core Catch2::Catch2WithMain)
    target_include_directories(test_piconet_aun_bridge PRIVATE ${CMAKE_CURRENT_SOURCE_DIR})
    catch_discover_tests(test_piconet_aun_bridge)
endif()

# Win32 serial port wrapper (production SerialPort implementation on
# Windows). Exercises read/write/close semantics against a
# CreateNamedPipe-backed helper that stands in for a real COM port.
if(WIN32)
    add_executable(test_win32_serial_port
        test_win32_serial_port.cpp
        piconet/NamedPipePair.cpp)
    target_link_libraries(test_win32_serial_port PRIVATE beebium_piconet_core Catch2::Catch2WithMain)
    target_include_directories(test_win32_serial_port PRIVATE ${CMAKE_CURRENT_SOURCE_DIR})
    catch_discover_tests(test_win32_serial_port)
endif()

# Platform-specific serial-bridge sources for tests that use
# FakePiconetDeviceOnSerial. On POSIX the PTY plumbing is compiled in;
# on Windows the named-pipe plumbing is compiled in.
if(WIN32)
    set(PICONET_SERIAL_BRIDGE_SRCS
        piconet/FakePiconetDeviceOnPipe.cpp
        piconet/NamedPipePair.cpp)
else()
    set(PICONET_SERIAL_BRIDGE_SRCS
        piconet/FakePiconetDeviceOnPty.cpp
        piconet/PtyPair.cpp)
endif()

# Econet TX complete NMI tests
add_executable(test_econet_tx_complete_nmi test_econet_tx_complete_nmi.cpp)
target_link_libraries(test_econet_tx_complete_nmi PRIVATE beebium_core Catch2::Catch2WithMain)
catch_discover_tests(test_econet_tx_complete_nmi)

# Econet NMI delivery chain tests (full Machine + 6502)
add_executable(test_econet_nmi_delivery test_econet_nmi_delivery.cpp)
target_link_libraries(test_econet_nmi_delivery PRIVATE beebium_core Catch2::Catch2WithMain)
catch_discover_tests(test_econet_nmi_delivery)

# Four-way handshake (AUN mode) tests
add_executable(test_four_way_handshake test_four_way_handshake.cpp)
target_link_libraries(test_four_way_handshake PRIVATE beebium_core Catch2::Catch2WithMain)
catch_discover_tests(test_four_way_handshake)

# AUN packet encode/decode tests
add_executable(test_aun_packet test_aun_packet.cpp)
target_link_libraries(test_aun_packet PRIVATE beebium_core Catch2::Catch2WithMain)
catch_discover_tests(test_aun_packet)

# AUN backend UDP loopback tests
add_executable(test_aun_backend test_aun_backend.cpp)
target_link_libraries(test_aun_backend PRIVATE beebium_core Catch2::Catch2WithMain)
catch_discover_tests(test_aun_backend)

# Disc controller socket tests
add_executable(test_disc_controller_socket test_disc_controller_socket.cpp)
target_link_libraries(test_disc_controller_socket PRIVATE beebium_core Catch2::Catch2WithMain)
catch_discover_tests(test_disc_controller_socket)

# Acorn 1770 disc controller tests
add_executable(test_acorn_1770_controller test_acorn_1770_controller.cpp)
target_link_libraries(test_acorn_1770_controller PRIVATE beebium_core Catch2::Catch2WithMain)
catch_discover_tests(test_acorn_1770_controller)

# Expression engine tests
add_executable(test_expression test_expression.cpp)
target_link_libraries(test_expression PRIVATE beebium_core Catch2::Catch2WithMain)
catch_discover_tests(test_expression)

# Machine tests
add_executable(test_machine test_machine.cpp)
target_link_libraries(test_machine PRIVATE beebium_core Catch2::Catch2WithMain)
catch_discover_tests(test_machine)

# VIA 6522 tests
add_executable(test_via6522 test_via6522.cpp)
target_link_libraries(test_via6522 PRIVATE beebium_core Catch2::Catch2WithMain)
catch_discover_tests(test_via6522)

# User Port framework tests
add_executable(test_user_port test_user_port.cpp)
target_link_libraries(test_user_port PRIVATE beebium_core Catch2::Catch2WithMain)
catch_discover_tests(test_user_port)

# SAF3019P RTC chip emulation tests
add_executable(test_saf3019p test_saf3019p.cpp)
target_link_libraries(test_saf3019p PRIVATE beebium_rtc_core Catch2::Catch2WithMain)
catch_discover_tests(test_saf3019p)

# Time parser tests
add_executable(test_time_parser test_time_parser.cpp)
target_link_libraries(test_time_parser PRIVATE beebium_rtc_core Catch2::Catch2WithMain)
catch_discover_tests(test_time_parser)

# SAF3019P v1.26 protocol conformance tests
add_executable(test_saf3019p_v126 test_saf3019p_v126.cpp)
target_link_libraries(test_saf3019p_v126 PRIVATE beebium_rtc_core Catch2::Catch2WithMain)
catch_discover_tests(test_saf3019p_v126)

# AcornRtcUi tests (build_view shape, format helpers, worker lifecycle).
# Gated on BEEBIUM_BUILD_SERVICE because the full AcornRtcExtension
# (which the UI wraps) is only built when the service layer is in.
if(BEEBIUM_BUILD_SERVICE)
    add_executable(test_acorn_rtc_ui test_acorn_rtc_ui.cpp)
    target_link_libraries(test_acorn_rtc_ui PRIVATE
        beebium_ext_acorn_rtc
        beebium_extension_ui_proto
        Catch2::Catch2WithMain
    )
    catch_discover_tests(test_acorn_rtc_ui)
endif()

# SN76489 sound chip tests
add_executable(test_sn76489 test_sn76489.cpp)
target_link_libraries(test_sn76489 PRIVATE beebium_core Catch2::Catch2WithMain)
catch_discover_tests(test_sn76489)

# SN76489 MAME-inspired tests (hardware-verified behaviors)
add_executable(test_sn76489_mame test_sn76489_mame.cpp)
target_link_libraries(test_sn76489_mame PRIVATE beebium_core Catch2::Catch2WithMain)
catch_discover_tests(test_sn76489_mame)

# SN76489 DC bias metadata tests
add_executable(test_sn76489_dc_bias test_sn76489_dc_bias.cpp)
target_link_libraries(test_sn76489_dc_bias PRIVATE beebium_core Catch2::Catch2WithMain)
catch_discover_tests(test_sn76489_dc_bias)

# BBC BASIC SOUND command integration tests
add_executable(test_sound_basic test_sound_basic.cpp)
target_link_libraries(test_sound_basic PRIVATE beebium_core Catch2::Catch2WithMain)
target_compile_definitions(test_sound_basic PRIVATE
    BEEBIUM_ROM_DIR="${BEEBIUM_ROM_DIR}"
)
catch_discover_tests(test_sound_basic)

# Direct VIA sound integration tests (no ROM required)
add_executable(test_sound_via test_sound_via.cpp)
target_link_libraries(test_sound_via PRIVATE beebium_core Catch2::Catch2WithMain)
catch_discover_tests(test_sound_via)

# MOS sound system diagnostic test
add_executable(test_sound_mos_diagnostic test_sound_mos_diagnostic.cpp)
target_link_libraries(test_sound_mos_diagnostic PRIVATE beebium_core Catch2::Catch2WithMain)
target_compile_definitions(test_sound_mos_diagnostic PRIVATE
    BEEBIUM_ROM_DIR="${BEEBIUM_ROM_DIR}"
)
catch_discover_tests(test_sound_mos_diagnostic)

# Sound chip boot trace
add_executable(test_sound_boot_trace test_sound_boot_trace.cpp)
target_link_libraries(test_sound_boot_trace PRIVATE beebium_core Catch2::Catch2WithMain)
target_compile_definitions(test_sound_boot_trace PRIVATE
    BEEBIUM_ROM_DIR="${BEEBIUM_ROM_DIR}"
)
catch_discover_tests(test_sound_boot_trace)

# Sound edge case tests (inspired by beebjit)
add_executable(test_sound_edge_cases test_sound_edge_cases.cpp)
target_link_libraries(test_sound_edge_cases PRIVATE beebium_core Catch2::Catch2WithMain)
catch_discover_tests(test_sound_edge_cases)

# Sound state consistency tests
add_executable(test_sound_state test_sound_state.cpp)
target_link_libraries(test_sound_state PRIVATE beebium_core Catch2::Catch2WithMain)
catch_discover_tests(test_sound_state)

# Sound timing precision tests
add_executable(test_sound_timing test_sound_timing.cpp)
target_link_libraries(test_sound_timing PRIVATE beebium_core Catch2::Catch2WithMain)
catch_discover_tests(test_sound_timing)

# CRTC timing diagnostic
add_executable(test_crtc_timing test_crtc_timing.cpp)
target_link_libraries(test_crtc_timing PRIVATE beebium_core Catch2::Catch2WithMain)
target_compile_definitions(test_crtc_timing PRIVATE
    BEEBIUM_ROM_DIR="${BEEBIUM_ROM_DIR}"
)
catch_discover_tests(test_crtc_timing)

# VSYNC frequency measurement
add_executable(test_vsync_frequency test_vsync_frequency.cpp)
target_link_libraries(test_vsync_frequency PRIVATE beebium_core Catch2::Catch2WithMain)
target_compile_definitions(test_vsync_frequency PRIVATE
    BEEBIUM_ROM_DIR="${BEEBIUM_ROM_DIR}"
)
catch_discover_tests(test_vsync_frequency)

# VIA 6522 cycle-accurate timing tests (hardware-validated)
add_executable(test_via_timing test_via_timing.cpp)
target_link_libraries(test_via_timing PRIVATE beebium_core Catch2::Catch2WithMain)
catch_discover_tests(test_via_timing)

# Memory map tests
add_executable(test_memory_map test_memory_map.cpp)
target_link_libraries(test_memory_map PRIVATE beebium_core Catch2::Catch2WithMain)
catch_discover_tests(test_memory_map)

# Clock tests
add_executable(test_clock test_clock.cpp)
target_link_libraries(test_clock PRIVATE beebium_core Catch2::Catch2WithMain)
catch_discover_tests(test_clock)

# IRQ aggregator tests
add_executable(test_irq_aggregator test_irq_aggregator.cpp)
target_link_libraries(test_irq_aggregator PRIVATE beebium_core Catch2::Catch2WithMain)
catch_discover_tests(test_irq_aggregator)

# NMI aggregator tests
add_executable(test_nmi_aggregator test_nmi_aggregator.cpp)
target_link_libraries(test_nmi_aggregator PRIVATE beebium_core Catch2::Catch2WithMain)
catch_discover_tests(test_nmi_aggregator)

# Disc controller integration tests
add_executable(test_disc_integration test_disc_integration.cpp)
target_link_libraries(test_disc_integration PRIVATE beebium_core Catch2::Catch2WithMain)
catch_discover_tests(test_disc_integration)

# Shipping ROMs (tracked in roms/ at repo root).
set(BEEBIUM_ROM_DIR "${CMAKE_SOURCE_DIR}/roms" CACHE PATH "Directory containing BBC Micro ROMs")

# Extended test ROM directory with additional NFS/ANFS versions for Econet testing
# (tracked in tests/assets/roms/).
set(BEEBIUM_TEST_ROM_DIR "${CMAKE_CURRENT_SOURCE_DIR}/assets/roms")

# Disc images directory for integration tests
set(BEEBIUM_DISCS_DIR "${CMAKE_SOURCE_DIR}/discs" CACHE PATH "Directory containing test disc images")

# Add ROM and disc dirs to disc integration tests
target_compile_definitions(test_disc_integration PRIVATE
    BEEBIUM_ROM_DIR="${BEEBIUM_ROM_DIR}"
    BEEBIUM_DISCS_DIR="${BEEBIUM_DISCS_DIR}"
)

# Boot trace tests (uses ProgramCounterHistogram for instruction profiling)
add_executable(test_boot_trace test_boot_trace.cpp)
target_link_libraries(test_boot_trace PRIVATE beebium_core Catch2::Catch2WithMain)
target_compile_definitions(test_boot_trace PRIVATE
    BEEBIUM_ROM_DIR="${BEEBIUM_ROM_DIR}"
)
catch_discover_tests(test_boot_trace)

# Boot tests
add_executable(test_boot test_boot.cpp)
target_link_libraries(test_boot PRIVATE beebium_core Catch2::Catch2WithMain)
target_compile_definitions(test_boot PRIVATE
    BEEBIUM_ROM_DIR="${BEEBIUM_ROM_DIR}"
)
catch_discover_tests(test_boot)

# Boot sequence test (TDD-style boot progression)
add_executable(test_boot_sequence test_boot_sequence.cpp)
target_link_libraries(test_boot_sequence PRIVATE beebium_core Catch2::Catch2WithMain)
target_compile_definitions(test_boot_sequence PRIVATE
    BEEBIUM_ROM_DIR="${BEEBIUM_ROM_DIR}"
)
catch_discover_tests(test_boot_sequence)

# Econet boot integration tests
add_executable(test_boot_econet test_boot_econet.cpp)
target_link_libraries(test_boot_econet PRIVATE beebium_core Catch2::Catch2WithMain)
target_compile_definitions(test_boot_econet PRIVATE
    BEEBIUM_ROM_DIR="${BEEBIUM_ROM_DIR}"
    BEEBIUM_TEST_ROM_DIR="${BEEBIUM_TEST_ROM_DIR}"
)
catch_discover_tests(test_boot_econet)

# Econet TX with/without Tube integration test
add_executable(test_econet_tx_with_tube test_econet_tx_with_tube.cpp)
target_link_libraries(test_econet_tx_with_tube PRIVATE beebium_core Catch2::Catch2WithMain)
target_compile_definitions(test_econet_tx_with_tube PRIVATE
    BEEBIUM_ROM_DIR="${BEEBIUM_ROM_DIR}"
    BEEBIUM_TEST_ROM_DIR="${BEEBIUM_TEST_ROM_DIR}"
)
catch_discover_tests(test_econet_tx_with_tube)

# Econet file server integration tests (discretionary — need BEEBIUM_FILESERVER env var)
add_executable(test_econet_fileserver test_econet_fileserver.cpp)
target_link_libraries(test_econet_fileserver PRIVATE beebium_core Catch2::Catch2WithMain)
target_compile_definitions(test_econet_fileserver PRIVATE
    BEEBIUM_ROM_DIR="${BEEBIUM_ROM_DIR}"
    BEEBIUM_TEST_ROM_DIR="${BEEBIUM_TEST_ROM_DIR}"
)
catch_discover_tests(test_econet_fileserver
    PROPERTIES SKIP_RETURN_CODE 4
)

# clearRAM test
add_executable(test_clearram test_clearram.cpp)
target_link_libraries(test_clearram PRIVATE beebium_core Catch2::Catch2WithMain)
target_compile_definitions(test_clearram PRIVATE
    BEEBIUM_ROM_DIR="${BEEBIUM_ROM_DIR}"
)
catch_discover_tests(test_clearram)

# Reset behavior tests (soft reset, power-on reset, Break key)
add_executable(test_reset test_reset.cpp)
target_link_libraries(test_reset PRIVATE beebium_core Catch2::Catch2WithMain)
target_compile_definitions(test_reset PRIVATE
    BEEBIUM_ROM_DIR="${BEEBIUM_ROM_DIR}"
)
catch_discover_tests(test_reset)

# Video output tests
add_executable(test_video test_video.cpp)
target_link_libraries(test_video PRIVATE beebium_core Catch2::Catch2WithMain)
target_compile_definitions(test_video PRIVATE
    BEEBIUM_ROM_DIR="${BEEBIUM_ROM_DIR}"
)
catch_discover_tests(test_video)

# FrameRenderer display region tests
add_executable(test_frame_renderer_regions test_frame_renderer_regions.cpp)
target_link_libraries(test_frame_renderer_regions PRIVATE beebium_core Catch2::Catch2WithMain)
catch_discover_tests(test_frame_renderer_regions)

# SAA5050 teletext chip tests
add_executable(test_saa5050 test_saa5050.cpp)
target_link_libraries(test_saa5050 PRIVATE beebium_core Catch2::Catch2WithMain)
catch_discover_tests(test_saa5050)

# Addressable latch (IC32) tests
add_executable(test_addressable_latch test_addressable_latch.cpp)
target_link_libraries(test_addressable_latch PRIVATE beebium_core Catch2::Catch2WithMain)
catch_discover_tests(test_addressable_latch)

# CRTC 6845 tests
add_executable(test_crtc6845 test_crtc6845.cpp)
target_link_libraries(test_crtc6845 PRIVATE beebium_core Catch2::Catch2WithMain)
catch_discover_tests(test_crtc6845)

# Video ULA tests
add_executable(test_video_ula test_video_ula.cpp)
target_link_libraries(test_video_ula PRIVATE beebium_core Catch2::Catch2WithMain)
catch_discover_tests(test_video_ula)

# Cursor blink tests
add_executable(test_cursor test_cursor.cpp)
target_link_libraries(test_cursor PRIVATE beebium_core Catch2::Catch2WithMain)
target_compile_definitions(test_cursor PRIVATE
    BEEBIUM_ROM_DIR="${BEEBIUM_ROM_DIR}"
)
catch_discover_tests(test_cursor)

# Cursor blink timing tests (integration test for blink rate verification)
add_executable(test_cursor_blink_timing test_cursor_blink_timing.cpp)
target_link_libraries(test_cursor_blink_timing PRIVATE beebium_core Catch2::Catch2WithMain)
target_compile_definitions(test_cursor_blink_timing PRIVATE
    BEEBIUM_ROM_DIR="${BEEBIUM_ROM_DIR}"
)
catch_discover_tests(test_cursor_blink_timing)

# Keyboard input tests
add_executable(test_keyboard_input test_keyboard_input.cpp)
target_link_libraries(test_keyboard_input PRIVATE beebium_core Catch2::Catch2WithMain)
target_compile_definitions(test_keyboard_input PRIVATE
    BEEBIUM_ROM_DIR="${BEEBIUM_ROM_DIR}"
)
catch_discover_tests(test_keyboard_input)

# Keyboard thread safety tests
add_executable(test_keyboard_race test_keyboard_race.cpp)
target_link_libraries(test_keyboard_race PRIVATE beebium_core Catch2::Catch2WithMain)
catch_discover_tests(test_keyboard_race)

# Clipboard text translation tests
add_executable(test_text_translation test_text_translation.cpp)
target_link_libraries(test_text_translation PRIVATE beebium_core Catch2::Catch2WithMain)
catch_discover_tests(test_text_translation)

# MODE 7 teletext comprehensive tests
add_executable(test_mode7 test_mode7.cpp)
target_link_libraries(test_mode7 PRIVATE beebium_core Catch2::Catch2WithMain)
target_compile_definitions(test_mode7 PRIVATE
    BEEBIUM_ROM_DIR="${BEEBIUM_ROM_DIR}"
)
catch_discover_tests(test_mode7)

# Bitmap modes (0-6) diagnostic tests
add_executable(test_bitmap_modes test_bitmap_modes.cpp)
target_link_libraries(test_bitmap_modes PRIVATE beebium_core Catch2::Catch2WithMain)
target_compile_definitions(test_bitmap_modes PRIVATE
    BEEBIUM_ROM_DIR="${BEEBIUM_ROM_DIR}"
)
catch_discover_tests(test_bitmap_modes)

# Keyboard typing reliability tests
add_executable(test_keyboard_typing test_keyboard_typing.cpp)
target_link_libraries(test_keyboard_typing PRIVATE beebium_core Catch2::Catch2WithMain)
target_compile_definitions(test_keyboard_typing PRIVATE
    BEEBIUM_ROM_DIR="${BEEBIUM_ROM_DIR}"
)
catch_discover_tests(test_keyboard_typing)

# Keyboard pacing measurement harness (issue #49). Hidden [.] tests; run on
# demand. BEEBIUM_KEYBOARD_SCAN_DIAGNOSTICS compiles the temporary scan-probe
# observer into this target only (absent from shipping builds).
add_executable(test_keyboard_pacing test_keyboard_pacing.cpp)
target_link_libraries(test_keyboard_pacing PRIVATE beebium_core Catch2::Catch2WithMain)
target_compile_definitions(test_keyboard_pacing PRIVATE
    BEEBIUM_ROM_DIR="${BEEBIUM_ROM_DIR}"
    BEEBIUM_KEYBOARD_SCAN_DIAGNOSTICS
)
catch_discover_tests(test_keyboard_pacing)

# Keyboard lock keys tests (Caps Lock and Shift Lock)
add_executable(test_keyboard_lock_keys test_keyboard_lock_keys.cpp)
target_link_libraries(test_keyboard_lock_keys PRIVATE beebium_core Catch2::Catch2WithMain)
target_compile_definitions(test_keyboard_lock_keys PRIVATE
    BEEBIUM_ROM_DIR="${BEEBIUM_ROM_DIR}"
)
catch_discover_tests(test_keyboard_lock_keys)

# Indicator filter tests
add_executable(test_indicator_filter test_indicator_filter.cpp)
target_link_libraries(test_indicator_filter PRIVATE beebium_core Catch2::Catch2WithMain)
catch_discover_tests(test_indicator_filter)

# Indicators class tests
add_executable(test_indicators test_indicators.cpp)
target_link_libraries(test_indicators PRIVATE beebium_core Catch2::Catch2WithMain)
catch_discover_tests(test_indicators)

# Advertiser tests (discovery library)
add_executable(test_advertiser test_advertiser.cpp)
target_link_libraries(test_advertiser PRIVATE beebium_discovery Catch2::Catch2WithMain)
# Allow skipped tests to pass (mDNS may not be available on CI)
catch_discover_tests(test_advertiser EXTRA_ARGS --allow-running-no-tests)

# Tube ULA tests
add_executable(test_tube_ula test_tube_ula.cpp)
target_link_libraries(test_tube_ula PRIVATE beebium_core Catch2::Catch2WithMain)
catch_discover_tests(test_tube_ula)

# Tube socket tests
add_executable(test_tube_socket test_tube_socket.cpp)
target_link_libraries(test_tube_socket PRIVATE beebium_core Catch2::Catch2WithMain)
catch_discover_tests(test_tube_socket)

# Parasite memory map tests (6502 second processor)
add_executable(test_parasite_memory_map test_parasite_memory_map.cpp)
target_link_libraries(test_parasite_memory_map PRIVATE beebium_core Catch2::Catch2WithMain)
catch_discover_tests(test_parasite_memory_map)

# Parasite CPU tests
add_executable(test_parasite_cpu test_parasite_cpu.cpp)
target_link_libraries(test_parasite_cpu PRIVATE beebium_core Catch2::Catch2WithMain)
catch_discover_tests(test_parasite_cpu)

# Parasite runner tests
add_executable(test_parasite_runner test_parasite_runner.cpp)
target_link_libraries(test_parasite_runner PRIVATE beebium_core Catch2::Catch2WithMain)
catch_discover_tests(test_parasite_runner)

# Parasite boot integration tests (real ROM)
add_executable(test_parasite_boot test_parasite_boot.cpp)
target_link_libraries(test_parasite_boot PRIVATE beebium_core Catch2::Catch2WithMain)
target_compile_definitions(test_parasite_boot PRIVATE
    BEEBIUM_ROM_DIR="${BEEBIUM_ROM_DIR}"
)
catch_discover_tests(test_parasite_boot)

# Tube in-process tests (ParasiteRunner backed by TubeUla, no shared memory)
add_executable(test_tube_inprocess test_tube_inprocess.cpp)
target_link_libraries(test_tube_inprocess PRIVATE beebium_core Catch2::Catch2WithMain)
target_compile_definitions(test_tube_inprocess PRIVATE
    BEEBIUM_ROM_DIR="${BEEBIUM_ROM_DIR}"
)
catch_discover_tests(test_tube_inprocess)

# Tube 65C02 coprocessor extension tests
add_executable(test_tube_extension test_tube_extension.cpp)
target_include_directories(test_tube_extension PRIVATE
    ${CMAKE_SOURCE_DIR}/src/extensions/acorn-65c02-coprocessor
)
target_link_libraries(test_tube_extension PRIVATE
    beebium_ext_acorn_65c02
    Catch2::Catch2WithMain
)
target_compile_definitions(test_tube_extension PRIVATE
    BEEBIUM_ROM_DIR="${BEEBIUM_ROM_DIR}"
)
catch_discover_tests(test_tube_extension)

# Host-serial extension test (POSIX-only: exercises a real pty).
if(NOT WIN32)
    add_executable(test_host_serial_extension test_host_serial_extension.cpp)
    target_include_directories(test_host_serial_extension PRIVATE
        ${CMAKE_SOURCE_DIR}/src/extensions/host-serial
    )
    target_link_libraries(test_host_serial_extension PRIVATE
        beebium_ext_host_serial
        beebium_extension_ui_proto
        Catch2::Catch2WithMain
    )
    catch_discover_tests(test_host_serial_extension)
endif()

# Host-serial endpoint test (cross-platform: stuck-peer back-pressure, no pty).
add_executable(test_host_serial_endpoint test_host_serial_endpoint.cpp)
target_include_directories(test_host_serial_endpoint PRIVATE
    ${CMAKE_SOURCE_DIR}/src/extensions/host-serial
)
target_link_libraries(test_host_serial_endpoint PRIVATE
    beebium_ext_host_serial
    Catch2::Catch2WithMain
)
catch_discover_tests(test_host_serial_endpoint)

# Serial-loopback extension test (cross-platform; no pty).
add_executable(test_loopback_serial_extension test_loopback_serial_extension.cpp)
target_include_directories(test_loopback_serial_extension PRIVATE
    ${CMAKE_SOURCE_DIR}/src/extensions/loopback-serial
)
target_link_libraries(test_loopback_serial_extension PRIVATE
    beebium_ext_loopback_serial
    Catch2::Catch2WithMain
)
catch_discover_tests(test_loopback_serial_extension)

# rpc-serial extension test (needs the service layer for the RpcSerial proto).
if(BEEBIUM_BUILD_SERVICE)
    add_executable(test_rpc_serial_extension test_rpc_serial_extension.cpp)
    target_include_directories(test_rpc_serial_extension PRIVATE
        ${CMAKE_SOURCE_DIR}/src/extensions/rpc-serial
    )
    target_link_libraries(test_rpc_serial_extension PRIVATE
        beebium_ext_rpc_serial
        Catch2::Catch2WithMain
    )
    catch_discover_tests(test_rpc_serial_extension)
endif()

# Serial extension UI panel (Peripherals sidebar; extension_ui.pb.h types). Only
# host-serial has a panel; rpc-serial and loopback-serial are panel-less (the
# sidebar lists them by name, but they have nothing to show or configure).
add_executable(test_host_serial_ui test_host_serial_ui.cpp)
target_link_libraries(test_host_serial_ui PRIVATE
    beebium_ext_host_serial
    beebium_extension_ui_proto
    Catch2::Catch2WithMain
)
catch_discover_tests(test_host_serial_ui)

# Tube boot screen integration tests (host + parasite, real ROMs)
add_executable(test_boot_tube test_boot_tube.cpp)
target_link_libraries(test_boot_tube PRIVATE beebium_core Catch2::Catch2WithMain)
target_compile_definitions(test_boot_tube PRIVATE
    BEEBIUM_ROM_DIR="${BEEBIUM_ROM_DIR}"
    BEEBIUM_TEST_ROM_DIR="${BEEBIUM_TEST_ROM_DIR}"
)
catch_discover_tests(test_boot_tube)

# Tube R1 transfer with real 65C02 CPU
add_executable(test_tube_r1_6502 test_tube_r1_6502.cpp)
target_link_libraries(test_tube_r1_6502 PRIVATE beebium_core 6502_lib Catch2::Catch2WithMain)
catch_discover_tests(test_tube_r1_6502)

# Tube R3 NMI transfer with real 65C02 CPU
add_executable(test_tube_r3_nmi_6502 test_tube_r3_nmi_6502.cpp)
target_link_libraries(test_tube_r3_nmi_6502 PRIVATE beebium_core 6502_lib Catch2::Catch2WithMain)
catch_discover_tests(test_tube_r3_nmi_6502)

# Tube R2 transfer with real 65C02 CPU
add_executable(test_tube_r2_6502 test_tube_r2_6502.cpp)
target_link_libraries(test_tube_r2_6502 PRIVATE beebium_core 6502_lib Catch2::Catch2WithMain)
catch_discover_tests(test_tube_r2_6502)

# Tube R4 transfer with real 65C02 CPU
add_executable(test_tube_r4_6502 test_tube_r4_6502.cpp)
target_link_libraries(test_tube_r4_6502 PRIVATE beebium_core 6502_lib Catch2::Catch2WithMain)
catch_discover_tests(test_tube_r4_6502)

# Tube R1 P-to-H transfer with real 65C02 CPU
add_executable(test_tube_r1_p2h_6502 test_tube_r1_p2h_6502.cpp)
target_link_libraries(test_tube_r1_p2h_6502 PRIVATE beebium_core 6502_lib Catch2::Catch2WithMain)
catch_discover_tests(test_tube_r1_p2h_6502)

# Tube R3 P-to-H transfer with real 65C02 CPU
add_executable(test_tube_r3_p2h_6502 test_tube_r3_p2h_6502.cpp)
target_link_libraries(test_tube_r3_p2h_6502 PRIVATE beebium_core 6502_lib Catch2::Catch2WithMain)
catch_discover_tests(test_tube_r3_p2h_6502)

# Tube PIRQ-driven transfer with real 65C02 CPU
add_executable(test_tube_pirq_6502 test_tube_pirq_6502.cpp)
target_link_libraries(test_tube_pirq_6502 PRIVATE beebium_core 6502_lib Catch2::Catch2WithMain)
catch_discover_tests(test_tube_pirq_6502)

# CE2023 divergence trace test
add_executable(test_tube_ce2023_trace test_tube_ce2023_trace.cpp)
target_link_libraries(test_tube_ce2023_trace PRIVATE beebium_core 6502_lib Catch2::Catch2WithMain)
target_compile_definitions(test_tube_ce2023_trace PRIVATE
    BEEBIUM_ROM_DIR="${BEEBIUM_ROM_DIR}"
    BEEBIUM_TEST_ROM_DIR="${BEEBIUM_TEST_ROM_DIR}"
    BEEBIUM_TEST_ASSETS_DIR="${BEEBIUM_TEST_ASSETS_DIR}"
)
catch_discover_tests(test_tube_ce2023_trace)

# End-to-end serial BREAK: a beebasm-assembled 6502 program on an auto-booting
# DFS disc drives/detects a break against a host peer (recording device / real
# pySerial client). SKIPs when beebasm / ROMs / pySerial are absent.
add_executable(test_serial_break_e2e test_serial_break_e2e.cpp)
target_include_directories(test_serial_break_e2e
    PRIVATE ${CMAKE_SOURCE_DIR}/src/extensions/rfc2217-client-serial
            ${CMAKE_SOURCE_DIR}/src/extensions/rfc2217-server-serial)
target_link_libraries(test_serial_break_e2e
    PRIVATE beebium_ext_rfc2217_client beebium_ext_rfc2217_server beebium_core
            6502_lib Threads::Threads Catch2::Catch2WithMain)
target_compile_definitions(test_serial_break_e2e PRIVATE
    BEEBIUM_ROM_DIR="${BEEBIUM_ROM_DIR}"
    BEEBIUM_TEST_ASSETS_DIR="${BEEBIUM_TEST_ASSETS_DIR}"
)
catch_discover_tests(test_serial_break_e2e PROPERTIES SKIP_RETURN_CODE 4)



# Tube control flag tests with real 65C02 CPU
add_executable(test_tube_control_6502 test_tube_control_6502.cpp)
target_link_libraries(test_tube_control_6502 PRIVATE beebium_core 6502_lib Catch2::Catch2WithMain)
catch_discover_tests(test_tube_control_6502)


# gRPC service tests (only if service layer is built)
if(BEEBIUM_BUILD_SERVICE)
    # Video service tests
    add_executable(test_grpc_video test_grpc_video.cpp)
    target_link_libraries(test_grpc_video PRIVATE
        beebium_service
        Catch2::Catch2WithMain
    )
    target_compile_definitions(test_grpc_video PRIVATE
        BEEBIUM_ROM_DIR="${BEEBIUM_ROM_DIR}"
    )
    catch_discover_tests(test_grpc_video)

    # Keyboard service tests
    add_executable(test_grpc_keyboard test_grpc_keyboard.cpp)
    target_link_libraries(test_grpc_keyboard PRIVATE
        beebium_service
        Catch2::Catch2WithMain
    )
    target_compile_definitions(test_grpc_keyboard PRIVATE
        BEEBIUM_ROM_DIR="${BEEBIUM_ROM_DIR}"
    )
    catch_discover_tests(test_grpc_keyboard)

    # Debugger service tests
    add_executable(test_grpc_debugger test_grpc_debugger.cpp)
    target_link_libraries(test_grpc_debugger PRIVATE
        beebium_service
        Catch2::Catch2WithMain
    )
    target_compile_definitions(test_grpc_debugger PRIVATE
        BEEBIUM_ROM_DIR="${BEEBIUM_ROM_DIR}"
    )
    catch_discover_tests(test_grpc_debugger)

    # Disc service tests
    add_executable(test_grpc_disc test_grpc_disc.cpp)
    target_link_libraries(test_grpc_disc PRIVATE
        beebium_service
        Catch2::Catch2WithMain
    )
    target_compile_definitions(test_grpc_disc PRIVATE
        BEEBIUM_ROM_DIR="${BEEBIUM_ROM_DIR}"
        BEEBIUM_DISCS_DIR="${BEEBIUM_DISCS_DIR}"
    )
    catch_discover_tests(test_grpc_disc)

    # Indicator service tests
    add_executable(test_grpc_indicator test_grpc_indicator.cpp)
    target_link_libraries(test_grpc_indicator PRIVATE
        beebium_service
        beebium_ext_scsi_hard_disc
        Catch2::Catch2WithMain
    )
    target_compile_definitions(test_grpc_indicator PRIVATE
        BEEBIUM_ROM_DIR="${BEEBIUM_ROM_DIR}"
        BEEBIUM_DISCS_DIR="${BEEBIUM_DISCS_DIR}"
        BEEBIUM_TEST_ASSETS_DIR="${BEEBIUM_TEST_ASSETS_DIR}"
    )
    catch_discover_tests(test_grpc_indicator)

    # Audio service tests
    add_executable(test_grpc_audio test_grpc_audio.cpp)
    target_link_libraries(test_grpc_audio PRIVATE
        beebium_service
        Catch2::Catch2WithMain
    )
    target_compile_definitions(test_grpc_audio PRIVATE
        BEEBIUM_ROM_DIR="${BEEBIUM_ROM_DIR}"
    )
    catch_discover_tests(test_grpc_audio)

    # Sideways service tests
    add_executable(test_grpc_sideways test_grpc_sideways.cpp)
    target_link_libraries(test_grpc_sideways PRIVATE
        beebium_service
        Catch2::Catch2WithMain
    )
    target_compile_definitions(test_grpc_sideways PRIVATE
        BEEBIUM_ROM_DIR="${BEEBIUM_ROM_DIR}"
    )
    catch_discover_tests(test_grpc_sideways)

    # Plugin loader tests
    add_executable(test_plugin_loader test_plugin_loader.cpp)
    # The dlopen'd plugins resolve the extension ABI from the linked
    # beebium_extension_api dylib (pulled in transitively here), so no host
    # symbol export / force-load is needed -- plugins no longer host gRPC
    # services or resolve a host-side runtime.
    target_link_libraries(test_plugin_loader PRIVATE
        beebium_core
        beebium_service
        Catch2::Catch2WithMain
    )
    # Point the test at the canonical deploy location populated by
    # beebium_finalize_plugin() (<server-exe-dir>/extensions/<name>/),
    # matching what the server's default --extension-dir discovery
    # looks at. Uses a generator expression so multi-config generators
    # (MSBuild, Xcode) resolve the right $(Configuration) subdirectory.
    target_compile_definitions(test_plugin_loader PRIVATE
        BEEBIUM_PLUGIN_DIR="$<TARGET_FILE_DIR:beebium-model-b>/extensions/test-scratch-ram"
        # The parent extensions/ dir holds every deployed plugin (for the
        # manifest/code attaches_to drift guard).
        BEEBIUM_EXTENSIONS_DIR="$<TARGET_FILE_DIR:beebium-model-b>/extensions"
    )
    # Ensure the plugins are deployed before the test runs: test-scratch-ram for
    # the load tests, and every peripheral plugin for the attaches_to drift guard.
    add_dependencies(test_plugin_loader
        beebium_ext_test_scratch_ram_plugin
        beebium_ext_acorn_rtc_plugin
        beebium_ext_acorn_scsi_plugin
        beebium_ext_scsi_hard_disc_plugin
        beebium_ext_ip232_serial_plugin
        beebium_ext_loopback_serial_plugin
        beebium_ext_rpc_serial_plugin
        beebium_ext_rfc2217_client_plugin
        beebium_ext_rfc2217_server_plugin
    )
    catch_discover_tests(test_plugin_loader)

    # Regression test for issue #42: SIGINT crash when shutting down
    # beebium-model-b-romram with a mounted SCSI hard disc image. Loads the
    # acorn-scsi and scsi-hard-disc plugins via PluginLoader, registers an
    # HDD activity indicator (whose filter vtable lives in scsi-hard-disc.dylib),
    # then dlclose's the plugins before the Indicators registry is destroyed
    # -- the same destruction order the server has at exit.
    add_executable(test_scsi_plugin_shutdown test_scsi_plugin_shutdown.cpp)
    # dlopen's the acorn-scsi / scsi-hard-disc plugins; they resolve the
    # extension ABI from the linked beebium_extension_api dylib, so no host
    # symbol export / force-load is needed.
    target_link_libraries(test_scsi_plugin_shutdown PRIVATE
        beebium_core
        beebium_service
        Catch2::Catch2WithMain
    )
    target_compile_definitions(test_scsi_plugin_shutdown PRIVATE
        BEEBIUM_PLUGIN_EXTENSIONS_DIR="$<TARGET_FILE_DIR:beebium-model-b>/extensions"
    )
    add_dependencies(test_scsi_plugin_shutdown
        beebium_ext_acorn_scsi_plugin
        beebium_ext_scsi_hard_disc_plugin
    )
    catch_discover_tests(test_scsi_plugin_shutdown)

    # PeripheralExtensionService gRPC tests (frontend discovery)
    add_executable(test_grpc_peripheral_extension test_grpc_peripheral_extension.cpp)
    target_link_libraries(test_grpc_peripheral_extension PRIVATE
        beebium_service
        beebium_ext_test_scratch_ram
        Catch2::Catch2WithMain
    )
    catch_discover_tests(test_grpc_peripheral_extension)

    # ScratchRam extension gRPC service tests
    add_executable(test_grpc_scratch_ram test_grpc_scratch_ram.cpp)
    target_link_libraries(test_grpc_scratch_ram PRIVATE
        beebium_service
        beebium_ext_test_scratch_ram
        Catch2::Catch2WithMain
    )
    catch_discover_tests(test_grpc_scratch_ram)

    # SCSI host adapter gRPC service tests
    add_executable(test_grpc_scsi_host_adapter test_grpc_scsi_host_adapter.cpp)
    target_link_libraries(test_grpc_scsi_host_adapter PRIVATE
        beebium_service
        beebium_ext_acorn_scsi
        Catch2::Catch2WithMain
    )
    catch_discover_tests(test_grpc_scsi_host_adapter)

    # User Port RTC gRPC service tests
    add_executable(test_grpc_acorn_rtc test_grpc_acorn_rtc.cpp)
    target_link_libraries(test_grpc_acorn_rtc PRIVATE
        beebium_service
        beebium_ext_acorn_rtc
        Catch2::Catch2WithMain
    )
    catch_discover_tests(test_grpc_acorn_rtc)

    # Econet service tests
    add_executable(test_grpc_econet test_grpc_econet.cpp)
    target_link_libraries(test_grpc_econet PRIVATE
        beebium_service
        Catch2::Catch2WithMain
    )
    target_compile_definitions(test_grpc_econet PRIVATE
        BEEBIUM_ROM_DIR="${BEEBIUM_ROM_DIR}"
    )
    catch_discover_tests(test_grpc_econet)

    # AUN service tests (the AUN-specific RPCs that used to live on EconetService)
    add_executable(test_grpc_aun_service test_grpc_aun_service.cpp)
    target_link_libraries(test_grpc_aun_service PRIVATE
        beebium_service
        beebium_ext_aun
        Catch2::Catch2WithMain
    )
    catch_discover_tests(test_grpc_aun_service)

    # AunUi unit tests (build_view shape against varying peer-table state)
    add_executable(test_aun_ui test_aun_ui.cpp)
    target_link_libraries(test_aun_ui PRIVATE
        beebium_ext_aun
        beebium_extension_api
        beebium_extension_ui_proto
        Catch2::Catch2WithMain
    )
    catch_discover_tests(test_aun_ui)

    # EconetTransportService discovery tests
    add_executable(test_grpc_econet_transport_service
        test_grpc_econet_transport_service.cpp)
    target_link_libraries(test_grpc_econet_transport_service PRIVATE
        beebium_service
        beebium_ext_aun
        Catch2::Catch2WithMain
    )
    catch_discover_tests(test_grpc_econet_transport_service)

    # ExtensionUiService schema round-trip tests (no server needed)
    add_executable(test_extension_ui_proto test_extension_ui_proto.cpp)
    target_link_libraries(test_extension_ui_proto PRIVATE
        beebium_service
        Catch2::Catch2WithMain
    )
    catch_discover_tests(test_extension_ui_proto)

    # ExtensionUiService end-to-end tests with a fake extension
    add_executable(test_grpc_extension_ui_service
        test_grpc_extension_ui_service.cpp)
    target_link_libraries(test_grpc_extension_ui_service PRIVATE
        beebium_service
        Catch2::Catch2WithMain
    )
    catch_discover_tests(test_grpc_extension_ui_service)

    # ExtensionRpc end-to-end tests: a fake extension exposes an
    # ExtensionRpcDispatcher, driven through the real ExtensionRpcServiceImpl
    # over an in-process gRPC server (routing, error mapping, server-stream).
    add_executable(test_grpc_extension_rpc
        test_grpc_extension_rpc.cpp)
    target_link_libraries(test_grpc_extension_rpc PRIVATE
        beebium_service
        Catch2::Catch2WithMain
    )
    catch_discover_tests(test_grpc_extension_rpc)

    # Piconet dispatcher: PiconetService.GetStatus served gRPC-free through the
    # ExtensionRpc channel. Links the static extension build (one protobuf
    # descriptor copy), not the plugin.
    add_executable(test_piconet_dispatcher test_piconet_dispatcher.cpp)
    target_link_libraries(test_piconet_dispatcher PRIVATE
        beebium_ext_piconet
        Catch2::Catch2WithMain
    )
    catch_discover_tests(test_piconet_dispatcher)

    # Piconet gRPC service tests. The plugin DLL no longer carries
    # PiconetService (gRPC's per-module state caused heap corruption
    # when piconet.dll linked gRPC alongside the EXE -- see
    # docs/discussion/grpc-windows-streaming-race.md). Restoring this
    # test requires re-introducing PiconetService as a STATIC library
    # linked into the EXE, mirroring the beebium_extension_ui_service
    # split done for the framework's UI service. Until then the test is
    # disabled.
    if(FALSE)
    add_executable(test_grpc_piconet_service
        test_grpc_piconet_service.cpp
        piconet/FakePiconetDevice.cpp
        ${PICONET_SERIAL_BRIDGE_SRCS})
    target_link_libraries(test_grpc_piconet_service PRIVATE
        beebium_service
        beebium_ext_piconet_plugin
        beebium_piconet_core
        Catch2::Catch2WithMain
    )
    target_include_directories(test_grpc_piconet_service PRIVATE
        ${CMAKE_CURRENT_SOURCE_DIR}
        ${CMAKE_SOURCE_DIR}/src/extensions/piconet)
    catch_discover_tests(test_grpc_piconet_service)
    endif()

    # Piconet ModalEditor / EditableChoice end-to-end through the
    # ExtensionUiService gRPC stub: SubscribeView -> Dispatch
    # (EditorCommit) -> emulation tick drains pending reopen ->
    # next pushed View shows the new path.
    #
    # POSIX-only: the test substitutes a FakePiconetDeviceOnPipe (a
    # named pipe) for the real serial device so the production code
    # path is exercised with the device replaced. On Windows that
    # substitution feeds a non-COM handle to Win32SerialPort, which
    # triggers a kernel-side stack write during a syscall on the test
    # machine's environment -- a fault that production never sees
    # because production only ever opens real COM ports. See
    # docs/discussion/test-grpc-piconet-ui-windows-av.md for the full
    # TTD-based investigation that established the test path is not
    # the production path.
    if(NOT WIN32)
    add_executable(test_grpc_piconet_ui
        test_grpc_piconet_ui.cpp
        piconet/FakePiconetDevice.cpp
        ${PICONET_SERIAL_BRIDGE_SRCS})
    target_link_libraries(test_grpc_piconet_ui PRIVATE
        beebium_service
        beebium_ext_piconet_plugin
        beebium_piconet_core
        beebium_extension_api
        Catch2::Catch2WithMain
    )
    target_include_directories(test_grpc_piconet_ui PRIVATE
        ${CMAKE_CURRENT_SOURCE_DIR}
        ${CMAKE_SOURCE_DIR}/src/extensions/piconet)
    catch_discover_tests(test_grpc_piconet_ui)
    endif()

    # PiconetUi unit tests (build_view shape + handle_event SET_MODE
    # round-trip via FakePiconetDevice).
    #
    # POSIX-only, for the same reason as test_grpc_piconet_ui above: the
    # FakePiconetDevice serial bridge resolves to a named pipe on Windows, and
    # PiconetBackend's reader thread feeding that non-COM handle to
    # Win32SerialPort triggers a Windows-only, timing-sensitive fault in the
    # test environment that production never sees (production only opens real
    # COM ports). See docs/discussion/test-grpc-piconet-ui-windows-av.md. The
    # race was latent until the extension's layout shifted (the ExtensionRpc
    # dispatcher member) nudged the timing; the macOS/Linux PTY path exercises
    # the same PiconetUi logic and passes.
    if(NOT WIN32)
    add_executable(test_piconet_ui
        test_piconet_ui.cpp
        piconet/FakePiconetDevice.cpp
        ${PICONET_SERIAL_BRIDGE_SRCS})
    # Link the STATIC extension build, not the plugin DLL: this test constructs
    # PiconetEconetTransportExtension directly rather than dlopen'ing it.
    target_link_libraries(test_piconet_ui PRIVATE
        beebium_ext_piconet
        beebium_piconet_core
        beebium_extension_api
        beebium_extension_ui_proto
        Catch2::Catch2WithMain
    )
    target_include_directories(test_piconet_ui PRIVATE
        ${CMAKE_CURRENT_SOURCE_DIR}
        ${CMAKE_SOURCE_DIR}/src/extensions/piconet)
    catch_discover_tests(test_piconet_ui)
    endif()

    # System service tests
    add_executable(test_grpc_system test_grpc_system.cpp)
    target_link_libraries(test_grpc_system PRIVATE
        beebium_service
        Catch2::Catch2WithMain
    )
    target_compile_definitions(test_grpc_system PRIVATE
        BEEBIUM_ROM_DIR="${BEEBIUM_ROM_DIR}"
    )
    catch_discover_tests(test_grpc_system)

    # Server main helper function tests
    add_executable(test_server_main test_server_main.cpp)
    target_include_directories(test_server_main PRIVATE
        ${CMAKE_SOURCE_DIR}/src/server/include
    )
    target_link_libraries(test_server_main PRIVATE
        beebium_service
        beebium_ext_test_scratch_ram
        beebium_ext_acorn_65c02
        beebium_ext_aun
        beebium_ext_host_serial
        nlohmann_json::nlohmann_json
        stb_image_write
        Catch2::Catch2WithMain
    )
    target_compile_definitions(test_server_main PRIVATE
        BEEBIUM_VERSION="${PROJECT_VERSION}"
        BEEBIUM_ROM_DIR="${BEEBIUM_ROM_DIR}"
    )
    catch_discover_tests(test_server_main)

    # CLI subcommand architecture tests
    add_executable(test_cli test_cli.cpp)
    target_include_directories(test_cli PRIVATE
        ${CMAKE_SOURCE_DIR}/src/server/include
    )
    target_link_libraries(test_cli PRIVATE
        beebium_service
        beebium_ext_test_scratch_ram
        beebium_ext_acorn_65c02
        beebium_ext_aun
        beebium_ext_host_serial
        nlohmann_json::nlohmann_json
        stb_image_write
        Catch2::Catch2WithMain
    )
    target_compile_definitions(test_cli PRIVATE
        BEEBIUM_VERSION="${PROJECT_VERSION}"
        BEEBIUM_TEST_ASSETS_DIR="${BEEBIUM_TEST_ASSETS_DIR}"
    )
    catch_discover_tests(test_cli)

    # Preset loader tests
    add_executable(test_preset_loader test_preset_loader.cpp)
    target_include_directories(test_preset_loader PRIVATE
        ${CMAKE_SOURCE_DIR}/src/server/include
    )
    target_link_libraries(test_preset_loader PRIVATE
        beebium_service
        nlohmann_json::nlohmann_json
        Catch2::Catch2WithMain
    )
    target_compile_definitions(test_preset_loader PRIVATE
        BEEBIUM_TEST_ASSETS_DIR="${BEEBIUM_TEST_ASSETS_DIR}"
    )
    catch_discover_tests(test_preset_loader)

    # Preset paths utility tests
    add_executable(test_preset_paths test_preset_paths.cpp)
    target_include_directories(test_preset_paths PRIVATE
        ${CMAKE_SOURCE_DIR}/src/server/include
    )
    target_link_libraries(test_preset_paths PRIVATE
        beebium_service
        Catch2::Catch2WithMain
    )
    catch_discover_tests(test_preset_paths)

    # Preset subcommand integration tests
    add_executable(test_preset_subcommands test_preset_subcommands.cpp)
    target_include_directories(test_preset_subcommands PRIVATE
        ${CMAKE_SOURCE_DIR}/src/server/include
    )
    target_link_libraries(test_preset_subcommands PRIVATE
        beebium_core
        nlohmann_json::nlohmann_json
        Catch2::Catch2WithMain
    )
    target_compile_definitions(test_preset_subcommands PRIVATE
        BEEBIUM_TEST_ASSETS_DIR="${BEEBIUM_TEST_ASSETS_DIR}"
    )
    # Integration tests depend on executables being built
    add_dependencies(test_preset_subcommands beebium-servers)
    # These tests share temp files and a user presets directory, so they must
    # run sequentially to avoid races when CTest runs tests in parallel.
    catch_discover_tests(test_preset_subcommands PROPERTIES RESOURCE_LOCK preset_dir)

    # Extension subcommand integration tests (list-extensions, describe-extension)
    add_executable(test_extension_subcommands test_extension_subcommands.cpp)
    target_link_libraries(test_extension_subcommands PRIVATE
        beebium_core
        Catch2::Catch2WithMain
    )
    add_dependencies(test_extension_subcommands beebium-servers)
    # These tests share temp files for stdout/stderr capture and must
    # serialise to avoid races when CTest runs tests in parallel.
    catch_discover_tests(test_extension_subcommands PROPERTIES RESOURCE_LOCK ext_subcommand_temp)
endif()

# Step performance benchmarks (run with --benchmark-samples N, not during normal ctest)
add_executable(test_step_performance test_step_performance.cpp)
target_link_libraries(test_step_performance PRIVATE beebium_core Catch2::Catch2WithMain)
catch_discover_tests(test_step_performance)

# Pacing controller PI tests
add_executable(test_pacing_controller test_pacing_controller.cpp)
target_link_libraries(test_pacing_controller PRIVATE beebium_core Catch2::Catch2WithMain)
catch_discover_tests(test_pacing_controller)

# Sleep quantum measurement (platform-specific sleep techniques)
add_executable(test_sleep_quantum test_sleep_quantum.cpp)
target_link_libraries(test_sleep_quantum PRIVATE Catch2::Catch2WithMain)
catch_discover_tests(test_sleep_quantum)

# Install smoke test: verify a `cmake --install` tree is actually runnable --
# ABI shared libraries resolve via the servers' INSTALL_RPATH, the plugin tree
# is discovered from <prefix>/bin/extensions, ROMs resolve through the
# <prefix>/share/beebium/roms fallback, and the gRPC server boots. This guards
# every downstream package format (.deb, Homebrew, tarball) since they all rest
# on the same install rules and runtime discovery paths. Requires a prior build
# of the server and plugin targets (CTest does not build them). Labelled
# "packaging" so it can be selected with `ctest -L packaging` or skipped with
# `ctest -LE packaging`.
#
# Linux/macOS only: the install tree relies on Unix INSTALL_RPATH ($ORIGIN /
# @loader_path) so the server in bin/ resolves the ABI libraries in lib/. Windows
# has no RPATH equivalent and is not a packaging target (the packages are the
# Linux .deb/.tar.gz and the macOS Homebrew tap), so the relocated tree is not
# expected to run there (the server would fail with a missing-DLL error).
if(BEEBIUM_BUILD_SERVER AND NOT WIN32)
    add_test(
        NAME install_smoke
        COMMAND ${CMAKE_COMMAND}
            -DBINARY_DIR=${CMAKE_BINARY_DIR}
            -DCONFIG=$<CONFIG>
            -DEXE_SUFFIX=${CMAKE_EXECUTABLE_SUFFIX}
            -P ${CMAKE_SOURCE_DIR}/cmake/InstallSmokeTest.cmake
    )
    set_tests_properties(install_smoke PROPERTIES LABELS "packaging")
endif()

# The test bodies poke 6502 addresses and byte values with integer literals, so
# MSVC /W4 reports a torrent of C4244/C4267 narrowing conversions that GCC and
# Clang (-Wall -Wextra) accept as safe. Catch2's decomposition macros likewise
# trip C4805 (bool/int mix). None indicate a defect in the test logic, so
# silence them for the test executables while the production libraries keep the
# strict narrowing diagnostics. Mirrors the generated-code suppression in
# src/service/CMakeLists.txt.
if(MSVC)
    get_property(_beebium_dir_targets DIRECTORY PROPERTY BUILDSYSTEM_TARGETS)
    foreach(_beebium_target ${_beebium_dir_targets})
        get_target_property(_beebium_target_type ${_beebium_target} TYPE)
        if(_beebium_target_type STREQUAL "EXECUTABLE")
            target_compile_options(${_beebium_target} PRIVATE /wd4244 /wd4267 /wd4805)
        endif()
    endforeach()
endif()
