#include "cdr_utils.hpp"
#include <gtest/gtest.h>
#include <cstdint>

namespace lazyrtui {

TEST(CdrUtilsTest, ByteSwap16) {
  EXPECT_EQ(cdr_byte_swap<uint16_t>(0x0102), 0x0201u);
}

TEST(CdrUtilsTest, ByteSwap32) {
  EXPECT_EQ(cdr_byte_swap<uint32_t>(0x01020304), 0x04030201u);
}

TEST(CdrUtilsTest, ByteSwap64) {
  EXPECT_EQ(cdr_byte_swap<uint64_t>(0x0102030405060708ULL), 0x0807060504030201ULL);
}

TEST(CdrUtilsTest, HostEndiannessProbeIsConsistent) {
  // The probe must agree with cdr_byte_swap: on a little-endian host the byte
  // pattern 0x0102 reads as 0x0201 (differs from its own swap), on a
  // big-endian host it reads as-is.
  const uint16_t pattern = 0x0102;
  const bool pattern_differs_from_swap = (pattern != cdr_byte_swap(pattern));
  EXPECT_EQ(pattern_differs_from_swap, g_host_is_little_endian);
  // Byte-swapping is an involution on every width.
  EXPECT_EQ(cdr_byte_swap(cdr_byte_swap<uint16_t>(0x0102)), 0x0102u);
  EXPECT_EQ(cdr_byte_swap(cdr_byte_swap<uint32_t>(0x01020304)), 0x01020304u);
}

}  // namespace lazyrtui
