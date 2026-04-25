#pragma once
#include <cstdint>
#include <cstddef>
#include <cstring>
#include <vector>
#include <string_view>

// A simple read/write buffer with O(1) consume via a read offset.
// Avoids O(n) erase-from-front used in the original code.
class Buffer {
public:
    // Append raw bytes to the write end.
    void append(const uint8_t* data, size_t n) {
        buf_.insert(buf_.end(), data, data + n);
    }
    void append(std::string_view sv) {
        append(reinterpret_cast<const uint8_t*>(sv.data()), sv.size());
    }

    // Convenience typed appends
    void append_u8 (uint8_t  v) { append(&v, 1); }
    void append_u32(uint32_t v) { append(reinterpret_cast<uint8_t*>(&v), 4); }
    void append_i64(int64_t  v) { append(reinterpret_cast<uint8_t*>(&v), 8); }
    void append_dbl(double   v) { append(reinterpret_cast<uint8_t*>(&v), 8); }

    // Read pointer into unread data.
    [[nodiscard]] const uint8_t* read_ptr() const {
        return buf_.data() + roff_;
    }
    [[nodiscard]] uint8_t* write_ptr() { return buf_.data(); }

    // How many bytes are available to read.
    [[nodiscard]] size_t readable() const { return buf_.size() - roff_; }
    [[nodiscard]] size_t size()     const { return buf_.size(); }
    [[nodiscard]] bool   empty()    const { return readable() == 0; }

    // Advance read pointer (consume n bytes).
    void consume(size_t n) {
        roff_ += n;
        // Compact when at least half the buffer is dead space.
        if (roff_ > buf_.size() / 2) {
            buf_.erase(buf_.begin(), buf_.begin() + static_cast<ptrdiff_t>(roff_));
            roff_ = 0;
        }
    }

    // Direct access into the underlying storage (for writev / scatter-gather).
    [[nodiscard]] const uint8_t* data_at(size_t abs_pos) const { return buf_.data() + abs_pos; }
    [[nodiscard]]       uint8_t* data_at(size_t abs_pos)       { return buf_.data() + abs_pos; }
    [[nodiscard]] size_t abs_write_pos() const { return buf_.size(); }

    void resize_to(size_t abs_pos) { buf_.resize(abs_pos); }
    void clear() { buf_.clear(); roff_ = 0; }

private:
    std::vector<uint8_t> buf_;
    size_t               roff_ = 0;
};
