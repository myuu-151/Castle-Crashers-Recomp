#include "as/objects.h"

#include <cmath>
#include <cstring>
#include <limits>

#include "as/builtins.h"
#include "as/interpreter.h"
#include "as/rng.h"
#include "audio/audio.h"
#include "player/movieclip.h"

namespace as {

// ---- Function

void Function::on_last_release() {
    props.clear();
    for (auto& p : params) p.reset();
    param_names.fill(0);
    param_count = 0;
    code = {};
    defining_clip = nullptr;
    pool.reset();
    name = 0;
}

void Runner::release_functions() {
    functions.for_each([](NameId, Function*& f) {
        release(kFunction, f);
        f = nullptr;
    });
    functions.clear();
}

// ---- Array

void Array::resize(int length) {
    for (auto& e : elements) release(e);
    elements.assign(size_t(std::max(0, std::min(length, 0xFFFF))), Value{});
}

void Array::on_last_release() {
    props.clear();
    for (auto& e : elements) release(e);
    elements.clear();
}

bool Array::get_member(Interpreter&, NameId id, Value& out) {
    if (id == name::klength) {
        out.set_int(length());
        return true;
    }
    return false;
}

void Array::clear_element(int i) {
    if (i < 0 || i >= length()) return;
    Value& e = elements[size_t(i)];
    release(e);
    e.set_undefined();
}

void Array::store_element(int i, const Value& v) {
    if (i < 0) return;
    if (i >= length()) elements.resize(size_t(i) + 1);
    clear_element(i);
    if ((v.type & kCounted) && v.obj && (v.obj->type() & kCounted)) v.obj->refcount++;
    Value& e = elements[size_t(i)];
    e.type = v.type;
    e.bits = v.bits;
    e.obj = v.obj;
}

void Array::call_method(Interpreter&, NameId id, Args args, Value& result, bool&) {
    if (id == name::klength) {
        result.set_int(length());
        return;
    }
    if (id != name::ksplice || args.count < 2) return;  // push, pop, ...: nothing
    int start = to_int(args[0]);
    if (args.count == 2) {
        // copies deleteCount + 1 elements down, not the whole
        // tail, then shortens the array.
        int del = to_int(args[1]);
        for (int i = 0; i <= del && del >= 0; i++) {
            int dst = start + i, src = start + i + del;
            Value v = src >= 0 && src < length() ? elements[size_t(src)] : Value{};
            store_element(dst, v);
        }
        int new_length = std::max(0, length() - del);
        for (int i = new_length; i < length(); i++) clear_element(i);
        elements.resize(size_t(new_length));
    } else if (args.count == 3) {
        // Shift right within the fixed length (the last element is lost),
        // then store the item at `start`.
        for (int i = length() - 1; start < i; i--) store_element(i, elements[size_t(i - 1)]);
        store_element(start, args[2]);
    }
}

// ---- Sound

void Sound::on_last_release() {
    // a kept (looping) voice stops with its last reference.
    if (voice >= 0) audio::manager().stop_loop(int16_t(voice));
    props.clear();
    sound = 0;
    attached = false;
    voice = -1;
    volume = 1.0f;
    pan = 0.0f;
}

void Sound::call_method(Interpreter&, NameId id, Args args, Value& result, bool&) {
    switch (id) {
    case name::kattachSound:
        // Loaded on first use in the movie updating now; attached even if
        // the file is missing.
        if (args.count > 0) {
            sound = to_name(args[0]);
            audio::manager().attach(sound, names().str(sound));
            attached = true;
        }
        break;
    case name::kstart: {
        // start([offset, loops]): loops over 1 (as unsigned) loop forever;
        // a looping sound is kept, so it can be stopped and retuned.
        int loops = 1;
        if (args.count == 2) {
            (void)to_float(args[0]);
            loops = to_int(args[1]);
        }
        if (!attached) break;
        bool loop = uint32_t(loops) > 1;
        if (loop && voice >= 0) {
            audio::manager().stop_loop(int16_t(voice));
            voice = -1;
        }
        int16_t v = audio::manager().play(sound, volume, pan, loop);
        if (loop && v >= 0) voice = v;
        break;
    }
    case name::kstop:
        if (voice >= 0) audio::manager().stop_loop(int16_t(voice));
        voice = -1;
        break;
    case name::kgetVolume: result.set_int(int32_t(std::trunc(volume * 100.0f))); break;
    case name::kgetPan: result.set_int(int32_t(std::trunc(pan * 100.0f))); break;
    case name::ksetVolume:
        if (args.count > 0) {
            volume = std::clamp(to_float(args[0]) * 0.01f, 0.0f, 1.0f);
            if (voice != 0) audio::manager().set_voice(int16_t(voice), volume, pan);
        }
        break;
    case name::ksetPan:
        if (args.count > 0) {
            pan = std::clamp(to_float(args[0]) * 0.01f, -1.0f, 1.0f);
            if (voice >= 0) audio::manager().set_voice(int16_t(voice), volume, pan);
        }
        break;
    default: break;
    }
}

// ---- Color

void Color::reset_values() {
    for (int i = 0; i < 4; i++) {
        mul[i] = 255;
        add[i] = 0;
    }
}

void Color::on_last_release() {
    props.clear();
    reset_values();
    clip = nullptr;
}

void Color::apply_to_clip() {
    auto* c = player::MovieClip::from(clip);
    if (!c || c->cxform == this) return;
    if (!c->cxform) {
        // the clip gets its own transform from the pool.
        return;  // created by the caller (needs the interpreter's pool)
    }
    for (int i = 0; i < 4; i++) {
        c->cxform->mul[i] = mul[i];
        c->cxform->add[i] = add[i];
    }
}

void Color::call_method(Interpreter& in, NameId id, Args args, Value&, bool&) {
    auto link = [&] {
        auto* c = player::MovieClip::from(clip);
        if (c && !c->cxform) {
            c->cxform = in.colors.allocate();
            if (c->cxform) {
                c->cxform->reset_values();
                c->cxform->refcount++;
            }
        }
        apply_to_clip();
    };
    if (id == name::ksetRGB) {  // 0x4a
        if (args.count > 0 && !args[0].is_undefined()) {
            int32_t rgb = to_int(args[0]);
            mul[0] = int16_t((rgb >> 16) & 0xFF);
            mul[1] = int16_t((rgb >> 8) & 0xFF);
            mul[2] = int16_t(rgb & 0xFF);
            if (clip) link();
        }
    } else if (id == name::ksetTransform) {  // 0x4b, the original's
        if (args.count == 0) return;
        Object* o = object_of(args[0]);
        if (!o) return;
        auto read = [&](NameId n, Value& v) {
            v = Value{};
            o->get_member(in, n, v);
        };
        auto percent = [&](NameId n) -> int16_t {
            Value v;
            read(n, v);
            return v.is_undefined() ? 0 : int16_t(int32_t(to_float(v) * 2.55f));
        };
        auto offset = [&](NameId n) -> int16_t {
            Value v;
            read(n, v);
            return v.is_undefined() ? 0 : int16_t(to_int(v));
        };
        mul[0] = percent(name::kra);
        mul[1] = percent(name::kga);
        mul[2] = percent(name::kba);
        mul[3] = percent(name::kaa);
        add[0] = offset(name::krb);
        add[1] = offset(name::kgb);
        add[2] = offset(name::kbb);
        add[3] = offset(name::kab);
        for (int i = 0; i < 4; i++)
            if (mul[i] < 0) mul[i] = 0;
        if (clip) link();
    }
}

// ---- Math

void MathObject::call_method(Interpreter&, NameId id, Args args, Value& result, bool&) {
    auto arg = [&](int i) { return i < args.count ? to_float(args[i]) : 0.0f; };
    auto trunc_int = [](float v) { return truncate_sse(v); };
    switch (id) {
    case name::kabs:
        // x < 0 ? -x : x, which keeps -0 (fabs would not).
        if (args.count > 0 && (args[0].type == kInt || args[0].type == kFloat)) {
            // The sign bit flipped only below zero, spelled out so the
            // compiler can't make it fabs.
            float x = arg(0);
            if (0.0f > x) {
                uint32_t bits;
                std::memcpy(&bits, &x, 4);
                bits ^= 0x80000000u;
                std::memcpy(&x, &bits, 4);
            }
            result.set_float(x);
        }
        else result.set_float(0.0f);
        break;
    case name::kacos: {
        // The C runtime's x87 acos answers the negative "indefinite" NaN.
        float r = std::acos(arg(0));
        if (std::isnan(r)) r = -std::numeric_limits<float>::quiet_NaN();
        result.set_float(r);
        break;
    }
    case name::katan: result.set_float(std::atan(arg(0))); break;
    case name::katan2: result.set_float(float(std::atan2(double(arg(0)), double(arg(1))))); break;
    case name::kceil: {
        float v = arg(0);
        if (v == 0) result.set_int(0);
        else if (!(v > 0)) result.set_int(trunc_int(v));  // also NaN
        else result.set_int(trunc_int(v) + (v - float(trunc_int(v)) == 0.0f ? 0 : 1));
        break;
    }
    case name::kcos: result.set_float(std::cos(arg(0))); break;
    case name::kfloor: {
        float v = arg(0);
        if (v == 0) result.set_int(0);
        else if (!(v < 0)) result.set_int(trunc_int(v));  // also NaN
        else result.set_int(trunc_int(v) - (v - float(trunc_int(v)) == 0.0f ? 0 : 1));
        break;
    }
    case name::kmin: {
        // The second argument on a tie or a NaN.
        float a = arg(0), b = arg(1);
        result.set_float(!(b > a) ? b : a);
        break;
    }
    case name::kmax: {
        float a = arg(0), b = arg(1);
        result.set_float(!(a > b) ? b : a);
        break;
    }
    case name::kPI: result.set_float(3.14159274f); break;
    case name::kpow: {
        float x = arg(0), r = 1.0f;
        int n = args.count > 1 ? to_int(args[1]) : 0;
        for (int i = 0; i < n; i++) r *= x;
        result.set_float(n <= 0 ? 1.0f : r);
        break;
    }
    case name::krandom: {
        uint32_t x = rng().next() & 0x7fffffffu;
        rng().draws++;
        result.set_float(float(double(x) / 2147483647.0));
        break;
    }
    case name::kround: {
        // Zero or more (or NaN): up from a fraction of 0.5 (or NaN); below
        // zero, truncated (-1.5 is -1).
        float v = arg(0);
        if (v == 0) {
            result.set_int(0);
            break;
        }
        int32_t i = trunc_int(v);
        float frac = v - float(i);
        if (!(0.0f > v)) {
            if (!(frac < 0.5f)) i++;
        } else if (frac > 0.5f) {
            i--;
        }
        result.set_int(i);
        break;
    }
    case name::ksin: result.set_float(std::sin(arg(0))); break;
    case name::ksqrt: result.set_float(std::sqrt(arg(0))); break;
    default: break;
    }
}

// ---- Key

bool KeyObject::get_member(Interpreter&, NameId id, Value& out) {
    switch (id) {
    case name::kDown: out.set_int(40); return true;
    case name::kLeft: out.set_int(37); return true;
    case name::kRight: out.set_int(39); return true;
    case name::kUp: out.set_int(38); return true;
    default: return false;
    }
}

void KeyObject::call_method(Interpreter& in, NameId id, Args args, Value& result, bool&) {
    if (id == name::kisDown) {
        result.set_bool(args.count > 0 && in.host().key_down(to_int(args[0])));
        if (std::FILE* t = var_trace(); t && result.b()) std::fprintf(t, "KEY %d down\n", to_int(args[0]));
    }
}

}  // namespace as
