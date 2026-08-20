#pragma once

#include <algorithm>
#include <cstdint>
#include <cstring>
#include <nlohmann/json.hpp>
#include <rosidl_typesupport_introspection_cpp/field_types.hpp>
#include <rosidl_typesupport_introspection_cpp/message_introspection.hpp>
#include <sstream>
#include <string>

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
  static_assert(sizeof(T) == 1 || sizeof(T) == 2 || sizeof(T) == 4 ||
                    sizeof(T) == 8,
                "cdr_byte_swap only supports 1, 2, 4, 8 byte types");
  alignas(T) uint8_t bytes[sizeof(T)];
  std::memcpy(bytes, &value, sizeof(T));
  std::reverse(bytes, bytes + sizeof(T));
  T result;
  std::memcpy(&result, bytes, sizeof(T));
  return result;
}

inline bool parse_cdr_field(
    const ::rosidl_typesupport_introspection_cpp::MessageMember& member,
    const uint8_t* buffer, size_t size, size_t& offset, std::stringstream& ss,
    int indent_level, bool swap_bytes);

inline bool parse_cdr_members(
    const ::rosidl_typesupport_introspection_cpp::MessageMembers* members,
    const uint8_t* buffer, size_t size, size_t& offset, std::stringstream& ss,
    int indent_level, bool swap_bytes) {
  if (!members) return false;
  std::string indent(indent_level * 2, ' ');
  ss << "{\n";
  for (uint32_t i = 0; i < members->member_count_; ++i) {
    const auto& member = members->members_[i];
    if (i > 0) ss << ",\n";
    ss << indent << "  \"" << member.name_ << "\": ";
    if (!parse_cdr_field(member, buffer, size, offset, ss, indent_level + 1,
                         swap_bytes)) {
      ss << "null";
    }
  }
  ss << "\n" << indent << "}";
  return true;
}

inline bool parse_cdr_field(
    const ::rosidl_typesupport_introspection_cpp::MessageMember& member,
    const uint8_t* buffer, size_t size, size_t& offset, std::stringstream& ss,
    int indent_level, bool swap_bytes) {
  using namespace rosidl_typesupport_introspection_cpp;

  auto align_offset = [&](size_t align) {
    if (align == 0) return;
    while ((offset - 4) % align != 0 && offset < size) offset++;
  };

  if (member.is_array_) {
    align_offset(4);
    uint32_t count = member.array_size_;
    // Read the actual element count from the wire for every non-fixed
    // array (unbounded AND bounded sequences); only fixed arrays have no
    // count on the wire.
    if (member.is_upper_bound_ || count == 0) {
      if (offset + 4 > size) return false;
      std::memcpy(&count, buffer + offset, 4);
      if (swap_bytes) count = cdr_byte_swap(count);
      offset += 4;
    }

    ss << "[";
    uint32_t display_count = std::min(count, 20u);
    for (uint32_t j = 0; j < count; ++j) {
      if (j > 0 && j < display_count) ss << ", ";
      ::rosidl_typesupport_introspection_cpp::MessageMember elem_member = member;
      elem_member.is_array_ = false;
      elem_member.array_size_ = 0;
      if (j < display_count) {
        if (!parse_cdr_field(elem_member, buffer, size, offset, ss, indent_level,
                             swap_bytes)) {
          ss << "null";
        }
      } else {
        std::stringstream dummy_ss;
        if (!parse_cdr_field(elem_member, buffer, size, offset, dummy_ss,
                             indent_level, swap_bytes)) {
          break;
        }
      }
    }
    if (count > display_count) {
      ss << ", ... (" << count << " items total)";
    }
    ss << "]";
    return true;
  }

  switch (member.type_id_) {
    case ROS_TYPE_BOOLEAN: {
      if (offset >= size) return false;
      bool val = (buffer[offset] != 0);
      offset += 1;
      ss << (val ? "true" : "false");
      return true;
    }
    case ROS_TYPE_UINT8:
    case ROS_TYPE_OCTET: {
      if (offset >= size) return false;
      uint8_t val = buffer[offset];
      offset += 1;
      ss << static_cast<unsigned>(val);
      return true;
    }
    case ROS_TYPE_INT8:
    case ROS_TYPE_CHAR: {
      if (offset >= size) return false;
      int8_t val = static_cast<int8_t>(buffer[offset]);
      offset += 1;
      ss << static_cast<int>(val);
      return true;
    }
    case ROS_TYPE_UINT16: {
      align_offset(2);
      if (offset + 2 > size) return false;
      uint16_t val = 0;
      std::memcpy(&val, buffer + offset, 2);
      if (swap_bytes) val = cdr_byte_swap(val);
      offset += 2;
      ss << val;
      return true;
    }
    case ROS_TYPE_INT16: {
      align_offset(2);
      if (offset + 2 > size) return false;
      int16_t val = 0;
      std::memcpy(&val, buffer + offset, 2);
      if (swap_bytes) val = cdr_byte_swap(val);
      offset += 2;
      ss << val;
      return true;
    }
    case ROS_TYPE_UINT32: {
      align_offset(4);
      if (offset + 4 > size) return false;
      uint32_t val = 0;
      std::memcpy(&val, buffer + offset, 4);
      if (swap_bytes) val = cdr_byte_swap(val);
      offset += 4;
      ss << val;
      return true;
    }
    case ROS_TYPE_INT32: {
      align_offset(4);
      if (offset + 4 > size) return false;
      int32_t val = 0;
      std::memcpy(&val, buffer + offset, 4);
      if (swap_bytes) val = cdr_byte_swap(val);
      offset += 4;
      ss << val;
      return true;
    }
    case ROS_TYPE_UINT64: {
      align_offset(8);
      if (offset + 8 > size) return false;
      uint64_t val = 0;
      std::memcpy(&val, buffer + offset, 8);
      if (swap_bytes) val = cdr_byte_swap(val);
      offset += 8;
      ss << val;
      return true;
    }
    case ROS_TYPE_INT64: {
      align_offset(8);
      if (offset + 8 > size) return false;
      int64_t val = 0;
      std::memcpy(&val, buffer + offset, 8);
      if (swap_bytes) val = cdr_byte_swap(val);
      offset += 8;
      ss << val;
      return true;
    }
    case ROS_TYPE_FLOAT: {
      align_offset(4);
      if (offset + 4 > size) return false;
      float val = 0;
      std::memcpy(&val, buffer + offset, 4);
      if (swap_bytes) val = cdr_byte_swap(val);
      offset += 4;
      ss << val;
      return true;
    }
    case ROS_TYPE_DOUBLE: {
      align_offset(8);
      if (offset + 8 > size) return false;
      double val = 0;
      std::memcpy(&val, buffer + offset, 8);
      if (swap_bytes) val = cdr_byte_swap(val);
      offset += 8;
      ss << val;
      return true;
    }
    case ROS_TYPE_STRING: {
      align_offset(4);
      if (offset + 4 > size) return false;
      uint32_t len = 0;
      std::memcpy(&len, buffer + offset, 4);
      if (swap_bytes) len = cdr_byte_swap(len);
      offset += 4;
      if (len > 0 && offset + len <= size) {
        std::string str_val(
            reinterpret_cast<const char*>(buffer + offset),
            (buffer[offset + len - 1] == '\0') ? len - 1 : len);
        offset += len;
        ss << nlohmann::json(str_val).dump();
        return true;
      } else if (len == 0) {
        ss << "\"\"";
        return true;
      }
      return false;
    }
    case ROS_TYPE_MESSAGE: {
      if (member.members_ && member.members_->data) {
        const auto* sub_members =
            static_cast<const ::rosidl_typesupport_introspection_cpp::MessageMembers*>(
                member.members_->data);
        return parse_cdr_members(sub_members, buffer, size, offset, ss,
                                 indent_level, swap_bytes);
      }
      return false;
    }
    default:
      return false;
  }
}

inline std::string parse_cdr_to_json(
    const ::rosidl_typesupport_introspection_cpp::MessageMembers* members,
    const uint8_t* buffer, size_t size) {
  if (!members || !buffer || size < 4) {
    return "{}";
  }
  const bool swap_bytes = should_swap_cdr_bytes(buffer, size);
  size_t offset = 4; // Skip 4-byte CDR header
  std::stringstream ss;
  if (parse_cdr_members(members, buffer, size, offset, ss, 0, swap_bytes)) {
    return ss.str();
  }
  return "{}";
}

} // namespace lazyrtui
