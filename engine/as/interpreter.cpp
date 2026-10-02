#include "as/interpreter.h"

#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>

#include <SDL3/SDL_log.h>

#include "as/builtins.h"
#include "as/rng.h"
#include "player/movieclip.h"

namespace as {

// Debugging: CASTLE_TRACE=FILE logs every call the scripts make.
FILE* call_trace() {
    static FILE* f = [] {
        const char* p = std::getenv("CASTLE_TRACE");
        return p ? std::fopen(p, "w") : nullptr;
    }();
    return f;
}

// CASTLE_TRACE_VARS=1 (with CASTLE_TRACE): also every GetVariable and
// GetMember with the type found.
FILE* var_trace() {
    static bool on = std::getenv("CASTLE_TRACE_VARS") != nullptr;
    return on ? call_trace() : nullptr;
}

// A value's type for the trace, with which object it is (two lookups that
// find the same type can find different clips).
std::string traced(const Value& v) {
    Object* o = v.type == kMovieClip || v.type == kObject ? object_of(v) : nullptr;
    return o ? format("%d@%p", int(v.type), static_cast<void*>(o)) : format("%d", int(v.type));
}

FILE* set_trace() {
    static FILE* f = [] {
        const char* p = std::getenv("CASTLE_TRACE_SET_FILE");
        return p && std::getenv("CASTLE_TRACE_SET") ? std::fopen(p, "w") : nullptr;
    }();
    return f;
}

std::vector<NameId> g_called;  // the functions being run, for set_trace()

NameId set_trace_name() {
    static NameId n = [] {
        const char* s = std::getenv("CASTLE_TRACE_SET");
        return s ? names().intern(s) : NameId(0);
    }();
    return n;
}

namespace {

enum Op : uint8_t {
    kEnd = 0x00,
    kNextFrame = 0x04,
    kPrevFrame = 0x05,
    kPlay = 0x06,
    kStop = 0x07,
    kStopSounds = 0x09,
    kSubtract = 0x0b,
    kMultiply = 0x0c,
    kDivide = 0x0d,
    kAnd = 0x10,
    kOr = 0x11,
    kNot = 0x12,
    kStringExtract = 0x15,
    kPop = 0x17,
    kToInteger = 0x18,
    kGetVariable = 0x1c,
    kSetVariable = 0x1d,
    kGetProperty = 0x22,
    kSetProperty = 0x23,
    kCloneSprite = 0x24,
    kRemoveSprite = 0x25,
    kTrace = 0x26,
    kRandomNumber = 0x30,
    kAsciiToChar = 0x33,
    kGetTime = 0x34,
    kDelete = 0x3a,
    kDelete2 = 0x3b,
    kDefineLocal = 0x3c,
    kCallFunction = 0x3d,
    kReturn = 0x3e,
    kModulo = 0x3f,
    kNewObject = 0x40,
    kDefineLocal2 = 0x41,
    kAdd2 = 0x47,
    kLess2 = 0x48,
    kEquals2 = 0x49,
    kToNumber = 0x4a,
    kToString = 0x4b,
    kPushDuplicate = 0x4c,
    kGetMember = 0x4e,
    kSetMember = 0x4f,
    kIncrement = 0x50,
    kDecrement = 0x51,
    kCallMethod = 0x52,
    kEnumerate2 = 0x55,
    kBitAnd = 0x60,
    kBitOr = 0x61,
    kBitXor = 0x62,
    kBitLShift = 0x63,
    kBitRShift = 0x64,
    kStrictEquals = 0x66,
    kGreater = 0x67,
    kToBoolean = 0x70,  // fused with the following Not
    kGotoFrame = 0x81,
    kStoreRegister = 0x87,
    kConstantPool = 0x88,
    kPush = 0x96,
    kJump = 0x99,
    kGetUrl2 = 0x9a,
    kDefineFunction = 0x9b,
    kIf = 0x9d,
    kCall = 0x9e,
    kGotoFrame2 = 0x9f,
    kPushGetVariable = 0xa0,
    kPush2GetVariable = 0xa1,
    kPushGetMember = 0xa2,
    kPushSetMember = 0xa3,
    kPushDefineLocal = 0xa4,
};

uint16_t u16(const uint8_t* p) { return uint16_t(p[0] | (p[1] << 8)); }
int32_t s32(const uint8_t* p) { return int32_t(uint32_t(p[0]) | (uint32_t(p[1]) << 8) | (uint32_t(p[2]) << 16) | (uint32_t(p[3]) << 24)); }

std::string str(NameId id) { return names().str(id); }

// eq(x, y), switched on x's type: not symmetric.
bool eq(const Value& x, const Value& y) {
    switch (x.type) {
    case kFloat: {
        float d = to_float(y) - x.f();
        return std::fabs(d) <= 9.99999975e-6f;
    }
    case kInt:
        if (y.type == kFloat) return std::fabs(to_float(y) - float(x.i())) <= 9.99999975e-6f;
        return x.i() == to_int(y);
    case kBool: return x.b() == to_bool(y);
    case kString: return x.name() == to_name(y);
    case kMovieClip:
    case kFunction: return x.obj == object_of(y);
    default: return false;  // Object, Array, Scene, Sound, Color, Math, Key, Text
    }
}

// str(a) for Add2's left operand.
std::string add_left_text(const Value& a) {
    switch (a.type) {
    case kBool: return a.b() ? "true" : "false";
    case kInt: return format("%d", a.i());
    case kFloat: return format_g12(double(a.f()));
    default: return str(to_name(a));
    }
}

}  // namespace

Interpreter::Interpreter(Host& host) : host_(host) { frames_[0] = nullptr; }

Interpreter::Slot* Interpreter::allocate() {
    for (int n = 0; n < kSlots; n++) {
        int i = (cursor_ + n) % kSlots;
        if (!pool_[size_t(i)].in_use) {
            cursor_ = i;
            pool_[size_t(i)].in_use = true;
            return &pool_[size_t(i)];
        }
    }
    return &pool_[0];  // castle.exe would crash on a full pool
}

void Interpreter::push(Slot* s) {
    retain_pushed(*s);
    if (sp_ < kSlots) stack_[size_t(sp_++)] = s;
}

Interpreter::Slot* Interpreter::pop() {
    if (sp_ == 0) {
        Slot* s = allocate();  // castle.exe reads below the stack; give a fresh slot
        s->set_undefined();
        return s;
    }
    Slot* s = stack_[size_t(--sp_)];
    release(*s);
    return s;
}

void Interpreter::reset_stack() {
    sp_ = 0;
    for (auto& s : pool_) {
        s.clear_payload();
        s.in_use = false;
    }
}

const ConstantPool* Interpreter::pool_in_force(const Runner& r) const {
    if (Function* f = current_function()) return f->pool.get();
    return r.pool.get();
}

void Interpreter::decode_push_item(const Runner& r, const uint8_t* body, size_t length, size_t& pos, Value& out) {
    uint8_t type = body[pos++];
    auto constant = [&](size_t index) {
        const ConstantPool* p = pool_in_force(r);
        out.set_string(p && index < p->ids.size() ? p->ids[index] : 0);
    };
    switch (type) {
    case 0: {  // string
        std::string s;
        while (pos < length && body[pos]) s += char(body[pos++]);
        pos++;
        out.set_string(names().intern(s));
        break;
    }
    case 1: {  // float, read big-endian
        uint32_t bits = (uint32_t(body[pos]) << 24) | (uint32_t(body[pos + 1]) << 16) |
                        (uint32_t(body[pos + 2]) << 8) | uint32_t(body[pos + 3]);
        pos += 4;
        float f;
        std::memcpy(&f, &bits, 4);
        out.set_float(f);
        break;
    }
    case 2:  // null: not handled, the slot keeps what it had
        break;
    case 3: out.set_undefined(); break;
    case 4:  // register: the one register, whatever the number
        pos += 1;
        out.set_int(register_);
        break;
    case 5: out.set_bool(body[pos++] == 1); break;
    case 6: {  // double, high word first, narrowed to float
        uint64_t hi = uint32_t(s32(body + pos)), lo = uint32_t(s32(body + pos + 4));
        pos += 8;
        uint64_t bits = (hi << 32) | lo;
        double d;
        std::memcpy(&d, &bits, 8);
        out.set_float(float(d));
        break;
    }
    case 7:
        out.set_int(s32(body + pos));
        pos += 4;
        break;
    case 8: constant(body[pos++]); break;
    case 9:
        constant(u16(body + pos));
        pos += 2;
        break;
    default: break;
    }
}

NameId Interpreter::decode_name(const Runner& r, const uint8_t* body, size_t length, size_t& pos) {
    uint8_t type = body[pos];
    if (type != 0 && type != 8 && type != 9) {
        pos++;
        return 0;
    }
    Value v;
    decode_push_item(r, body, length, pos, v);
    return v.name();
}

void Interpreter::lookup(NameId id, Value& out) {
    player::MovieClip* t = player::MovieClip::from(target());
    switch (id) {
    case name::k_parent:
        if (t && t->parent) out.set_object(t->parent);
        else out.set_undefined();
        return;
    case name::k_root: out.set_object(reinterpret_cast<Object*>(root)); return;
    case name::kconsole_version: out.set_bool(true); return;
    case name::kKey: out.set_object(&key_object); return;
    case name::kMath: out.set_object(&math_object); return;
    default: break;
    }
    if (Function* f = current_function()) {
        for (int i = 0; i < Function::kMaxParams && f->param_names[size_t(i)]; i++)
            if (f->param_names[size_t(i)] == id) {
                out = f->params[size_t(i)].load();
                return;
            }
        if (Cell* c = f->props.find(id)) {
            out = c->load();
            return;
        }
    }
    if (Object* o = target(); o && o->get_member(*this, id, out)) return;
    out.set_undefined();
}

void Interpreter::set_variable(NameId id, const Value& v) {
    if (Function* f = current_function()) {
        if (Cell* c = f->props.find(id)) {
            c->store(v);
            return;
        }
        for (int i = 0; i < Function::kMaxParams && f->param_names[size_t(i)]; i++)
            if (f->param_names[size_t(i)] == id) {
                f->params[size_t(i)].store(v);
                return;
            }
    }
    if (Object* o = target()) o->set_member(*this, id, v);
}

void Interpreter::define_local(NameId id, const Value& v) {
    if (Function* f = current_function()) f->props.get_or_add(id).store(v);
    else if (Object* o = target()) o->set_member(*this, id, v);
}

player::MovieClip* Interpreter::find_clip(NameId id) {
    player::MovieClip* t = player::MovieClip::from(target());
    if (id == 0) return t;
    for (player::MovieClip* c = t; c; c = c->parent)
        if (player::MovieClip* child = c->child_named(id)) return child;
    return nullptr;
}

void Interpreter::get_member(Value& obj, const Value& key, bool text_special) {
    if (obj.is_undefined() || key.is_undefined()) {
        obj.set_undefined();
        return;
    }
    switch (obj.type) {
    case kArray: {
        auto* a = static_cast<Array*>(obj.obj);
        if (!a) break;
        if (key.type == kInt || key.type == kFloat) {
            int i = to_int(key);
            if (i >= 0 && i < a->length()) {
                const Value& e = a->elements[size_t(i)];
                obj.type = e.type;
                obj.bits = e.bits;
                obj.obj = e.obj;
            } else {
                obj.set_undefined();  // castle.exe reads past the end
            }
        } else if (key.type == kString) {
            if (!a->get_member(*this, key.name(), obj)) obj.set_undefined();
        }
        // bool keys: the array itself
        return;
    }
    case kString:
        if (to_name(key) == name::klength) obj.set_int(int32_t(str(obj.name()).size()));
        return;  // any other key: the string itself
    case kObject:
    case kScene:
    case kMovieClip:
        if (Object* o = obj.obj) {
            Value out;
            if (o->get_member(*this, to_name(key), out)) obj = out;
            else obj.set_undefined();
        }
        return;
    case kText:
        if (text_special && obj.obj) {
            if (to_name(key) == name::kntext) {
                Value out;
                if (obj.obj->get_member(*this, name::kntext, out)) obj = out;
                else obj.set_undefined();
            }
        }
        return;
    default:
        return;  // Sound, Color, Function, Math, Key, numbers: unchanged
    }
}

void Interpreter::set_member(const Value& obj, const Value& key, const Value& v) {
    if (obj.is_undefined() || key.is_undefined()) return;
    Object* p = object_of(obj);
    if (!p) return;  // castle.exe dereferences null here
    if (p->type() == kArray) {
        auto* a = static_cast<Array*>(p);
        uint32_t i = uint32_t(to_int(key));
        if (i < uint32_t(a->length())) a->store_element(int(i), v);
        return;
    }
    p->set_member(*this, to_name(key), v);
}

void Interpreter::add2(Slot* a, Slot* b) {
    // Pop b, pop a (already done by the caller).
    if (a->is_undefined() && b->is_undefined()) {
        a->set_int(0);
        push(a);
        free(b);
        return;
    }
    if (b->is_undefined()) {
        push(a);
        free(b);
        return;
    }
    if (a->is_undefined()) {
        free(a);
        push(b);
        return;
    }
    Value va = *a, vb = *b;
    free(a);
    free(b);
    Slot* r = allocate();
    auto sum_demote = [&] {
        double s = double(to_float(va)) + double(to_float(vb));
        if (va.type == kInt) s = double(va.i()) + double(vb.f());
        else if (vb.type == kInt) s = double(va.f()) + double(vb.i());
        float f = float(s);
        int32_t i = std::isnan(f) || std::fabs(f) >= 2147483648.0f ? INT32_MIN : int32_t(f);
        if (float(i) == f) r->set_int(i);
        else r->set_float(f);
    };
    switch (vb.type) {
    case kBool:
        if (va.type == kString) r->set_string(names().intern(str(va.name()) + (vb.b() ? "true" : "false")));
        else r->set_int(to_int(va) + to_int(vb));
        break;
    case kInt:
        if (va.type == kString) r->set_string(names().intern(str(va.name()) + format("%d", vb.i())));
        else if (va.type == kInt) r->set_int(int32_t(uint32_t(va.i()) + uint32_t(vb.i())));
        else if (va.type == kFloat) sum_demote();
        else r->set_int(to_int(va) + vb.i());
        break;
    case kString: r->set_string(names().intern(add_left_text(va) + str(vb.name()))); break;
    case kFloat:
        if (va.type == kString) r->set_string(names().intern(str(va.name()) + format_g12(double(vb.f()))));
        else if (va.type == kInt) sum_demote();
        else if (va.type == kFloat) r->set_float(va.f() + vb.f());
        else r->set_float(to_float(va) + vb.f());
        break;
    default:
        break;  // nothing written: the slot keeps what it held
    }
    push(r);
}

void Interpreter::run_frame_actions(Runner& r, int frame) {
    if (frame == 0) frame = 1;
    auto blocks = r.frame_actions.find(frame);
    if (blocks == r.frame_actions.end()) return;
    auto cached = r.frame_pools.find(frame);
    r.pool = cached == r.frame_pools.end() ? nullptr : cached->second;
    r.built.reset();
    int scratch_result = 0;
    (void)scratch_result;
    for (const Code& code : blocks->second) {
        bool abort = false;
        Value scratch;
        if (!run(r, code, &scratch, false, abort)) {
            r.pool.reset();
            r.load_pool.reset();
            r.enter_pool.reset();
            r.frame_pools.clear();
            reset_stack();
            return;
        }
    }
    if (r.built) r.frame_pools[frame] = r.built;
    r.built.reset();
    r.pool.reset();
}

void Interpreter::run_clip_event(Runner& r, bool load) {
    r.pool = load ? r.load_pool : r.enter_pool;
    r.built.reset();
    const Code& code = load ? r.load_code : r.enter_code;
    if (!code.data) {
        r.pool.reset();
        return;
    }
    bool abort = false;
    Value scratch;
    if (!run(r, code, &scratch, false, abort)) {
        r.pool.reset();
        r.load_pool.reset();
        r.enter_pool.reset();
        r.frame_pools.clear();
        reset_stack();
        return;
    }
    if (r.built) (load ? r.load_pool : r.enter_pool) = r.built;
    r.built.reset();
    r.pool.reset();
}

void Interpreter::call_function(Runner& r, NameId id, Args args, Value& result, bool& abort) {
    Function* f = nullptr;
    auto it = r.functions.find(id);
    if (it != r.functions.end()) {
        f = it->second;
    } else {
        Value v;
        lookup(id, v);
        if (v.is_undefined() || !object_of(v)) return;
        if (v.obj->type() != kFunction) return;  // castle.exe would run it anyway
        f = static_cast<Function*>(v.obj);
    }
    for (int i = 0; i < f->param_count; i++) f->params[size_t(i)].reset();
    for (int i = 0; i < args.count && i < Function::kMaxParams; i++) f->params[size_t(i)].store(args[i]);
    if (!f->code.data || f->code.size == 0) return;

    frames_[size_t(++frame_top_)] = f;
    push_target(f->defining_clip);
    size_t start = 0;
    while (start < f->code.size && f->code.data[start] == 0) start++;
    Code body{f->code.data + start, f->code.size - start};
    run(r, body, &result, true, abort);
    f->props.clear();
    for (auto& p : f->params) p.reset();
    pop_target();
    frames_[size_t(frame_top_--)] = nullptr;
}

bool Interpreter::run(Runner& r, const Code& code, Value* result, bool function, bool& abort) {
    size_t pc = 0;
    while (pc < code.size && !abort) {
        uint8_t op = code.data[pc];
        if (op == kEnd) break;
        size_t size = 1;
        uint16_t length = 0;
        const uint8_t* body = nullptr;
        if (op >= 0x80) {
            length = u16(code.data + pc + 1);
            size = 3 + length;
            body = code.data + pc + 3;
        }
        int extra = execute(r, op, body, length, result, abort);
        pc = size_t(int64_t(pc) + int64_t(size) + extra);
        if (function && op == kReturn) break;
    }
    return !abort;
}

int Interpreter::execute(Runner& r, uint8_t op, const uint8_t* body, uint16_t length, Value* result, bool& abort) {
    player::MovieClip* t = player::MovieClip::from(target());
    switch (op) {
    // ---- Timeline (2.1)
    case kNextFrame:
        if (t && t->current_frame() + 1 <= t->total_frames) {
            t->flags |= player::MovieClip::kStopRequested;
            t->goto_frame(t->current_frame() + 1);
        }
        return 0;
    case kPrevFrame:
        if (t && t->current_frame() - 1 != 0) {
            t->flags |= player::MovieClip::kStopRequested;
            t->goto_frame(t->current_frame() - 1);
        }
        return 0;
    case kPlay:
        if (t) t->flags = uint8_t((t->flags | player::MovieClip::kPlaying) & ~player::MovieClip::kStopRequested);
        return 0;
    case kStop:
        if (t) t->flags |= player::MovieClip::kStopRequested;
        return 0;
    case kGotoFrame: {
        if (!t) return 0;
        int f = int(uint16_t(u16(body) + 1));
        if (f > t->total_frames) f = t->total_frames;
        if (f == 0) f = 1;
        t->flags |= player::MovieClip::kStopRequested;
        t->goto_frame(f);
        return 0;
    }
    case kGotoFrame2: {
        Slot* f = pop();
        if (f->is_undefined() || !t) {
            free(f);
            return 0;
        }
        int n;
        if (f->type == kInt || f->type == kFloat) n = to_int(*f);
        else n = t->label_frame(to_name(*f));
        if (n > t->total_frames) n = t->total_frames;
        if (n < 1) n = 1;
        if (length > 0 && body[0]) t->flags &= uint8_t(~player::MovieClip::kStopRequested);
        else t->flags |= player::MovieClip::kStopRequested;
        t->goto_frame(n);
        free(f);
        return 0;
    }
    case kStopSounds: host_.stop_all_sounds(); return 0;

    // ---- Arithmetic (2.2, 2.3)
    case kSubtract:
    case kMultiply:
    case kDivide: {
        Slot* b = pop();
        Slot* a = pop();
        float fb = b->is_undefined() ? 0.0f : to_float(*b);
        float fa = a->is_undefined() ? 0.0f : to_float(*a);
        if (op == kSubtract) a->set_float(fa - fb);
        else if (op == kMultiply) a->set_float(fa * fb);
        else if (fb == 0.0f) a->set_undefined();
        else a->set_float(fa / fb);
        push(a);
        free(b);
        return 0;
    }
    case kModulo: {
        Slot* b = pop();
        Slot* a = pop();
        int32_t ib = b->is_undefined() ? 0 : to_int(*b);
        int32_t ia = a->is_undefined() ? 0 : to_int(*a);
        if (ib == 0 || (ia == INT32_MIN && ib == -1)) a->set_undefined();
        else a->set_int(ia % ib);
        push(a);
        free(b);
        return 0;
    }
    case kIncrement:
    case kDecrement: {
        Slot* v = pop();
        int step = op == kIncrement ? 1 : -1;
        if (v->is_undefined()) v->set_int(step);
        else if (v->type == kInt) v->set_int(int32_t(uint32_t(to_int(*v)) + uint32_t(step)));
        else if (v->type == kFloat) v->set_float(v->f() + float(step));
        push(v);
        return 0;
    }
    case kAdd2: {
        Slot* b = pop();
        Slot* a = pop();
        add2(a, b);
        return 0;
    }

    // ---- Comparisons and logic (2.4)
    case kLess2:
    case kGreater:
    case kAnd:
    case kOr: {
        Slot* b = pop();
        Slot* a = pop();
        float fb = b->is_undefined() ? 0.0f : to_float(*b);
        float fa = a->is_undefined() ? 0.0f : to_float(*a);
        bool res;
        if (op == kLess2) res = fa < fb;
        else if (op == kGreater) res = fa > fb;
        else if (op == kAnd) res = fa != 0 && fb != 0;
        else res = !(fa == 0 && fb == 0);
        a->set_bool(res);
        push(a);
        free(b);
        return 0;
    }
    case kNot: {
        Slot* v = pop();
        bool res = v->is_undefined() ? true : !to_bool(*v);
        v->set_bool(res);
        push(v);
        return 0;
    }
    case kToBoolean: {
        Slot* v = pop();
        bool res = v->is_undefined() ? false : to_bool(*v);
        v->set_bool(res);
        push(v);
        return 1;  // skips the following Not
    }
    case kEquals2: {
        Slot* b = pop();
        Slot* a = pop();
        bool res;
        if (a->is_undefined()) res = b->is_undefined();
        else if (b->is_undefined()) res = false;
        else if (a->type == b->type || (!(a->type & kCounted) && !(b->type & kCounted))) res = eq(*a, *b);
        else res = false;
        a->set_bool(res);
        push(a);
        free(b);
        return 0;
    }
    case kStrictEquals: {
        Slot* b = pop();
        Slot* a = pop();
        bool res = !a->is_undefined() && !b->is_undefined() && a->type == b->type && eq(*b, *a);
        a->set_bool(res);
        push(a);
        free(b);
        return 0;
    }

    // ---- Bits (2.5)
    case kBitAnd:
    case kBitOr:
    case kBitXor:
    case kBitLShift:
    case kBitRShift: {
        Slot* b = pop();
        Slot* a = pop();
        int32_t ib = b->is_undefined() ? 0 : to_int_bits(*b);
        int32_t res;
        if (op == kBitRShift) {
            res = int32_t(uint32_t(a->is_undefined() ? 0 : to_int(*a)) >> ((ib & 0xFF) & 31));
        } else {
            int32_t ia = a->is_undefined() ? 0 : to_int_bits(*a);
            if (op == kBitAnd) res = ia & ib;
            else if (op == kBitOr) res = ia | ib;
            else if (op == kBitXor) res = ia ^ ib;
            else res = int32_t(uint32_t(ia) << ((ib & 0xFF) & 31));
        }
        a->set_int(res);
        push(a);
        free(b);
        return 0;
    }

    // ---- Conversions and strings (2.6)
    case kToInteger:
    case kToNumber: {
        Slot* v = pop();
        v->set_int(v->is_undefined() ? 0 : to_int(*v));
        push(v);
        return 0;
    }
    case kToString: {
        Slot* v = pop();
        v->set_string(to_name(*v));
        push(v);
        return 0;
    }
    case kAsciiToChar: {
        Slot* v = pop();
        if (v->is_undefined()) {
            v->set_string(0);
        } else {
            char c = char(to_int(*v) & 0xFF);
            v->set_string(c ? names().intern(std::string(1, c)) : 0);
        }
        push(v);
        return 0;
    }
    case kStringExtract: {
        Slot* count = pop();
        Slot* index = pop();
        Slot* s = pop();
        if (s->is_undefined() || index->type != kInt || count->type != kInt) {
            s->set_string(0);
        } else {
            std::string text = str(s->name());
            int i = index->i() - 1, n = count->i();
            std::string out;
            if (i >= 0 && size_t(i) <= text.size()) out = n < 0 ? text.substr(size_t(i)) : text.substr(size_t(i), size_t(n));
            s->set_string(names().intern(out));
        }
        push(s);
        free(index);
        free(count);
        return 0;
    }

    // ---- Variables (2.7)
    case kGetVariable: {
        Slot* n = pop();
        NameId id = to_name(*n);
        Value out;
        lookup(id, out);
        if (FILE* t = var_trace()) std::fprintf(t, "GV %s -> %s\n", str(id).c_str(), traced(out).c_str());
        static_cast<Value&>(*n) = out;
        push(n);
        return 0;
    }
    case kSetVariable: {
        Slot* value = pop();
        Slot* n = pop();
        if (!n->is_undefined()) set_variable(to_name(*n), *value);
        free(value);
        free(n);
        return 0;
    }
    case kDefineLocal: {
        Slot* value = pop();
        Slot* n = pop();
        define_local(to_name(*n), *value);
        free(value);
        free(n);
        return 0;
    }
    case kDefineLocal2: {
        Slot* n = pop();
        NameId id = to_name(*n);
        if (Function* f = current_function()) {
            if (!f->props.find(id)) f->props.get_or_add(id);
        } else if (Object* o = target()) {
            o->set_member(*this, id, Value{});
        }
        free(n);
        return 0;
    }
    case kDelete: {
        Slot* n = pop();
        Slot* obj = pop();
        bool was = false;
        if (Object* p = object_of(*obj)) was = p->props.erase(to_name(*n));
        n->set_bool(was);
        push(n);
        free(obj);
        return 0;
    }
    case kDelete2: {
        Slot* n = pop();
        bool was = false;
        if (Object* o = target()) was = o->props.erase(to_name(*n));
        n->set_bool(was);
        push(n);
        return 0;
    }
    case kStoreRegister:
        if (Slot* top = peek(); top && !top->is_undefined()) register_ = to_int(*top);
        return 0;

    // ---- Properties (2.8)
    case kGetProperty: {
        Slot* index = pop();
        Slot* tgt = pop();
        player::MovieClip* clip = find_clip(to_name(*tgt));
        if (!clip) tgt->set_undefined();
        else clip->get_property(NameId(0x50 + to_int(*index)), *tgt);
        push(tgt);
        free(index);
        return 0;
    }
    case kSetProperty: {
        Slot* value = pop();
        Slot* index = pop();
        Slot* tgt = pop();
        if (player::MovieClip* clip = find_clip(to_name(*tgt)))
            clip->set_property(NameId(0x50 + to_int(*index)), *value);
        free(value);
        free(index);
        free(tgt);
        return 0;
    }

    // ---- Stack (2.9)
    case kPush: {
        size_t pos = 0;
        while (pos < length) {
            Slot* s = allocate();
            decode_push_item(r, body, length, pos, *s);
            push(s);
        }
        return 0;
    }
    case kPop:
    case kCall:
        free(pop());
        return 0;
    case kPushDuplicate:
        if (Slot* top = peek()) push(top);
        return 0;

    // ---- Control flow (2.10)
    case kJump: return int16_t(u16(body));
    case kIf: {
        Slot* c = pop();
        bool taken = to_bool(*c);
        free(c);
        return taken ? int16_t(u16(body)) : 0;
    }
    case kReturn: {
        Slot* v = pop();
        if (result) {
            result->type = v->type;
            result->bits = v->bits;
            result->obj = v->obj;
        }
        free(v);
        return 0;
    }

    // ---- Functions and calls (2.11)
    case kDefineFunction: {
        size_t pos = 0;
        std::string fname;
        while (pos < length && body[pos]) fname += char(body[pos++]);
        pos++;
        int nparams = u16(body + pos);
        pos += 2;
        std::vector<NameId> params;
        for (int i = 0; i < nparams; i++) {
            std::string p;
            while (pos < length && body[pos]) p += char(body[pos++]);
            pos++;
            params.push_back(names().intern(p));
        }
        uint16_t code_size = u16(body + pos);
        NameId id = names().intern(fname);
        if (r.functions.count(id)) return code_size;
        Function* f = functions.allocate();
        if (!f) {
            // (said once: the pool filling up, functions kept after their
            // clips had gone, left levels without their own functions)
            static bool said = false;
            if (!said) SDL_Log("DefineFunction %s: the pool of 1000 functions is full; not defined", fname.c_str());
            said = true;
            return code_size;
        }
        f->refcount++;
        f->name = id;
        f->param_count = std::min(nparams, Function::kMaxParams);
        for (int i = 0; i < f->param_count; i++) f->param_names[size_t(i)] = params[size_t(i)];
        f->code = {code_size ? body + length : nullptr, code_size};
        f->defining_clip = target();
        f->pool = r.pool;
        r.functions[id] = f;
        return code_size;
    }
    case kCallFunction: {
        Slot* n = pop();
        Slot* argc = pop();
        Value args[12];
        Slot* arg_slots[12] = {};
        int count = 0;
        if (!argc->is_undefined()) {
            count = std::max(0, std::min(12, to_int(*argc)));
            for (int i = 0; i < count; i++) {
                arg_slots[i] = pop();
                args[i] = *arg_slots[i];
            }
            argc->set_undefined();
        }
        if (set_trace()) g_called.push_back(to_name(*n));
        if (Object* o = target()) o->call_method(*this, to_name(*n), Args{count, args}, *argc, abort);
        if (set_trace()) g_called.pop_back();
        if (FILE* t = call_trace()) std::fprintf(t, "CF %s(%d) -> %d\n",str(to_name(*n)).c_str(), count, int(argc->type));
        push(argc);
        for (int i = 0; i < count; i++) free(arg_slots[i]);
        free(n);
        return 0;
    }
    case kCallMethod: {
        Slot* method = pop();
        Slot* obj = pop();
        Slot* argc = pop();
        Value args[12];
        Slot* arg_slots[12] = {};
        int count = 0;
        if (!argc->is_undefined()) {
            count = std::max(0, std::min(12, to_int(*argc)));
            for (int i = 0; i < count; i++) {
                arg_slots[i] = pop();
                args[i] = *arg_slots[i];
            }
        }
        argc->set_undefined();
        bool push_result = true;
        if (!obj->is_undefined() && !method->is_undefined()) {
            NameId m = to_name(*method);
            if (FILE* t = call_trace()) std::fprintf(t, "CM %d.%s(%d)\n",int(obj->type), str(m).c_str(), count);
            if (obj->type == kString) {
                // String natives on a temporary String object.
                std::string s = str(obj->name());
                if (m == name::kcharAt || m == name::kcharCodeAt) {
                    int i = count > 0 ? to_int(args[0]) : 0;
                    char c = i >= 0 && size_t(i) < s.size() ? s[size_t(i)] : 0;
                    if (m == name::kcharAt) argc->set_string(c ? names().intern(std::string(1, c)) : 0);
                    else argc->set_int(int32_t(int8_t(c)));
                } else if (m == name::ksubstr) {
                    int start = count > 0 ? to_int(args[0]) : 0;
                    int len = count > 1 ? to_int(args[1]) : int(s.size());
                    std::string out;
                    if (start >= 0 && size_t(start) <= s.size()) out = s.substr(size_t(start), size_t(std::max(0, len)));
                    argc->set_string(names().intern(out));
                }
            } else if (Object* o = object_of(*obj);
                       o && (obj->type == kMovieClip || obj->type == kScene || obj->type == kArray ||
                             obj->type == kSound || obj->type == kColor || obj->type == kMath || obj->type == kKey)) {
                player::MovieClip* tc = player::MovieClip::from(target());
                uint32_t generation = tc ? tc->serial : 0;
                if (set_trace()) g_called.push_back(m);
                o->call_method(*this, m, Args{count, args}, *argc, abort);
                if (set_trace()) g_called.pop_back();
                if (tc && tc->character_id != 0 && (tc->refcount == 0 || tc->serial != generation)) {
                    abort = true;
                    push_result = false;
                }
            }
        }
        if (push_result) push(argc);
        else free(argc);
        for (int i = 0; i < count; i++) free(arg_slots[i]);
        free(method);
        free(obj);
        return 0;
    }

    // ---- Members (2.12)
    case kGetMember: {
        Slot* key = pop();
        Slot* obj = pop();
        uint32_t obj_type = obj->type;
        get_member(*obj, *key, true);
        if (FILE* t = var_trace())
            std::fprintf(t, "GM %d.%s -> %s\n", int(obj_type), str(to_name(*key)).c_str(), traced(*obj).c_str());
        push(obj);
        free(key);
        return 0;
    }
    case kSetMember: {
        Slot* value = pop();
        Slot* key = pop();
        Slot* obj = pop();
        if (FILE* t = set_trace(); t && to_name(*key) == set_trace_name()) {
            auto* c = dynamic_cast<player::Character*>(object_of(*obj));
            std::fprintf(t, "%s: SM %s.%s = type %x bits %08x (%.9g)\n", g_called.empty() ? (player::MovieClip::from(target()) ? str(player::MovieClip::from(target())->path_name()).c_str() : "-") : str(g_called.back()).c_str(), c ? str(c->name).c_str() : "?",
                         str(to_name(*key)).c_str(), unsigned(value->type), unsigned(value->bits),
                         value->type == kFloat ? double(value->f()) : double(value->i()));
        }
        set_member(*obj, *key, *value);
        free(value);
        free(key);
        free(obj);
        return 0;
    }
    case kEnumerate2: {
        Slot* obj = pop();
        obj->set_undefined();
        push(obj);
        return 0;
    }

    // ---- Objects (2.13)
    case kNewObject: {
        Slot* n = pop();
        Slot* argc = pop();
        int count = argc->is_undefined() ? 0 : to_int(*argc);
        NameId cls = to_name(*n);
        auto discard = [&](int k) {
            for (int i = 0; i < k; i++) free(pop());
        };
        if (cls == name::kArray) {
            if (count == 0) {
                argc->set_undefined();
            } else if (count == 1) {
                Slot* size = pop();
                Array* a = arrays.allocate();
                if (a) a->resize(to_int(*size));
                free(size);
                argc->set_object(a);
            } else {
                Array* a = arrays.allocate();
                if (a) {
                    a->resize(count);
                    for (int i = 0; i < count; i++) {
                        Slot* e = pop();
                        a->store_element(i, *e);
                        free(e);
                    }
                }
                argc->set_object(a);
            }
        } else if (cls == name::kColor) {
            Slot* arg = pop();
            Color* c = colors.allocate();
            if (c) {
                c->reset_values();
                c->clip = player::MovieClip::from(object_of(*arg));
            }
            free(arg);
            argc->set_object(c);
        } else if (cls == name::kObject) {
            discard(count);
            argc->set_object(objects.allocate());
        } else if (cls == name::kSound) {
            discard(count);
            argc->set_object(sounds.allocate());
        } else {
            argc->set_undefined();  // the arguments stay on the stack
        }
        push(argc);
        free(n);
        return 0;
    }

    // ---- Clips and loading (2.14)
    case kCloneSprite: {
        Slot* depth = pop();
        Slot* new_name = pop();
        Slot* source = pop();
        // Unused by the game; not implemented.
        free(depth);
        free(new_name);
        free(source);
        return 0;
    }
    case kRemoveSprite: {
        Slot* tgt = pop();
        if (player::MovieClip* clip = player::MovieClip::from(object_of(*tgt)); clip && clip->depth >= 0 && clip->parent)
            clip->parent->remove_child(clip);
        free(tgt);
        return 0;
    }
    case kGetUrl2: {
        Slot* tgt = pop();
        Slot* url = pop();
        if (!url->is_undefined()) {
            std::string u = str(to_name(*url));
            if (u.empty()) {
                if (!tgt->is_undefined()) {
                    if (player::MovieClip* clip = player::MovieClip::from(object_of(*tgt))) host_.unload_movie(clip);
                }
            } else if (player::MovieClip* clip = player::MovieClip::from(object_of(*tgt))) {
                // "../player/player.swf" -> "player"
                if (u.size() > 4 && u.compare(u.size() - 4, 4, ".swf") == 0) u.resize(u.size() - 4);
                size_t slash = u.find('/');
                if (slash != std::string::npos) slash = u.find('/', slash + 1);
                if (slash != std::string::npos) u = u.substr(slash + 1);
                host_.load_movie(clip, names().intern(u));
            }
        }
        free(tgt);
        free(url);
        return 0;
    }
    case kTrace: free(pop()); return 0;

    // ---- Random numbers and time (2.15)
    case kRandomNumber: {
        Slot* max = pop();
        if (max->is_undefined()) {
            max->set_int(0);
        } else {
            uint32_t m = uint32_t(to_int(*max));
            if (m == 0) m = 1;
            if (m < 2) {
                max->set_int(0);
            } else {
                uint32_t v = rng().next();
                rng().draws++;
                max->set_int(int32_t((v & 0x7fffffffu) % m));
            }
        }
        push(max);
        return 0;
    }
    case kGetTime: {
        Slot* s = allocate();
        s->set_float(time * 1000.0f);
        push(s);
        return 0;
    }

    // ---- ConstantPool (2.16)
    case kConstantPool: {
        if (r.pool) return 0;
        auto pool = std::make_shared<ConstantPool>();
        int count = u16(body);
        size_t pos = 2;
        for (int i = 0; i < count && pos < length; i++) {
            std::string s;
            while (pos < length && body[pos]) s += char(body[pos++]);
            pos++;
            pool->ids.push_back(names().intern(s));
        }
        r.pool = pool;
        r.built = pool;
        return 0;
    }

    // ---- Fused actions (2.17)
    case kPushGetVariable: {
        size_t pos = 0;
        NameId id = decode_name(r, body, length, pos);
        Slot* s = allocate();
        lookup(id, *s);
        push(s);
        return 1;
    }
    case kPush2GetVariable: {
        size_t pos = 0;
        Slot* v = allocate();
        decode_push_item(r, body, length, pos, *v);
        push(v);
        NameId id = decode_name(r, body, length, pos);
        Slot* s = allocate();
        lookup(id, *s);
        push(s);
        return 1;
    }
    case kPushGetMember: {
        size_t pos = 0;
        Value key;
        decode_push_item(r, body, length, pos, key);
        Slot* obj = pop();
        get_member(*obj, key, false);
        push(obj);
        return 1;
    }
    case kPushSetMember: {
        Slot* obj = pop();
        size_t pos = 0;
        Value key, value;
        decode_push_item(r, body, length, pos, key);
        decode_push_item(r, body, length, pos, value);
        set_member(*obj, key, value);
        free(obj);
        return 1;
    }
    case kPushDefineLocal: {
        size_t pos = 0;
        NameId id = decode_name(r, body, length, pos);
        Value value;
        decode_push_item(r, body, length, pos, value);
        define_local(id, value);
        return 1;
    }

    default:
        return 0;  // castle.exe has no handler (a crash); skip it
    }
}

}  // namespace as
