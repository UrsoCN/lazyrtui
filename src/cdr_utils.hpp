#pragma once

#include <algorithm>
#include <cstdint>
#include <cstring>

namespace lazyrtui {

// CDR stream endianness vs host: the 4-byte CDR encapsulation header's first
// byte is 0x01 (CDR_LE) or 0x00 (CDR_BE). Multi-byte reads must be byte-swapped
// when the stream order differs from the host.
inline const bool g_host_is_little_endian = []() {
  const uint16_t probe = 0x0102;
  uint8_t bytes[2];
  std::memcpy(bytes, &probe, sizeof(bytes));
  return bytes[0] == 0x02;
}();

template <typename T>
inline T cdr_byte_swap(T value) {
    uint8_t* bytes = reinterpret_cast<uint8_t*>(&value);
    std::reverse(bytes, bytes + sizeof(T));
    return value;
}

} // namespace lazyrtui
