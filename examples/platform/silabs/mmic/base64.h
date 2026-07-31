#pragma once

#include <stddef.h>
#include <stdint.h>
#include <vector>

// Decode a standard base64 string (with or without padding). Whitespace and
// newlines are ignored. Returns true on success and fills `out` with the
// decoded bytes. On failure `out` is left in an unspecified state and false
// is returned.
bool base64Decode(const char * input, size_t inputLen, std::vector<uint8_t> & out);
