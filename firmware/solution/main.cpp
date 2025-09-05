#include "src/can_decode.hpp"
#include <fstream>
#include <iostream>

int main() {
    // Paths relative to repo root (/workspace at runtime)
    const std::string dbc_control  = "dbc-files/ControlBus.dbc";   // can0/vcan0
    const std::string dbc_sensor   = "dbc-files/SensorBus.dbc";    // can1/vcan1
    const std::string dbc_tractive = "dbc-files/TractiveBus.dbc";  // can2/vcan2

    auto net_can0 = rbk::load_network(dbc_control);
    auto net_can1 = rbk::load_network(dbc_sensor);
    auto net_can2 = rbk::load_network(dbc_tractive);

    if (!net_can0 || !net_can1 || !net_can2) {
        std::cerr << "One or more DBCs failed to load. Exiting.\n";
        return 1;
    }

    auto map_can0 = rbk::build_msg_map(*net_can0);
    auto map_can1 = rbk::build_msg_map(*net_can1);
    auto map_can2 = rbk::build_msg_map(*net_can2);

    std::ifstream dump("dump.log");
    if (!dump) {
        std::cerr << "Could not open dump.log\n";
        return 1;
    }
    std::ofstream out("output.txt");
    if (!out) {
        std::cerr << "Could not create output.txt\n";
        return 1;
    }
    out.setf(std::ios::fmtflags(0), std::ios::floatfield);
    out << std::setprecision(15);

    std::string line;
    rbk::ParsedLine pl;
    while (std::getline(dump, line)) {
        if (!rbk::parse_line(line, pl)) continue;

        if (pl.iface == "can0") {
            rbk::decode_and_write(pl, *net_can0, map_can0, out);
        } else if (pl.iface == "can1") {
            rbk::decode_and_write(pl, *net_can1, map_can1, out);
        } else if (pl.iface == "can2") {
            rbk::decode_and_write(pl, *net_can2, map_can2, out);
        }
    }

    std::cout << "Decoded to output.txt\n";
    return 0;
}
