#pragma once

#include <cstddef>
#include <cstdint>
#include <cstring>
#include <cmath>
#include <limits>
#include <stdexcept>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace restoration::network {

inline constexpr std::uint16_t WireVersion = 1;
inline constexpr std::size_t MaxWirePacketBytes = 64 * 1024;

class WireError : public std::runtime_error {
public:
    using std::runtime_error::runtime_error;
};

class Writer {
public:
    Writer& u8(std::uint8_t value) { append(&value, 1); return *this; }
    Writer& u16(std::uint16_t value) { u8(static_cast<std::uint8_t>(value)); u8(static_cast<std::uint8_t>(value >> 8)); return *this; }
    Writer& u32(std::uint32_t value) { u16(static_cast<std::uint16_t>(value)); u16(static_cast<std::uint16_t>(value >> 16)); return *this; }
    Writer& u64(std::uint64_t value) { u32(static_cast<std::uint32_t>(value)); u32(static_cast<std::uint32_t>(value >> 32)); return *this; }
    Writer& i32(std::int32_t value) { std::uint32_t bits; std::memcpy(&bits, &value, sizeof(bits)); return u32(bits); }
    Writer& f64(double value) {
        if (!std::isfinite(value)) throw WireError("wire float must be finite");
        std::uint64_t bits; std::memcpy(&bits, &value, sizeof(bits)); return u64(bits);
    }
    Writer& boolean(bool value) { return u8(value ? 1 : 0); }
    Writer& string(std::string_view value, std::size_t maxBytes = MaxWirePacketBytes) {
        if (value.size() > maxBytes || value.size() > std::numeric_limits<std::uint32_t>::max() ||
            value.size() > MaxWirePacketBytes - sizeof(std::uint32_t) || data_.size() > MaxWirePacketBytes - sizeof(std::uint32_t) - value.size())
            throw WireError("wire string exceeds its limit");
        u32(static_cast<std::uint32_t>(value.size()));
        append(reinterpret_cast<const std::uint8_t*>(value.data()), value.size());
        return *this;
    }
    Writer& bytes(const std::uint8_t* value, std::size_t size, std::size_t maxBytes = MaxWirePacketBytes) {
        if ((size && !value) || size > maxBytes || size > std::numeric_limits<std::uint32_t>::max() ||
            size > MaxWirePacketBytes - sizeof(std::uint32_t) || data_.size() > MaxWirePacketBytes - sizeof(std::uint32_t) - size)
            throw WireError("wire bytes exceed their limit");
        u32(static_cast<std::uint32_t>(size));
        append(value, size);
        return *this;
    }
    Writer& bytes(const std::vector<std::uint8_t>& value, std::size_t maxBytes = MaxWirePacketBytes) {
        return bytes(value.data(), value.size(), maxBytes);
    }
    const std::vector<std::uint8_t>& data() const { return data_; }
    std::vector<std::uint8_t> take() { return std::move(data_); }

private:
    void append(const std::uint8_t* value, std::size_t size) {
        if (size > MaxWirePacketBytes || data_.size() > MaxWirePacketBytes - size)
            throw WireError("wire packet exceeds 64 KiB");
        if (size) data_.insert(data_.end(), value, value + size);
    }
    std::vector<std::uint8_t> data_;
};

class Reader {
public:
    Reader(const std::uint8_t* data, std::size_t size) : data_(data), size_(size) {
        if (size > MaxWirePacketBytes || (size && !data)) throw WireError("invalid wire packet");
    }
    explicit Reader(const std::vector<std::uint8_t>& data) : Reader(data.data(), data.size()) {}

    std::uint8_t u8() { require(1); return data_[offset_++]; }
    std::uint16_t u16() { const auto a = u8(), b = u8(); return static_cast<std::uint16_t>(a | (std::uint16_t(b) << 8)); }
    std::uint32_t u32() { const auto a = u16(), b = u16(); return std::uint32_t(a) | (std::uint32_t(b) << 16); }
    std::uint64_t u64() { const auto a = u32(), b = u32(); return std::uint64_t(a) | (std::uint64_t(b) << 32); }
    std::int32_t i32() { const auto bits = u32(); std::int32_t value; std::memcpy(&value, &bits, sizeof(value)); return value; }
    double f64() {
        const auto bits = u64(); double value; std::memcpy(&value, &bits, sizeof(value));
        if (!std::isfinite(value)) throw WireError("wire float must be finite");
        return value;
    }
    bool boolean() {
        const auto value = u8();
        if (value > 1) throw WireError("invalid wire boolean");
        return value != 0;
    }
    std::string string(std::size_t maxBytes = MaxWirePacketBytes) {
        const auto size = length(maxBytes);
        require(size);
        std::string value;
        if (size) value.assign(reinterpret_cast<const char*>(data_ + offset_), size);
        offset_ += size;
        return value;
    }
    std::vector<std::uint8_t> bytes(std::size_t maxBytes = MaxWirePacketBytes) {
        const auto size = length(maxBytes);
        require(size);
        std::vector<std::uint8_t> value;
        if (size) value.assign(data_ + offset_, data_ + offset_ + size);
        offset_ += size;
        return value;
    }
    std::size_t remaining() const { return size_ - offset_; }
    void finish() const { if (remaining()) throw WireError("trailing wire data"); }

private:
    std::uint32_t length(std::size_t maxBytes) {
        const auto size = u32();
        if (size > maxBytes) throw WireError("wire field exceeds its limit");
        return size;
    }
    void require(std::size_t size) const {
        if (size > size_ - offset_) throw WireError("truncated wire packet");
    }
    const std::uint8_t* data_;
    std::size_t size_, offset_ = 0;
};

} // namespace restoration::network
