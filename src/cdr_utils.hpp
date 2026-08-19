#pragma once

#include <algorithm>
#include <cstdint>
#include <cstring>

namespace lazyrtui {

// CDR stream endianness vs host: In OMG CDR / RTPS encapsulation header
// (2-byte scheme ID in network byte order: byte 0 is 0x00, byte 1 is kind):
// 0x0001 = CDR_LE, 0x0003 = PL_CDR_LE, 0x0007 = CDR2_LE, 0x000b = D_CDR2_LE
// 0x0000 = CDR_BE, 0x0002 = PL_CDR_BE, 0x0006 = CDR2_BE, 0x000a = D_CDR2_BE
// Multi-byte reads must be byte-swapped when stream order differs from the host.
inline const bool g_host_is_little_endian = []() {
  const uint16_t probe = 0x0102;
  uint8_t bytes[2];
  std::memcpy(bytes, &probe, sizeof(bytes));
  return bytes[0] == 0x02;
}();

inline bool is_cdr_stream_little_endian(const uint8_t* buffer, size_t size) {
  if (!buffer || size < 2) return true;
  return (buffer[1] == 0x01 || buffer[1] == 0x03 || buffer[1] == 0x07 ||
          buffer[1] == 0x0b || buffer[0] == 0x01);
}

inline bool should_swap_cdr_bytes(const uint8_t* buffer, size_t size) {
  return is_cdr_stream_little_endian(buffer, size) != g_host_is_little_endian;
}

template <typename T>
inline T cdr_byte_swap(T value) {
    uint8_t* bytes = reinterpret_cast<uint8_t*>(&value);
    std::reverse(bytes, bytes + sizeof(T));
    return value;
}

} // namespace lazyrtui
