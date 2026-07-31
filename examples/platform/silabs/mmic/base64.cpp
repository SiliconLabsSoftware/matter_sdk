#include "base64.h"

#include <cctype>

namespace {

// -1 = invalid, -2 = whitespace/skip, -3 = padding '='
static int8_t decodeChar(char c)
{
    if (c >= 'A' && c <= 'Z') return static_cast<int8_t>(c - 'A');
    if (c >= 'a' && c <= 'z') return static_cast<int8_t>(c - 'a' + 26);
    if (c >= '0' && c <= '9') return static_cast<int8_t>(c - '0' + 52);
    if (c == '+') return 62;
    if (c == '/') return 63;
    if (c == '=') return -3;
    if (c == ' ' || c == '\t' || c == '\r' || c == '\n') return -2;
    return -1;
}

} // namespace

bool base64Decode(const char * input, size_t inputLen, std::vector<uint8_t> & out)
{
    if (input == nullptr)
    {
        return false;
    }

    out.clear();
    out.reserve((inputLen * 3) / 4);

    uint32_t accum = 0;
    int      bits  = 0;
    bool     seenPad = false;

    for (size_t i = 0; i < inputLen; ++i)
    {
        int8_t v = decodeChar(input[i]);
        if (v == -2)
        {
            continue;
        }
        if (v == -3)
        {
            seenPad = true;
            continue;
        }
        if (v < 0 || seenPad)
        {
            return false;
        }
        accum = (accum << 6) | static_cast<uint32_t>(v);
        bits += 6;
        if (bits >= 8)
        {
            bits -= 8;
            out.push_back(static_cast<uint8_t>((accum >> bits) & 0xFF));
        }
    }
    return true;
}
