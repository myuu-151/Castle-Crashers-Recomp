#include "as/value.h"

#include <cmath>
#include <cstdarg>
#include <cstdio>
#include <cstdlib>

#include "as/builtins.h"
#include "as/object.h"

namespace as {

// CVTTSS2SI: truncation, with NaN and out-of-range values giving INT_MIN.
int32_t truncate_sse(float f) {
    if (std::isnan(f) || f >= 2147483648.0f || f < -2147483648.0f) return INT32_MIN;
    return int32_t(f);
}

std::string format_g12(double d) {
    // The 2008 C runtime writes at least three exponent digits ("e+029").
    std::string s = format("%.12g", d);
    size_t e = s.find('e');
    if (e != std::string::npos && e + 2 < s.size()) {
        size_t digits = s.size() - (e + 2);
        if (digits < 3) s.insert(e + 2, 3 - digits, '0');
    }
    return s;
}

namespace {

// MSVC atoi: leading spaces, a sign, then digits.
int32_t parse_int(const std::string& s) { return int32_t(std::atoi(s.c_str())); }

}  // namespace

void Value::set_object(Object* o) {
    if (!o) {
        set_undefined();
        return;
    }
    type = o->type();
    bits = 1;
    obj = o;
}

void Cell::store(const Value& v) {
    // Release the old contents, then take the new ones.
    Object* old = obj;
    uint32_t old_type = type;
    type = kUndefined;
    bits = 0;
    obj = nullptr;
    release(old_type, old);

    switch (v.type) {
    case kInt: type = kInt; bits = uint32_t(to_int(v)); break;
    case kBool: type = kBool; bits = to_bool(v) ? 1 : 0; break;
    case kString:
    case 0x20: type = kString; bits = to_name(v); break;
    case kFloat: type = kFloat; bits = v.bits; break;
    case kObject:
    case kScene:
    case kArray:
    case kSound:
    case kMovieClip:
    case kFunction:
    case kColor:
        if (v.obj) {
            type = kObject;  // objects are kept as type 1 with the pointer
            obj = v.obj;
            obj->refcount++;
        }
        break;
    default:  // undefined, and Math, Key, Graphic, Text: not storable
        break;
    }
}

void Cell::reset() {
    Object* old = obj;
    uint32_t old_type = type;
    type = kUndefined;
    bits = 0;
    obj = nullptr;
    release(old_type, old);
}

Value Cell::load() const {
    Value v;
    if (type == kObject) {
        v.set_object(obj);  // the object's real type, undefined if null
    } else {
        v.type = type;
        v.bits = bits;
    }
    return v;
}

namespace {

// The 2008 C runtime's strtod: leading white space, a sign,
// digits with one '.', an exponent; no hex, "inf" or "nan", which today's
// strtod would read.
double old_strtod(const std::string& s) {
    size_t i = 0, n = s.size();
    while (i < n && (s[i] == ' ' || (s[i] >= '\t' && s[i] <= '\r'))) i++;
    size_t start = i;
    if (i < n && (s[i] == '+' || s[i] == '-')) i++;
    size_t digits = 0;
    while (i < n && s[i] >= '0' && s[i] <= '9') i++, digits++;
    if (i < n && s[i] == '.') {
        i++;
        while (i < n && s[i] >= '0' && s[i] <= '9') i++, digits++;
    }
    if (!digits) return 0.0;
    if (i < n && (s[i] == 'e' || s[i] == 'E')) {
        size_t e = i + 1;
        if (e < n && (s[e] == '+' || s[e] == '-')) e++;
        if (e < n && s[e] >= '0' && s[e] <= '9') {
            while (e < n && s[e] >= '0' && s[e] <= '9') e++;
            i = e;
        }
    }
    return std::strtod(s.substr(start, i - start).c_str(), nullptr);
}

// The 2008 C runtime's "%f": at most 17 significant digits, the rest zeros.
std::string old_format_f(double d) {
    if (!std::isfinite(d) || std::fabs(d) < 1e17) return format("%f", d);
    std::string e = format("%.16e", d);  // "-d.dddddddddddddddde+XX"
    bool negative = e[0] == '-';
    if (negative) e.erase(0, 1);
    size_t exp_at = e.find('e');
    int exponent = std::atoi(e.c_str() + exp_at + 1);
    std::string digits = e.substr(0, 1) + e.substr(2, exp_at - 2);  // 17 digits
    std::string whole = digits;
    whole.resize(size_t(exponent) + 1, '0');
    return (negative ? "-" : "") + whole + ".000000";
}

}  // namespace

float to_float(const Value& v) {
    switch (v.type) {
    case kFloat: return v.f();
    case kBool: return v.b() ? 1.0f : 0.0f;
    case kInt: return float(v.i());
    case kString: return float(old_strtod(names().str(v.name())));
    case kMovieClip:
    case kFunction: return v.obj ? 1.0f : 0.0f;
    default: return 0.0f;
    }
}

int32_t to_int(const Value& v) {
    switch (v.type) {
    case kInt: return v.i();
    case kBool: return v.b() ? 1 : 0;
    case kFloat: return truncate_sse(v.f());
    case kString: return parse_int(names().str(v.name()));
    default: return 0;
    }
}

int32_t to_int_bits(const Value& v) {
    switch (v.type) {
    case kInt: return v.i();
    case kBool: return v.b() ? 1 : 0;
    case kFloat: {
        double t = std::trunc(double(v.f()));
        if (std::isnan(t) || std::abs(t) >= 9.2e18) return 0;
        return int32_t(uint32_t(uint64_t(int64_t(t))));
    }
    default: return 0;
    }
}

bool to_bool(const Value& v) {
    switch (v.type) {
    case kBool: return v.b();
    case kInt: return v.i() != 0;
    case kFloat: return v.f() != 0.0f;  // NaN is true
    case kSound:
    case kMovieClip:
    case kFunction:
    case kText: return v.obj != nullptr;
    default: return false;  // strings, Object, Array, Scene, Color, Math, Key
    }
}

NameId to_name(const Value& v) {
    switch (v.type) {
    case kString: return v.name();
    case kInt: return names().intern(format("%d", v.i()));
    case kFloat: {
        float f = v.f();
        int32_t i = truncate_sse(f);
        if (f == float(i)) return names().intern(format("%d", i));
        return names().intern(old_format_f(double(f)));
    }
    case kBool: return v.b() ? name::kTRUE : name::kFALSE;
    case kMovieClip: return v.obj ? v.obj->path_name() : 0;
    default: return 0;
    }
}

Object* object_of(const Value& v) {
    if ((v.type & kCounted) || v.type == kMath || v.type == kKey) return v.obj;
    return nullptr;
}

std::string format(const char* fmt, ...) {
    char buf[256];
    va_list args;
    va_start(args, fmt);
    std::vsnprintf(buf, sizeof(buf), fmt, args);
    va_end(args);
    return buf;
}

}  // namespace as
