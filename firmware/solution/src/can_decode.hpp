#pragma once
#include <dbcppp/Network.h>
#include <cstdint>
#include <memory>
#include <string>
#include <unordered_map>
#include <vector>
#include <ostream>

namespace rbk {

struct ParsedLine {
    double timestamp = 0.0;
    std::string iface;
    uint32_t can_id = 0;
    std::vector<uint8_t> data;
};

// Canonicalize canX/vcanX to canX
std::string canonical_iface(const std::string& s);

// Parse one cangen/candump-style line: "(ts) iface ID#HEXDATA"
bool parse_line(const std::string& line, ParsedLine& out);

// Load a DBC from path
std::unique_ptr<dbcppp::INetwork> load_network(const std::string& path);

// Map message id -> message*
using MsgMap = std::unordered_map<uint32_t, const dbcppp::IMessage*>;
MsgMap build_msg_map(const dbcppp::INetwork& net);

// Decode all signals for one message+payload and write lines:
// (timestamp): SignalName: value
// Returns number of signals written.
size_t decode_and_write(
    const ParsedLine& pl,
    const dbcppp::INetwork& net,
    const MsgMap& mmap,
    std::ostream& os);

} // namespace rbk
