#include "dbc_simple.hpp"
#include <regex>
#include <fstream>
#include <sstream>
#include <cstring>
#include <limits>
#include <iomanip>
#include <iostream>

namespace stage4 {

// ------------------ helpers ------------------
static inline uint64_t mask_nbits(unsigned n) {
    if (n == 64) return ~0ULL;
    return (1ULL << n) - 1ULL;
}

// Extract raw unsigned value for little-endian (@1 / Intel) signals.
// DBC start bit is LSB position counting upward across bytes.
static uint64_t extract_le(const std::vector<uint8_t>& data, uint16_t start, uint16_t length) {
    uint64_t result = 0;
    for (unsigned k = 0; k < length; ++k) {
        unsigned bit_index = start + k;
        unsigned byte = bit_index / 8;
        unsigned bit  = bit_index % 8;
        if (byte < data.size()) {
            uint8_t b = (data[byte] >> bit) & 0x1;
            result |= (uint64_t(b) << k);
        }
    }
    return result;
}

// Extract raw unsigned value for big-endian (@0 / Motorola) signals.
// DBC start bit refers to the *MSB* of the signal at (byte = s/8, bit = 7 - (s%8)),
// subsequent bits proceed toward less significant bits; when bit < 0, move to next byte (+1) and bit=7.
static uint64_t extract_be(const std::vector<uint8_t>& data, uint16_t start, uint16_t length) {
    uint64_t result = 0;
    int byte = static_cast<int>(start / 8);
    int bit  = 7 - static_cast<int>(start % 8);

    for (unsigned i = 0; i < length; ++i) {
        uint8_t v = 0;
        if (byte >= 0 && static_cast<size_t>(byte) < data.size() && bit >= 0 && bit <= 7) {
            v = (data[byte] >> bit) & 0x1;
        }
        // MSB-first building
        result = (result << 1) | v;
        // move to next lower bit; wrap to next byte when needed
        --bit;
        if (bit < 0) {
            --byte;
            bit = 7;
        }
    }
    return result;
}

// ------------------ parser ------------------
static inline std::string trim(const std::string& s) {
    size_t b = s.find_first_not_of(" \t\r\n");
    if (b == std::string::npos) return {};
    size_t e = s.find_last_not_of(" \t\r\n");
    return s.substr(b, e - b + 1);
}
static inline bool starts_with(const std::string& s, const char* pfx) {
    return s.rfind(pfx, 0) == 0;
}

bool parse_dbc_file(const std::string& path, Network& out, std::string* err) {
    std::ifstream is(path);
    if (!is) {
        if (err) *err = "Failed to open " + path;
        return false;
    }

    Message* current = nullptr;
    std::string line;

    while (std::getline(is, line)) {
        line = trim(line);
        if (line.empty()) continue;

        // -------- BO_ line --------
        if (starts_with(line, "BO_")) {
            // Format: BO_ <id> <name>: <dlc> <tx> ...
            // We will tokenize enough to get id, name(with :), dlc
            std::istringstream ls(line);
            std::string tag, id_str, name_colon, dlc_str;
            ls >> tag;                // "BO_"
            ls >> id_str;             // e.g. "1697" or "0x705"
            ls >> name_colon;         // e.g. "Msg:" (has ':')
            // If colon not stuck to name (rare), read next and stitch until we see ':'
            while (!name_colon.empty() && name_colon.back() != ':' && ls.good()) {
                std::string extra;
                ls >> extra;
                name_colon += extra;  // stitch (handles odd spacing)
            }
            ls >> dlc_str;            // "8"

            if (id_str.empty() || name_colon.empty() || dlc_str.empty()) {
                current = nullptr;
                continue; // malformed; skip
            }

            // strip trailing ':'
            if (name_colon.back() == ':') name_colon.pop_back();

            Message msg;
            // base 0 -> auto-detect 0x... hex or decimal
            msg.id  = static_cast<uint32_t>(std::stoul(id_str, nullptr, 0));
            msg.name= name_colon;
            try {
                msg.dlc = static_cast<uint8_t>(std::stoul(dlc_str, nullptr, 10));
            } catch (...) {
                msg.dlc = 8; // default if weird
            }

            out.msgs[msg.id] = std::move(msg);
            current = &out.msgs[msg.id];
            continue;
        }

        // -------- SG_ line --------
        if (starts_with(line, "SG_")) {
            if (!current) continue; // ignore SG_ before BO_

            // We split into left (before ':') and right (after ':')
            auto colon_pos = line.find(':');
            if (colon_pos == std::string::npos) continue;

            std::string left  = trim(line.substr(0, colon_pos));   // "SG_ Name" or "SG_ Name mX"
            std::string right = trim(line.substr(colon_pos + 1));  // "start|len@endian sign (scale,offset) ..."

            // Left tokens: "SG_", <name> [mXX]...
            std::istringstream ll(left);
            std::string tag, name, t2;
            ll >> tag;       // "SG_"
            ll >> name;      // signal name (no spaces)
            // If next token starts with 'm' and is followed by digits, it's multiplex marker -> ignore
            if (ll >> t2) {
                if (!(t2.size() >= 2 && (t2[0] == 'm' || t2[0] == 'M') && std::all_of(t2.begin() + 1, t2.end(), ::isdigit))) {
                    // not a mux token -> put it back logically (we don't need it anyway)
                }
            }

            // Right side begins with "<start>|<len>@<endian><sign> ..."
            // Extract the very first "bit spec" token up to first space
            std::string bitspec;
            {
                std::istringstream rr(right);
                rr >> bitspec;
            }
            // bitspec like:  "48|16@1-"  or "0|12@0+"
            // Parse fields from bitspec
            auto pipe_pos = bitspec.find('|');
            auto at_pos   = bitspec.find('@');
            if (pipe_pos == std::string::npos || at_pos == std::string::npos || at_pos + 2 > bitspec.size())
                continue;

            std::string start_str = bitspec.substr(0, pipe_pos);
            std::string len_str   = bitspec.substr(pipe_pos + 1, at_pos - (pipe_pos + 1));
            char endian_ch        = bitspec[at_pos + 1];
            char sign_ch          = bitspec[at_pos + 2];

            // Now find "(scale,offset)" in the right side
            double scale = 1.0, offset = 0.0;
            auto lpar = right.find('(');
            auto rpar = right.find(')', lpar == std::string::npos ? 0 : lpar + 1);
            if (lpar != std::string::npos && rpar != std::string::npos && rpar > lpar + 1) {
                std::string so = right.substr(lpar + 1, rpar - lpar - 1); // "s,o"
                auto comma = so.find(',');
                if (comma != std::string::npos) {
                    try {
                        scale  = std::stod(trim(so.substr(0, comma)));
                        offset = std::stod(trim(so.substr(comma + 1)));
                    } catch (...) {
                        scale = 1.0; offset = 0.0;
                    }
                }
            }

            // Fallback regex in case parsing above fails
            if (scale == 1.0 && offset == 0.0) {
                std::smatch sm;
                if (std::regex_search(right, sm,
                    std::regex(R"(\(\s*([-+]?[\d\.eE]+)\s*,\s*([-+]?[\d\.eE]+)\s*\))"))) {
                    try {
                        scale  = std::stod(sm[1]);
                        offset = std::stod(sm[2]);
                    } catch (...) {
                        // leave defaults
                    }
                }
            }


            // Construct Signal
            Signal s;
            s.name          = name;
            try {
                s.start_bit = static_cast<uint16_t>(std::stoul(trim(start_str)));
                s.bit_len   = static_cast<uint16_t>(std::stoul(trim(len_str)));
            } catch (...) {
                continue; // malformed
            }
            s.little_endian = (endian_ch == '1');
            s.is_signed     = (sign_ch == '-');
            s.scale         = scale;
            s.offset        = offset;

            std::cerr << "Parsed signal: " << s.name
                        << " start=" << s.start_bit
                        << " len=" << s.bit_len
                        << " endian=" << (s.little_endian ? "LE" : "BE")
                        << " signed=" << s.is_signed
                        << " scale=" << s.scale
                        << " offset=" << s.offset << "\n";


            current->signals.push_back(std::move(s));
            continue;
        }

        // Ignore all other lines (NS_, BS_, BU_, VAL_, BO_TX_BU_, comments, etc.)
    }

    if (err) *err = "";
    return true;
}


// ------------------ decoding ------------------
double decode_signal_phys(const Signal& sig, const std::vector<uint8_t>& data) {
    uint64_t raw_u = 0;
    if (sig.little_endian) {
        raw_u = extract_le(data, sig.start_bit, sig.bit_len);
    } else {
        raw_u = extract_be(data, sig.start_bit, sig.bit_len);
    }
    raw_u &= mask_nbits(sig.bit_len);

    // sign handling
    int64_t raw_s = 0;
    if (sig.is_signed) {
        const uint64_t sign_bit = 1ULL << (sig.bit_len - 1);
        if (raw_u & sign_bit) {
            // sign-extend
            uint64_t full = raw_u | (~mask_nbits(sig.bit_len));
            raw_s = static_cast<int64_t>(full);
        } else {
            raw_s = static_cast<int64_t>(raw_u);
        }
        return static_cast<double>(raw_s) * sig.scale + sig.offset;
    } else {
        return static_cast<double>(raw_u) * sig.scale + sig.offset;
    }
}

size_t decode_frame_and_write(const Network& net,
                              uint32_t can_id,
                              double timestamp,
                              const std::vector<uint8_t>& data,
                              std::ostream& os)
{
    auto it = net.msgs.find(can_id);
    if (it == net.msgs.end()) return 0;

    const Message& msg = it->second;
    size_t count = 0;
    for (const auto& sig : msg.signals) {
        const double phys = decode_signal_phys(sig, data);
        os << '(' << std::setprecision(15) << timestamp << "): "
           << sig.name << ": " << phys << "\n";
        ++count;
    }
    return count;
}

} // namespace stage4
