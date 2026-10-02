// Script objects. Every castle.exe object has a type code (+0x18), a 16-bit
// reference count (+0x1c) and the same virtual slots: "last reference
// released" (vtable[1]), GetMember (slot 3), SetMember (slot 4) and
// CallMethod (slot 5). See docs/engine/actionscript-semantics.md 1.2-1.5.
#pragma once

#include <cstdint>
#include <utility>
#include <vector>

#include "as/value.h"

namespace as {

class Interpreter;

// A property map: cells sorted by name ID (the original's inserts,
// the original's finds), the order for..in would enumerate them in.
class Properties {
public:
    Cell* find(NameId name);
    Cell& get_or_add(NameId name);
    bool erase(NameId name);  // releases the cell's object
    void clear();             // releases every object
    bool empty() const { return cells_.empty(); }
    std::vector<std::pair<NameId, Cell>>& cells() { return cells_; }

private:
    std::vector<std::pair<NameId, Cell>> cells_;
};

// Arguments of a call, first argument first (at most 12).
struct Args {
    int count = 0;
    Value* values = nullptr;
    const Value& operator[](int i) const { return values[i]; }
};

class Object {
public:
    Object() : type_(kObject) {}  // a plain Object (new Object())
    explicit Object(uint32_t type) : type_(type) {}
    virtual ~Object() = default;
    Object(const Object&) = delete;
    Object& operator=(const Object&) = delete;

    uint32_t type() const { return type_; }
    uint16_t refcount = 0;

    // vtable[1]: the last reference went away. Plain objects clear their
    // properties; subclasses clear what they hold.
    virtual void on_last_release() { props.clear(); }
    // GetMember: false if the name isn't found (the result stays untouched).
    virtual bool get_member(Interpreter& in, NameId name, Value& out);
    // SetMember.
    virtual void set_member(Interpreter& in, NameId name, const Value& v);
    // CallMethod: `result` starts undefined; set `abort` to stop the calling
    // script. The base class ignores calls.
    virtual void call_method(Interpreter& in, NameId name, Args args, Value& result, bool& abort);
    // ToString of this object: a movie clip's dotted path, else "" (ID 0).
    virtual NameId path_name() { return 0; }

    Properties props;

protected:
    uint32_t type_;
};

// The reference counting protocol (1.4).
// A value is pushed: count++ unless it is 0 (fresh objects stay at 0).
void retain_pushed(const Value& v);
// A value is popped or overwritten: if the count is 1 the object is reset
// (on_last_release), then count--; nothing if the count is 0.
void release(uint32_t type, Object* obj);
inline void release(const Value& v) { release(v.type, v.obj); }

}  // namespace as
