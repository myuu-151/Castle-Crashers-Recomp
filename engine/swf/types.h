// Basic SWF value types and the bit-level reader used to parse them.
#pragma once

#include <algorithm>
#include <cstdint>
#include <cstring>
#include <string>

namespace swf {

struct Rgba {
    uint8_t r = 0, g = 0, b = 0, a = 255;
};

// Coordinates are in twips (1/20 pixel), as stored in the file.
struct Rect {
    int32_t xmin = 0, xmax = 0, ymin = 0, ymax = 0;
};

// 2x3 affine transform: x' = a*x + c*y + tx, y' = b*x + d*y + ty.
struct Matrix {
    float a = 1, b = 0, c = 0, d = 1, tx = 0, ty = 0;
    // Which optional terms the SWF MATRIX record had (castle.exe converts
    // only the present ones).
    bool has_scale = false, has_rotate = false;

    Matrix operator*(const Matrix& child) const {
        return {a * child.a + c * child.b,
                b * child.a + d * child.b,
                a * child.c + c * child.d,
                b * child.c + d * child.d,
                a * child.tx + c * child.ty + tx,
                b * child.tx + d * child.ty + ty};
    }
};

// Colour transform, as castle.exe applies it: a tint that multiplies the
// colour, out = in * (mul + add) / 256 per channel. Adobe's Flash Player adds
// instead (in * mul / 256 + add); castle.exe only modulates vertex colours,
// and Color.setRGB writes its colour into the multiply terms. Fitted to the
// real game's logo screen (the burst's colours match within one level) and
// read; nested tints multiply (to confirm).
struct CXform {
    float mul[4] = {256, 256, 256, 256};
    float add[4] = {0, 0, 0, 0};

    float tint(int channel) const { return (mul[channel] + add[channel]) / 256; }

    CXform operator*(const CXform& child) const {
        CXform out;
        for (int i = 0; i < 4; i++) out.mul[i] = tint(i) * child.tint(i) * 256;
        return out;
    }
};

class Reader {
public:
    Reader(const uint8_t* data, size_t size, size_t pos = 0) : data_(data), size_(size), pos_(pos) {}

    size_t pos() const { return pos_; }
    const uint8_t* data() const { return data_; }
    size_t size() const { return size_; }
    void seek(size_t pos) { pos_ = pos; bits_ = 0; }
    bool done() const { return pos_ >= size_; }

    uint8_t u8() { align(); return pos_ < size_ ? data_[pos_++] : 0; }
    uint16_t u16() { uint16_t lo = u8(); return uint16_t(lo | (u8() << 8)); }
    uint32_t u32() { uint32_t lo = u16(); return lo | (uint32_t(u16()) << 16); }
    int16_t s16() { return int16_t(u16()); }
    int32_t s32() { return int32_t(u32()); }
    float f32() { uint32_t v = u32(); float f; std::memcpy(&f, &v, 4); return f; }

    std::string cstring() {
        align();
        std::string s;
        while (pos_ < size_ && data_[pos_]) s += char(data_[pos_++]);
        pos_++;
        return s;
    }

    uint32_t ub(int n) {
        uint32_t v = 0;
        while (n-- > 0) {
            if (bits_ == 0) {
                cur_ = pos_ < size_ ? data_[pos_] : 0;
                pos_++;
                bits_ = 8;
            }
            bits_--;
            v = (v << 1) | ((cur_ >> bits_) & 1);
        }
        return v;
    }

    int32_t sb(int n) {
        if (n == 0) return 0;
        uint32_t v = ub(n);
        if (v & (1u << (n - 1))) v |= ~0u << n;
        return int32_t(v);
    }

    // 16.16 fixed point stored in n bits.
    float fb(int n) { return float(sb(n)) / 65536.0f; }

    void align() { bits_ = 0; }

    Rect rect() {
        align();
        int n = int(ub(5));
        Rect r;
        r.xmin = sb(n);
        r.xmax = sb(n);
        r.ymin = sb(n);
        r.ymax = sb(n);
        align();
        return r;
    }

    Matrix matrix() {
        align();
        Matrix m;
        if (ub(1)) {
            int n = int(ub(5));
            m.a = fb(n);
            m.d = fb(n);
            m.has_scale = true;
        }
        if (ub(1)) {
            int n = int(ub(5));
            m.b = fb(n);
            m.c = fb(n);
            m.has_rotate = true;
        }
        int n = int(ub(5));
        m.tx = float(sb(n));
        m.ty = float(sb(n));
        align();
        return m;
    }

    CXform cxform(bool with_alpha) {
        align();
        CXform cx;
        bool has_add = ub(1), has_mul = ub(1);
        int n = int(ub(4));
        int channels = with_alpha ? 4 : 3;
        if (has_mul)  // castle.exe clamps negative multipliers to 0
            for (int i = 0; i < channels; i++) cx.mul[i] = float(std::max(0, sb(n)));
        if (has_add)
            for (int i = 0; i < channels; i++) cx.add[i] = float(sb(n));
        align();
        return cx;
    }

    Rgba rgb() { Rgba c; c.r = u8(); c.g = u8(); c.b = u8(); return c; }
    Rgba rgba() { Rgba c = rgb(); c.a = u8(); return c; }

private:
    const uint8_t* data_;
    size_t size_;
    size_t pos_;
    uint8_t cur_ = 0;
    int bits_ = 0;
};

}  // namespace swf
