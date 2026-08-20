#include "cdr_utils.hpp"
#include <gtest/gtest.h>
#include <cstdint>
#include <cstring>
#include <geometry_msgs/msg/point.hpp>
#include <nlohmann/json.hpp>
#include <rclcpp/rclcpp.hpp>
#include <rclcpp/serialization.hpp>
#include <rclcpp/serialized_message.hpp>
#include <rosidl_typesupport_introspection_cpp/message_introspection.hpp>
#include <rosidl_typesupport_introspection_cpp/message_type_support_decl.hpp>
#include <std_msgs/msg/int32_multi_array.hpp>
#include <std_msgs/msg/string.hpp>

namespace lazyrtui {

// Typed parameterized coverage over every integer scalar width the CDR parser
// reads (unsigned and signed).
template <typename T> class CdrByteSwapTest : public ::testing::Test {};
using ByteSwapTypes = ::testing::Types<uint8_t, uint16_t, uint32_t, uint64_t,
                                       int16_t, int32_t, int64_t>;
TYPED_TEST_SUITE(CdrByteSwapTest, ByteSwapTypes);

TYPED_TEST(CdrByteSwapTest, ReversesByteOrder) {
  TypeParam value = static_cast<TypeParam>(0);
  for (unsigned i = 0; i < sizeof(TypeParam); ++i) {
    const uint64_t shifted = static_cast<uint64_t>(value) << 8;
    value = static_cast<TypeParam>(shifted | (i + 1));
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
  TypeParam ones = static_cast<TypeParam>(~TypeParam{0});
  EXPECT_EQ(cdr_byte_swap<TypeParam>(cdr_byte_swap<TypeParam>(ones)), ones);
  EXPECT_EQ(cdr_byte_swap<TypeParam>(ones), ones);

  TypeParam zero = static_cast<TypeParam>(0);
  EXPECT_EQ(cdr_byte_swap<TypeParam>(cdr_byte_swap<TypeParam>(zero)), zero);
  EXPECT_EQ(cdr_byte_swap<TypeParam>(zero), zero);

  TypeParam scramble = static_cast<TypeParam>(
      0x12345678ULL &
      (sizeof(TypeParam) == 8 ? ~0ULL : (1ULL << (8 * sizeof(TypeParam))) - 1));
  EXPECT_EQ(cdr_byte_swap<TypeParam>(cdr_byte_swap<TypeParam>(scramble)),
            scramble);
}

TEST(CdrUtilsTest, FloatAndDoubleByteSwapInvolution) {
  const float f_val = 123.456f;
  const float f_swapped = cdr_byte_swap(f_val);
  EXPECT_NE(f_val, f_swapped);
  EXPECT_EQ(cdr_byte_swap(f_swapped), f_val);

  const double d_val = 9876.54321;
  const double d_swapped = cdr_byte_swap(d_val);
  EXPECT_NE(d_val, d_swapped);
  EXPECT_EQ(cdr_byte_swap(d_swapped), d_val);
}

TEST(CdrUtilsTest, HostEndiannessProbeMatchesMemcpyProbe) {
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
  const uint16_t pattern = 0x0102;
  const bool pattern_differs_from_swap = (pattern != cdr_byte_swap(pattern));
  EXPECT_EQ(pattern_differs_from_swap, g_host_is_little_endian);
  EXPECT_EQ(cdr_byte_swap(cdr_byte_swap<uint16_t>(0x0102)), 0x0102u);
  EXPECT_EQ(cdr_byte_swap(cdr_byte_swap<uint32_t>(0x01020304)), 0x01020304u);
}

TEST(CdrUtilsTest, DetectsCdrStreamEndianness) {
  const uint8_t le_hdr1[4] = {0x00, 0x01, 0x00, 0x00};
  EXPECT_TRUE(is_cdr_stream_little_endian(le_hdr1, sizeof(le_hdr1)));

  const uint8_t le_hdr2[4] = {0x00, 0x03, 0x00, 0x00};
  EXPECT_TRUE(is_cdr_stream_little_endian(le_hdr2, sizeof(le_hdr2)));

  const uint8_t le_hdr3[4] = {0x00, 0x07, 0x00, 0x00};
  EXPECT_TRUE(is_cdr_stream_little_endian(le_hdr3, sizeof(le_hdr3)));

  const uint8_t le_hdr4[4] = {0x00, 0x0b, 0x00, 0x00};
  EXPECT_TRUE(is_cdr_stream_little_endian(le_hdr4, sizeof(le_hdr4)));

  const uint8_t be_hdr1[4] = {0x00, 0x00, 0x00, 0x00};
  EXPECT_FALSE(is_cdr_stream_little_endian(be_hdr1, sizeof(be_hdr1)));

  const uint8_t be_hdr2[4] = {0x00, 0x02, 0x00, 0x00};
  EXPECT_FALSE(is_cdr_stream_little_endian(be_hdr2, sizeof(be_hdr2)));

  const uint8_t be_hdr3[4] = {0x00, 0x06, 0x00, 0x00};
  EXPECT_FALSE(is_cdr_stream_little_endian(be_hdr3, sizeof(be_hdr3)));
}

TEST(CdrUtilsTest, ShouldSwapCdrBytesMatchesHost) {
  const uint8_t le_hdr[4] = {0x00, 0x01, 0x00, 0x00};
  if (g_host_is_little_endian) {
    EXPECT_FALSE(should_swap_cdr_bytes(le_hdr, sizeof(le_hdr)));
  } else {
    EXPECT_TRUE(should_swap_cdr_bytes(le_hdr, sizeof(le_hdr)));
  }

  const uint8_t be_hdr[4] = {0x00, 0x00, 0x00, 0x00};
  if (g_host_is_little_endian) {
    EXPECT_TRUE(should_swap_cdr_bytes(be_hdr, sizeof(be_hdr)));
  } else {
    EXPECT_FALSE(should_swap_cdr_bytes(be_hdr, sizeof(be_hdr)));
  }
}

template <typename T>
static const rosidl_typesupport_introspection_cpp::MessageMembers*
get_test_message_members() {
  const auto* ts =
      rosidl_typesupport_introspection_cpp::get_message_type_support_handle<T>();
  return static_cast<const rosidl_typesupport_introspection_cpp::MessageMembers*>(
      ts->data);
}

template <typename T>
static std::vector<uint8_t> serialize_test_msg(const T& msg) {
  rclcpp::Serialization<T> serializer;
  rclcpp::SerializedMessage serialized_msg;
  serializer.serialize_message(&msg, &serialized_msg);
  const auto& rcl_msg = serialized_msg.get_rcl_serialized_message();
  return std::vector<uint8_t>(rcl_msg.buffer,
                              rcl_msg.buffer + rcl_msg.buffer_length);
}

TEST(CdrUtilsTest, StringParserLeAndBeRoundtrip) {
  std_msgs::msg::String msg;
  msg.data = "Hello LazyRTUI CDR Test";

  const auto* members = get_test_message_members<std_msgs::msg::String>();
  ASSERT_NE(members, nullptr);

  std::vector<uint8_t> le_bytes = serialize_test_msg(msg);
  ASSERT_GE(le_bytes.size(), 8u);

  std::string json_le =
      parse_cdr_to_json(members, le_bytes.data(), le_bytes.size());
  auto parsed_le = nlohmann::json::parse(json_le);
  EXPECT_EQ(parsed_le["data"], "Hello LazyRTUI CDR Test");

  // Create big-endian CDR buffer: flip encapsulation header byte 1 and length
  std::vector<uint8_t> be_bytes = le_bytes;
  be_bytes[1] = 0x00; // Big-endian CDR header
  uint32_t str_len = 0;
  std::memcpy(&str_len, &be_bytes[4], 4);
  str_len = cdr_byte_swap(str_len);
  std::memcpy(&be_bytes[4], &str_len, 4);

  std::string json_be =
      parse_cdr_to_json(members, be_bytes.data(), be_bytes.size());
  auto parsed_be = nlohmann::json::parse(json_be);
  EXPECT_EQ(parsed_be["data"], "Hello LazyRTUI CDR Test");
  EXPECT_EQ(parsed_le, parsed_be);
}

TEST(CdrUtilsTest, PointParserLeAndBeRoundtrip) {
  geometry_msgs::msg::Point msg;
  msg.x = 1.25;
  msg.y = -3.5;
  msg.z = 42.0;

  const auto* members = get_test_message_members<geometry_msgs::msg::Point>();
  ASSERT_NE(members, nullptr);

  std::vector<uint8_t> le_bytes = serialize_test_msg(msg);
  ASSERT_GE(le_bytes.size(), 28u);

  std::string json_le =
      parse_cdr_to_json(members, le_bytes.data(), le_bytes.size());
  auto parsed_le = nlohmann::json::parse(json_le);
  EXPECT_DOUBLE_EQ(parsed_le["x"].get<double>(), 1.25);
  EXPECT_DOUBLE_EQ(parsed_le["y"].get<double>(), -3.5);
  EXPECT_DOUBLE_EQ(parsed_le["z"].get<double>(), 42.0);

  // Big-endian buffer: flip header byte 1 and swap three doubles (8 bytes each)
  std::vector<uint8_t> be_bytes = le_bytes;
  be_bytes[1] = 0x00;
  for (size_t offset = 4; offset + 8 <= be_bytes.size(); offset += 8) {
    double val = 0.0;
    std::memcpy(&val, &be_bytes[offset], 8);
    val = cdr_byte_swap(val);
    std::memcpy(&be_bytes[offset], &val, 8);
  }

  std::string json_be =
      parse_cdr_to_json(members, be_bytes.data(), be_bytes.size());
  auto parsed_be = nlohmann::json::parse(json_be);
  EXPECT_DOUBLE_EQ(parsed_be["x"].get<double>(), 1.25);
  EXPECT_DOUBLE_EQ(parsed_be["y"].get<double>(), -3.5);
  EXPECT_DOUBLE_EQ(parsed_be["z"].get<double>(), 42.0);
  EXPECT_EQ(parsed_le, parsed_be);
}

TEST(CdrUtilsTest, Int32MultiArrayFullParserLeAndBeRoundtrip) {
  std_msgs::msg::Int32MultiArray msg;
  msg.layout.dim.resize(2);
  msg.layout.dim[0].label = "rows";
  msg.layout.dim[0].size = 2;
  msg.layout.dim[0].stride = 6;
  msg.layout.dim[1].label = "cols";
  msg.layout.dim[1].size = 3;
  msg.layout.dim[1].stride = 3;
  msg.layout.data_offset = 0;
  msg.data = {10, 20, 30, 40, 50, 60};

  const auto* members =
      get_test_message_members<std_msgs::msg::Int32MultiArray>();
  ASSERT_NE(members, nullptr);

  std::vector<uint8_t> le_bytes = serialize_test_msg(msg);

  std::string json_le =
      parse_cdr_to_json(members, le_bytes.data(), le_bytes.size());
  auto parsed_le = nlohmann::json::parse(json_le);

  EXPECT_EQ(parsed_le["layout"]["dim"].size(), 2u);
  EXPECT_EQ(parsed_le["layout"]["dim"][0]["label"], "rows");
  EXPECT_EQ(parsed_le["layout"]["dim"][0]["size"], 2);
  EXPECT_EQ(parsed_le["layout"]["dim"][0]["stride"], 6);
  EXPECT_EQ(parsed_le["layout"]["dim"][1]["label"], "cols");
  EXPECT_EQ(parsed_le["layout"]["dim"][1]["size"], 3);
  EXPECT_EQ(parsed_le["layout"]["dim"][1]["stride"], 3);
  EXPECT_EQ(parsed_le["layout"]["data_offset"], 0);
  EXPECT_EQ(parsed_le["data"], std::vector<int>({10, 20, 30, 40, 50, 60}));

  // Build corresponding big-endian buffer by swapping each wire field
  std::vector<uint8_t> be_bytes = le_bytes;
  be_bytes[1] = 0x00; // Big-endian scheme

  auto swap_u32_at = [&](size_t& off) {
    uint32_t val = 0;
    std::memcpy(&val, &be_bytes[off], 4);
    val = cdr_byte_swap(val);
    std::memcpy(&be_bytes[off], &val, 4);
    off += 4;
    return cdr_byte_swap(val); // return host-endian original value
  };

  size_t offset = 4;
  uint32_t dim_count = swap_u32_at(offset);
  for (uint32_t i = 0; i < dim_count; ++i) {
    uint32_t label_len = swap_u32_at(offset);
    offset += label_len;
    while ((offset - 4) % 4 != 0) offset++;
    swap_u32_at(offset); // size
    swap_u32_at(offset); // stride
  }
  swap_u32_at(offset); // data_offset

  while ((offset - 4) % 4 != 0) offset++;
  uint32_t data_count = swap_u32_at(offset);
  for (uint32_t i = 0; i < data_count; ++i) {
    swap_u32_at(offset); // data[i]
  }

  std::string json_be =
      parse_cdr_to_json(members, be_bytes.data(), be_bytes.size());
  auto parsed_be = nlohmann::json::parse(json_be);

  EXPECT_EQ(parsed_be, parsed_le);
}

TEST(CdrUtilsTest, LargeSequenceTruncation) {
  std_msgs::msg::Int32MultiArray msg;
  msg.data.resize(30);
  for (int i = 0; i < 30; ++i) {
    msg.data[i] = i * 10;
  }

  const auto* members =
      get_test_message_members<std_msgs::msg::Int32MultiArray>();
  std::vector<uint8_t> bytes = serialize_test_msg(msg);
  std::string json_str =
      parse_cdr_to_json(members, bytes.data(), bytes.size());

  EXPECT_NE(json_str.find("... (30 items total)"), std::string::npos);
}

} // namespace lazyrtui
