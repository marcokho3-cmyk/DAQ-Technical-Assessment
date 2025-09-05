#include "can_decode.hpp"
#include <cstring>
#include <fstream>
#include <iomanip>
#include <regex>
#include <sstream>

namespace rbk {

std::string canonical_iface(const std::string& s) {
    if (s == "can0" || s == "vcan0") return "can0";
    if (s == "can1" || s == "vcan1") return "can1";
    if (s == "can2" || s == "vcan2") return "can2";
    return s;
}

bool parse_line(const std::string& line, ParsedLine& out) {
    static const std::regex rx(
        R"(^\(([\d]+\.[\d]+)\)\s+([A-Za-z0-9_]+)\s+([0-9A-Fa-f]+)#([0-9A-Fa-f]+)\s*$)"
    );
    std::smatch m;
    if (!std::regex_match(line, m, rx)) return false;

    out.timestamp = std::stod(m[1].str());
    out.iface = canonical_iface(m[2].str());

    std::stringstream ss;
    ss << std::hex << m[3].str();
    uint32_t id_hex = 0;
    ss >> id_hex;
    out.can_id = id_hex;

    const std::string hex = m[4].str();
    out.data.clear();
    out.data.reserve(hex.size() / 2);
    for (size_t i = 0; i + 1 < hex.size(); i += 2) {
        std::string byte_str = hex.substr(i, 2);
        uint8_t byte = static_cast<uint8_t>(std::stoul(byte_str, nullptr, 16));
        out.data.push_back(byte);
    }
    return true;
}

std::unique_ptr<dbcppp::INetwork> load_network(const std::string& path) {
    std::ifstream is(path);
    if (!is) return nullptr;
    return dbcppp::INetwork::LoadDBCFromIs(is);
}

MsgMap build_msg_map(const dbcppp::INetwork& net) {
    MsgMap mm;
    for (const dbcppp::IMessage& msg : net.Messages()) {
        mm.emplace(static_cast<uint32_t>(msg.Id()), &msg);
    }
    return mm;
}

size_t decode_and_write(
    const ParsedLine& pl,
    const dbcppp::INetwork& /*net*/,
    const MsgMap& mmap,
    std::ostream& os)
{
    auto it = mmap.find(pl.can_id);
    if (it == mmap.end()) return 0;

    const dbcppp::IMessage* msg = it->second;

    uint8_t data_buf[64] = {0};
    const size_t ncopy = std::min(pl.data.size(), sizeof(data_buf));
    if (ncopy > 0) std::memcpy(data_buf, pl.data.data(), ncopy);

    const dbcppp::ISignal* mux_sig = msg->MuxSignal();
    size_t wrote = 0;

    for (const dbcppp::ISignal& sig : msg->Signals()) {
        bool take = true;
        if (sig.MultiplexerIndicator() == dbcppp::ISignal::EMultiplexer::MuxValue) {
            if (mux_sig) {
                const auto mux_val = mux_sig->Decode(data_buf);
                take = (mux_val == sig.MultiplexerSwitchValue());
            } else {
                take = false;
            }
        }
        if (!take) continue;

        const auto raw = sig.Decode(data_buf);
        const auto phys = sig.RawToPhys(raw);
        os << '(' << std::setprecision(15) << pl.timestamp << "): "
           << sig.Name() << ": " << phys << "\n";
        ++wrote;
    }
    return wrote;
}

} // namespace rbk
