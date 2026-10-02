// Script values, laid out like castle.exe's: a type code and a 32-bit payload
// (an int, a float's bits, a name ID or an object). Values live in a fixed
// pool (see context.h), and variables hold "cells", a slightly different
// form. See docs/engine/actionscript-semantics.md, sections 1.2-1.7.
#pragma once

#include <cstdint>
#include <cstring>
#include <string>

#include "as/names.h"

namespace as {

class Object;

// Type codes, as castle.exe stores them.
enum TypeCode : uint32_t {
    kUndefined = 0x80000000,
    kObject = 0x1,        // new Object()
    kBool = 0x2,
    kInt = 0x4,
    kString = 0x8,        // interned name ID
    kFloat = 0x10,        // 32-bit float
    kScene = 0x40,        // a movie's root timeline
    kArray = 0x80,
    kSound = 0x200,
    kMovieClip = 0x400,
    kMath = 0x1000,
    kKey = 0x2000,
    kFunction = 0x4000,
    kColor = 0x8000,
    kGraphic = 0x10000,
    kText = 0x40000,
};

// Types whose objects are reference counted.
constexpr uint32_t kCounted = 0x7c6c1;

// One value. For object types `obj` is the pointer (null when the payload was
// cleared); for the others `bits` is the payload.
struct Value {
    uint32_t type = kUndefined;
    uint32_t bits = 0;
    Object* obj = nullptr;

    bool is_undefined() const { return type == kUndefined; }
    bool is_object_type() const { return (type & kCounted) != 0 || type == kMath || type == kKey; }

    int32_t i() const { return int32_t(bits); }
    float f() const { float v; std::memcpy(&v, &bits, 4); return v; }
    bool b() const { return (bits & 0xFF) != 0; }
    NameId name() const { return bits & 0xFFFF; }

    void set_undefined() { type = kUndefined; bits = 0; obj = nullptr; }
    void set_bool(bool v) { type = kBool; bits = v ? 1 : 0; obj = nullptr; }
    void set_int(int32_t v) { type = kInt; bits = uint32_t(v); obj = nullptr; }
    void set_float(float v) { type = kFloat; std::memcpy(&bits, &v, 4); obj = nullptr; }
    void set_string(NameId id) { type = kString; bits = id; obj = nullptr; }
    void set_object(Object* o);  // type from the object; undefined if null
    // The payload cleared, the type kept: what "freeing" a pool slot does.
    void clear_payload() { bits = 0; obj = nullptr; }
};

// A variable, parameter or property cell (the original's stores into one).
// Objects of types 0x1 0x40 0x80 0x200 0x400 0x4000 0x8000 are kept as type 1
// with the pointer; Math, Key, Graphic and Text become undefined.
struct Cell {
    uint32_t type = kUndefined;
    uint32_t bits = 0;
    Object* obj = nullptr;

    // Stores `v` with castle.exe's conversion, releasing the old object and
    // retaining the new one.
    void store(const Value& v);
    // Releases any object and becomes undefined.
    void reset();
    Value load() const;
};

// Conversions (docs/engine/actionscript-semantics.md 1.7).
float to_float(const Value& v);
int32_t to_int(const Value& v);
int32_t to_int_bits(const Value& v);
bool to_bool(const Value& v);
NameId to_name(const Value& v);
// The object pointer for object types (including Math and Key), else null
//.
Object* object_of(const Value& v);

// sprintf into a std::string.
std::string format(const char* fmt, ...);

// CVTTSS2SI truncation: NaN and out-of-range values give INT_MIN.
int32_t truncate_sse(float f);

// "%.12g" as the 2008 C runtime wrote it (three-digit exponents).
std::string format_g12(double d);

}  // namespace as
