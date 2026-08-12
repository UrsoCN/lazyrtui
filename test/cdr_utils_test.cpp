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

TEST(CdrUtilsTest, HostEndiannessProbeIsBoolean) {
  EXPECT_TRUE(g_host_is_little_endian == true || g_host_is_little_endian == false);
}

}  // namespace lazyrtui
