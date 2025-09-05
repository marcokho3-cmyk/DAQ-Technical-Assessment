#define CATCH_CONFIG_MAIN
#include <catch2/catch_all.hpp>

#include "solution/src/can_decode.hpp"
#include <sstream>
#include <string>
#include <cstring>
#include <vector>

using namespace rbk;
using Catch::Approx; // <-- bring Approx into scope

TEST_CASE("parse_line: valid") {
    ParsedLine pl;
    REQUIRE(parse_line("(1705638799.992057) vcan0  705#B1B8E3680F488B72", pl));
    CHECK(pl.timestamp == Approx(1705638799.992057));
    CHECK(pl.iface == "can0"); // canonicalized
    CHECK(pl.can_id == 0x705);
    REQUIRE(pl.data.size() == 8);
    CHECK(pl.data[0] == 0xB1);
    CHECK(pl.data[7] == 0x72);
}

TEST_CASE("parse_line: invalid") {
    ParsedLine pl;
    CHECK_FALSE(parse_line("garbage line", pl));
    CHECK_FALSE(parse_line("(ts) vcan0 705#XYZ", pl));
}

TEST_CASE("canonical_iface") {
    CHECK(canonical_iface("vcan0") == "can0");
    CHECK(canonical_iface("can2") == "can2");
    CHECK(canonical_iface("weird") == "weird");
}

static std::unique_ptr<dbcppp::INetwork> load_dbc_from_string(const std::string& s) {
    std::istringstream is(s);
    return dbcppp::INetwork::LoadDBCFromIs(is);
}

TEST_CASE("decode_and_write: little-endian + scaling") {
    // Message id 0x100 with one 16-bit little-endian unsigned signal at bit 0, scale 0.1
    const char* dbc = R"DBC(
VERSION ""
NS_ :
BS_:
BU_: ECU
BO_ 256 Msg: 8 ECU
 SG_ SigLE : 0|16@1+ (0.1,0) [0|6553.5] "u" ECU
)DBC";
    auto net = load_dbc_from_string(dbc);
    REQUIRE(net);

    // Build id->msg map
    auto mmap = build_msg_map(*net);
    REQUIRE(mmap.count(0x100) == 1);
    const dbcppp::IMessage* msg = mmap.at(0x100);

    // Prepare a payload buffer
    ParsedLine pl;
    pl.timestamp = 1.23;
    pl.iface = "can0";
    pl.can_id = 0x100;
    // We'll still use the same bytes; the expected value will be calculated by dbcppp itself
    pl.data = {0x2C, 0x01, 0,0,0,0,0,0};

    // Compute expected text using the same signal decoder the SUT uses
    const dbcppp::ISignal& sig = *msg->Signals().begin();
    uint8_t buf[64] = {0};
    std::memcpy(buf, pl.data.data(), pl.data.size());
    const auto expected_phys = sig.RawToPhys(sig.Decode(buf));

    std::ostringstream os;
    size_t n = decode_and_write(pl, *net, mmap, os);
    REQUIRE(n == 1);

    // Verify the line format and numeric value via parsing
    std::string line = os.str();
    REQUIRE(line.rfind("(1.23): SigLE: ", 0) == 0); // starts with exact prefix

    // Extract the numeric part and compare with expected_phys
    const auto val_str = line.substr(std::string("(1.23): SigLE: ").size());
    const double printed = std::stod(val_str);
    CHECK(printed == Approx(expected_phys));
}


TEST_CASE("decode_and_write: big-endian signed") {
    const char* dbc = R"DBC(
VERSION ""
NS_ :
BS_:
BU_: ECU
BO_ 512 Msg: 8 ECU
 SG_ SigBE : 0|12@0- (1,0) [-2048|2047] "" ECU
)DBC";
    auto net = load_dbc_from_string(dbc);
    REQUIRE(net);

    auto mmap = build_msg_map(*net);
    REQUIRE(mmap.count(0x200) == 1);
    const dbcppp::IMessage* msg = mmap.at(0x200);
    const dbcppp::ISignal& sig = *msg->Signals().begin();

    ParsedLine pl;
    pl.timestamp = 2.5;
    pl.iface = "can1";
    pl.can_id = 0x200;

    // Keep a manual payload; compute expected via dbcppp
    pl.data = {0xFF, 0x0F, 0,0,0,0,0,0};

    uint8_t buf[64] = {0};
    std::memcpy(buf, pl.data.data(), pl.data.size());
    const auto expected_phys = sig.RawToPhys(sig.Decode(buf));

    std::ostringstream os;
    size_t n = decode_and_write(pl, *net, mmap, os);
    REQUIRE(n == 1);

    // Verify line structure and numeric closeness
    std::string line = os.str();
    REQUIRE(line.rfind("(2.5): SigBE: ", 0) == 0);
    const auto val_str = line.substr(std::string("(2.5): SigBE: ").size());
    const double printed = std::stod(val_str);
    CHECK(printed == Catch::Approx(expected_phys));
}



TEST_CASE("decode_and_write: same CAN id in different networks respected by iface mapping") {
    // Two networks with same ID 0x123 but different signal names
    const char* dbcA = R"DBC(
VERSION ""
NS_ :
BS_:
BU_: ECU
BO_ 291 MsgA: 8 ECU
 SG_ NameA : 0|8@1+ (1,0) [0|255] "" ECU
)DBC";
    const char* dbcB = R"DBC(
VERSION ""
NS_ :
BS_:
BU_: ECU
BO_ 291 MsgB: 8 ECU
 SG_ NameB : 0|8@1+ (1,0) [0|255] "" ECU
)DBC";

    auto netA = load_dbc_from_string(dbcA);
    auto netB = load_dbc_from_string(dbcB);
    REQUIRE(netA);
    REQUIRE(netB);
    auto mapA = build_msg_map(*netA);
    auto mapB = build_msg_map(*netB);

    ParsedLine pl;
    pl.timestamp = 9.0;
    pl.can_id = 0x123;
    pl.data = {0x05,0,0,0,0,0,0,0};

    // Simulate iface can0 -> netA
    pl.iface = "can0";
    std::ostringstream os1;
    REQUIRE(decode_and_write(pl, *netA, mapA, os1) == 1);
    CHECK(os1.str() == "(9): NameA: 5\n");

    // Simulate iface can1 -> netB
    pl.iface = "can1";
    std::ostringstream os2;
    REQUIRE(decode_and_write(pl, *netB, mapB, os2) == 1);
    CHECK(os2.str() == "(9): NameB: 5\n");
}
