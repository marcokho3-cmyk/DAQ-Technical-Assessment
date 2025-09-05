#include "src/dbc_simple.hpp"
#include <fstream>
#include <iostream>
#include <iomanip>
#include <regex>
#include <sstream>
#include <vector>
#include <string>

using stage4::Network;

// Canonicalize canX/vcanX to canX
static void debug_dump_signal(const stage4::Network& net, const char* name, const char* tag) {
    for (const auto& kv : net.msgs) {
        const auto& msg = kv.second;
        for (const auto& s : msg.signals) {
            if (s.name == name) {
                std::cerr << "[" << tag << "] " << name
                          << "  id=0x" << std::hex << kv.first << std::dec
                          << "  start=" << s.start_bit
                          << "  len=" << s.bit_len
                          << "  endian=" << (s.little_endian ? "LE" : "BE")
                          << "  signed=" << (s.is_signed ? "yes" : "no")
                          << "  scale=" << s.scale
                          << "  offset=" << s.offset
                          << "\n";
            }
        }
    }
}

static std::string canonical_iface(const std::string& s) {
    if (s == "can0" || s == "vcan0") return "can0";
    if (s == "can1" || s == "vcan1") return "can1";
    if (s == "can2" || s == "vcan2") return "can2";
    return s;
}

struct ParsedLine {
    double ts = 0.0;
    std::string iface;
    uint32_t id = 0;
    std::vector<uint8_t> data;
};

static bool parse_dump_line(const std::string& line, ParsedLine& out) {
    static const std::regex rx(
        R"(^\(([\d]+\.[\d]+)\)\s+([A-Za-z0-9_]+)\s+([0-9A-Fa-f]+)#([0-9A-Fa-f]+)\s*$)"
    );
    std::smatch m;
    if (!std::regex_match(line, m, rx)) return false;

    out.ts = std::stod(m[1].str());
    out.iface = canonical_iface(m[2].str());

    std::stringstream ss;
    ss << std::hex << m[3].str();
    ss >> out.id;

    const std::string hex = m[4].str();
    out.data.clear();
    out.data.reserve(hex.size() / 2);
    for (size_t i = 0; i + 1 < hex.size(); i += 2) {
        uint8_t byte = static_cast<uint8_t>(std::stoul(hex.substr(i, 2), nullptr, 16));
        out.data.push_back(byte);
    }
    return true;
}

int main() {
    // Load the three DBCs with our custom parser
    Network net0, net1, net2;
    std::string err;

    if (!stage4::parse_dbc_file("dbc-files/ControlBus.dbc", net0, &err)) {
        std::cerr << "DBC parse failed: ControlBus.dbc -> " << err << "\n";
        return 1;
    }
    if (!stage4::parse_dbc_file("dbc-files/SensorBus.dbc", net1, &err)) {
        std::cerr << "DBC parse failed: SensorBus.dbc -> " << err << "\n";
        return 1;
    }
    if (!stage4::parse_dbc_file("dbc-files/TractiveBus.dbc", net2, &err)) {
        std::cerr << "DBC parse failed: TractiveBus.dbc -> " << err << "\n";
        return 1;
    }

    // Debug dump of key signals
    debug_dump_signal(net0, "Pack_SOC", "can0");
    debug_dump_signal(net1, "Pack_SOC", "can1");
    debug_dump_signal(net2, "Pack_SOC", "can2");

    debug_dump_signal(net0, "Pack_Inst_Voltage", "can0");
    debug_dump_signal(net1, "Pack_Inst_Voltage", "can1");
    debug_dump_signal(net2, "Pack_Inst_Voltage", "can2");

    debug_dump_signal(net0, "Relay_State", "can0");
    debug_dump_signal(net1, "Relay_State", "can1");
    debug_dump_signal(net2, "Relay_State", "can2");


    std::ifstream dump("dump.log");
    if (!dump) { std::cerr << "Could not open dump.log\n"; return 1; }

    std::ofstream out("output_stage4.txt");
    if (!out) { std::cerr << "Could not create output_stage4.txt\n"; return 1; }
    out.setf(std::ios::fmtflags(0), std::ios::floatfield);
    out << std::setprecision(15);

    std::string line;
    ParsedLine pl;
    while (std::getline(dump, line)) {
        if (!parse_dump_line(line, pl)) continue;

        if (pl.iface == "can0") {
            stage4::decode_frame_and_write(net0, pl.id, pl.ts, pl.data, out);
        } else if (pl.iface == "can1") {
            stage4::decode_frame_and_write(net1, pl.id, pl.ts, pl.data, out);
        } else if (pl.iface == "can2") {
            stage4::decode_frame_and_write(net2, pl.id, pl.ts, pl.data, out);
        }
    }

    std::cout << "Stage 4: Decoded to output_stage4.txt\n";
    return 0;
}
