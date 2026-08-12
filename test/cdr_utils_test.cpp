#include "cdr_utils.hpp"
#include <gtest/gtest.h>
#include <cstdint>
#include <cstring>
#include <type_traits>

namespace lazyrtui {

// Typed parameterized coverage over every scalar width the CDR parser reads.
template <typename T> class CdrByteSwapTest : public ::testing::Test {};
using ByteSwapTypes = ::testing::Types<uint8_t, uint16_t, uint32_t, uint64_t>;
TYPED_TEST_SUITE(CdrByteSwapTest, ByteSwapTypes);

TYPED_TEST(CdrByteSwapTest, ReversesByteOrder) {
  // Build a value whose bytes are 0x01 0x02 ... 0xNN, then compare the swap
  // against an independent shift-based reference (not against swap-of-swap).
  TypeParam value = static_cast<TypeParam>(0);
  for (unsigned i = 0; i < sizeof(TypeParam); ++i) {
    value = static_cast<TypeParam>((static_cast<uint64_t>(value) << 8) | (i + 1));
  }
  TypeParam expected = static_cast<TypeParam>(0);
  for (unsigned i = 0; i < sizeof(TypeParam); ++i) {
    expected = static_cast<TypeParam>(
        (static_cast<uint64_t>(expected) << 8) |
        ((static_cast<uint64_t>(value) >> (8 * i)) & 0xFF));
  }
  EXPECT_EQ(cdr_byte_swap<TypeParam>(value), expected);
}

TYPED_TEST(CdrByteSwapTest, IsInvolution) {
  // Swapping twice restores the original for representative bit patterns:
  // all-ones, zero, and a multi-bit scramble.
  TypeParam ones = static_cast<TypeParam>(~TypeParam{0});
  EXPECT_EQ(cdr_byte_swap<TypeParam>(cdr_byte_swap<TypeParam>(ones)), ones);

  TypeParam zero = static_cast<TypeParam>(0);
  EXPECT_EQ(cdr_byte_swap<TypeParam>(cdr_byte_swap<TypeParam>(zero)), zero);

  TypeParam scramble = static_cast<TypeParam>(
      0x12345678ULL & (sizeof(TypeParam) == 8 ? ~0ULL
                                              : (1ULL << (8 * sizeof(TypeParam))) - 1));
  EXPECT_EQ(cdr_byte_swap<TypeParam>(cdr_byte_swap<TypeParam>(scramble)),
            scramble);
}

TYPED_TEST(CdrByteSwapTest, ZeroAndAllOnesAreSymmetric) {
  EXPECT_EQ(cdr_byte_swap<TypeParam>(static_cast<TypeParam>(0)),
            static_cast<TypeParam>(0));
  TypeParam ones = static_cast<TypeParam>(~TypeParam{0});
  EXPECT_EQ(cdr_byte_swap<TypeParam>(ones), ones);
}

TEST(CdrUtilsTest, HostEndiannessProbeMatchesMemcpyProbe) {
  // Independent probe via memcpy (does not rely on cdr_byte_swap itself):
  // on a little-endian host bytes[0] == 0x02, on big-endian bytes[0] == 0x01.
  const uint16_t pattern = 0x0102;
  uint8_t bytes[2];
  std::memcpy(bytes, &pattern, sizeof(bytes));
  const bool memcpy_little_endian = (bytes[0] == 0x02);
  EXPECT_EQ(g_host_is_little_endian, memcpy_little_endian);
}

TEST(CdrUtilsTest, ByteSwap16) {
  EXPECT_EQ(cdr_byte_swap<uint16_t>(0x0102), 0x0201u);
}

TEST(CdrUtilsTest, ByteSwap32) {
  EXPECT_EQ(cdr_byte_swap<uint32_t>(0x01020304), 0x04030201u);
}

TEST(CdrUtilsTest, ByteSwap64) {
  EXPECT_EQ(cdr_byte_swap<uint64_t>(0x0102030405060708ULL),
            0x0807060504030201ULL);
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
