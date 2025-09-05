#pragma once
#include <cstdint>
#include <string>
#include <unordered_map>
#include <vector>
#include <ostream>

namespace stage4 {

// ---------- Data model ----------
struct Signal {
    std::string name;
    uint16_t start_bit = 0;   // DBC bit index
    uint16_t bit_len = 0;
    bool little_endian = true; // @1 = little, @0 = big (Motorola)
    bool is_signed = false;    // '+' unsigned, '-' signed
    double scale = 1.0;
    double offset = 0.0;
};

struct Message {
    uint32_t id = 0;          // decimal in DBC
    std::string name;
    uint8_t dlc = 8;
    std::vector<Signal> signals;
};

struct Network {
    std::unordered_map<uint32_t, Message> msgs; // id -> message
};

// ---------- DBC parsing ----------
bool parse_dbc_file(const std::string& path, Network& out, std::string* err = nullptr);

// ---------- Decoding ----------
double decode_signal_phys(const Signal& sig, const std::vector<uint8_t>& data);
size_t decode_frame_and_write(const Network& net,
                              uint32_t can_id,
                              double timestamp,
                              const std::vector<uint8_t>& data,
                              std::ostream& os);

} // namespace stage4
