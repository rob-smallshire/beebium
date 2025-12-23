import XCTest
@testable import Beebium

final class VideoClientTests: XCTestCase {

    @MainActor
    func testInitialState() async {
        let client = VideoClient()

        XCTAssertEqual(client.connectionState, .disconnected)
        XCTAssertNil(client.currentFrame)
        XCTAssertEqual(client.frameWidth, 736)
        XCTAssertEqual(client.frameHeight, 576)
        XCTAssertEqual(client.frameCount, 0)
    }

    @MainActor
    func testConnectionStateEquatable() async {
        XCTAssertEqual(ConnectionState.disconnected, ConnectionState.disconnected)
        XCTAssertEqual(ConnectionState.connecting, ConnectionState.connecting)
        XCTAssertEqual(ConnectionState.connected, ConnectionState.connected)
        XCTAssertEqual(ConnectionState.error("test"), ConnectionState.error("test"))

        XCTAssertNotEqual(ConnectionState.disconnected, ConnectionState.connecting)
        XCTAssertNotEqual(ConnectionState.error("a"), ConnectionState.error("b"))
    }

    @MainActor
    func testCustomHostAndPort() async {
        let client = VideoClient(host: "192.168.1.100", port: 8080)

        // Just verify it can be created with custom parameters
        XCTAssertEqual(client.connectionState, .disconnected)
    }
}
