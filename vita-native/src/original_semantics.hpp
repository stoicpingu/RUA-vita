#pragma once
#include <cstdint>
#include <limits>

namespace rua {
// Java int arithmetic is modulo 2^32; signed overflow in C++ is undefined.
inline int32_t javaInt(uint32_t bits) {
    return bits <= 0x7fffffffU ? int32_t(bits) : -1 - int32_t(~bits);
}
inline int32_t javaAdd(int32_t a, int32_t b) { return javaInt(uint32_t(a) + uint32_t(b)); }
inline int32_t javaMultiply(int32_t a, int32_t b) { return javaInt(uint32_t(a) * uint32_t(b)); }
inline int32_t javaCast(double value) {
    if (value != value) return 0;
    if (value >= 2147483647.0) return INT32_MAX;
    if (value <= -2147483648.0) return INT32_MIN;
    return int32_t(value);
}
// java.util.Random's 48-bit state, including nextInt's rejection draw.
class JavaRandom {
public:
    explicit JavaRandom(uint64_t seed) : state_((seed ^ 0x5deece66dULL) & mask) {}
    uint32_t next(int bits) {
        state_ = (state_ * 0x5deece66dULL + 11) & mask;
        return uint32_t(state_ >> (48 - bits));
    }
    int nextInt(int bound) {
        if ((bound & -bound) == bound) return int((uint64_t(bound) * next(31)) >> 31);
        uint32_t bits, value;
        do { bits = next(31); value = bits % uint32_t(bound); }
        while (bits - value + uint32_t(bound - 1) >= 0x80000000U);
        return int(value);
    }
    float nextFloat() { return next(24) / 16777216.0f; }
private:
    static constexpr uint64_t mask = (1ULL << 48) - 1;
    uint64_t state_;
};
struct Rect {
    int left = 0, top = 0, right = 0, bottom = 0;
    bool intersects(const Rect& other) const {
        // Android deliberately does not perform a separate empty-rectangle check.
        return left < other.right && other.left < right &&
               top < other.bottom && other.top < bottom;
    }
};
}
