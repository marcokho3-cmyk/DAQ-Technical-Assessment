#include <dbcppp/Network.h>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <regex>
#include <sstream>
#include <unordered_map>
#include <vector>

// Helpers
struct ParsedLine {
    double timestamp = 0.0;              // e.g. 1705638799.992057
    std::string iface;                   // e.g. vcan0
    uint32_t can_id = 0;                 // hex ID parsed into number
    std::vector<uint8_t> data;           // up to 64 bytes (we’ll use 8 here)
};

// Trim iface to canonical key (accept can0/vcan0)
static std::string canonical_iface(const std::string& s) {
    if (s == "can0" || s == "vcan0") return "can0";
    if (s == "can1" || s == "vcan1") return "can1";
    if (s == "can2" || s == "vcan2") return "can2";
    return s;
}

static bool parse_line(const std::string& line, ParsedLine& out) {
    // Example: (1705638799.992057) vcan0       705#B1B8E3680F488B72
    static const std::regex rx(
        R"(^\(([\d]+\.[\d]+)\)\s+([A-Za-z0-9_]+)\s+([0-9A-Fa-f]+)#([0-9A-Fa-f]+)\s*$)"
    );
    std::smatch m;
    if (!std::regex_match(line, m, rx)) return false;

    out.timestamp = std::stod(m[1].str());
    out.iface = canonical_iface(m[2].str());

    // ID is hex in dump; dbcppp IMessage::Id() is numeric (decimal), so parse to number
    {
        std::stringstream ss;
        ss << std::hex << m[3].str();
        uint32_t id_hex = 0;
        ss >> id_hex;
        out.can_id = id_hex;
    }

    // Parse data hex into bytes (pairs). Classic CAN → up to 8 bytes, but be tolerant.
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

using MsgMap = std::unordered_map<uint32_t, const dbcppp::IMessage*>;

static std::unique_ptr<dbcppp::INetwork> load_network(const std::string& path) {
    std::ifstream is(path);
    if (!is) {
        std::cerr << "Failed to open DBC: " << path << "\n";
        return nullptr;
    }
    auto net = dbcppp::INetwork::LoadDBCFromIs(is);
    if (!net) {
        std::cerr << "Failed to parse DBC: " << path << "\n";
    }
    return net;
}

static MsgMap build_msg_map(const dbcppp::INetwork& net) {
    MsgMap mm;
    for (const dbcppp::IMessage& msg : net.Messages()) {
        mm.emplace(static_cast<uint32_t>(msg.Id()), &msg);
    }
    return mm;
}

int main() {
    try {
        // ---- Load DBCs per interface ----
        const std::string dbc_control  = "dbc-files/ControlBus.dbc";   // can0
        const std::string dbc_sensor   = "dbc-files/SensorBus.dbc";    // can1
        const std::string dbc_tractive = "dbc-files/TractiveBus.dbc";  // can2

        auto net_can0 = load_network(dbc_control);
        auto net_can1 = load_network(dbc_sensor);
        auto net_can2 = load_network(dbc_tractive);

        if (!net_can0 || !net_can1 || !net_can2) {
            std::cerr << "One or more DBCs failed to load. Exiting.\n";
            return 1;
        }

        auto map_can0 = build_msg_map(*net_can0);
        auto map_can1 = build_msg_map(*net_can1);
        auto map_can2 = build_msg_map(*net_can2);

        // ---- Open dump ----
        std::ifstream dump("dump.log");
        if (!dump) {
            std::cerr << "Could not open dump.log\n";
            return 1;
        }

        // ---- Prepare output ----
        std::ofstream out("output.txt");
        if (!out) {
            std::cerr << "Could not create output.txt\n";
            return 1;
        }

        // Keep full timestamp precision
        out.setf(std::ios::fmtflags(0), std::ios::floatfield);
        out << std::setprecision(15);

        std::string line;
        ParsedLine pl;
        uint8_t data_buf[64] = {0}; // dbcppp supports arbitrarily long frames; we’ll fill what we have

        while (std::getline(dump, line)) {
            if (!parse_line(line, pl)) continue;

            // Select network & message map by interface
            const dbcppp::INetwork* net = nullptr;
            const MsgMap* mmap = nullptr;
            if (pl.iface == "can0") { net = net_can0.get(); mmap = &map_can0; }
            else if (pl.iface == "can1") { net = net_can1.get(); mmap = &map_can1; }
            else if (pl.iface == "can2") { net = net_can2.get(); mmap = &map_can2; }
            else {
                // Unknown interface: skip
                continue;
            }

            auto it = mmap->find(pl.can_id);
            if (it == mmap->end()) {
                // ID not in this DBC → skip
                continue;
            }

            // Build contiguous data buffer
            std::fill(std::begin(data_buf), std::end(data_buf), 0);
            const size_t ncopy = std::min(pl.data.size(), sizeof(data_buf));
            if (ncopy > 0) std::memcpy(data_buf, pl.data.data(), ncopy);

            const dbcppp::IMessage* msg = it->second;

            // Multiplex handling per dbcppp example
            const dbcppp::ISignal* mux_sig = msg->MuxSignal();
            for (const dbcppp::ISignal& sig : msg->Signals()) {
                bool take = true;
                if (sig.MultiplexerIndicator() != dbcppp::ISignal::EMultiplexer::MuxValue) {
                    // non-muxed signal → always decode
                    take = true;
                } else {
                    // muxed signal → only decode if mux switch matches
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

                // Strict format: (timestamp): SignalName: value
                out << '(' << std::setprecision(15) << pl.timestamp << "): "
                    << sig.Name() << ": " << phys << "\n";
            }
        }

        out.flush();
        std::cout << "Decoded to output.txt\n";
        return 0;
    } catch (const std::exception& e) {
        std::cerr << "Fatal error: " << e.what() << "\n";
        return 2;
    }
}
