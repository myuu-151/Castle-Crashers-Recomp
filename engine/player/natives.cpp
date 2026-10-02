// MovieClip / Scene CallMethod: the clip natives and the
// engine's global natives, then user functions. docs/engine/
// movieclips-and-frames.md section 4.

#include <SDL3/SDL_log.h>

#include <cmath>
#include <ctime>

#include "as/builtins.h"
#include "as/interpreter.h"
#include "audio/audio.h"
#include "player/game.h"
#include "player/movieclip.h"
#include "player/player.h"

namespace player {

using namespace as;

namespace {

// Native IDs beyond the Flash names (docs/engine/builtin-names.txt).
enum : NameId {
    kAddShader = 0x2d,
    kErrorNative = 0x2e,
    kGetFramerate = 0x2f,
    kIsActive = 0x30,
    kRemoveShader = 0x31,
    kSetActive = 0x32,
    kSetBackgroundAlpha = 0x33,
    kSetFramerate = 0x34,
    kFadeIn = 0x35,
    kFadeOut = 0x36,
    kGetMasterVolume = 0x37,
    kGetMusicVolume = 0x38,
    kGetSfxVolume = 0x39,
    kPauseMusic = 0x3a,
    kPlayMusic = 0x3b,
    kRegisterMusic = 0x3c,
    kSetMasterVolume = 0x3d,
    kSetMusicVolume = 0x3e,
    kSetSfxVolume = 0x3f,
    kStopMusic = 0x40,
    kUnregisterMusic = 0x41,
    kClearAllMusic = 0x42,
    kReadStorage = 0x43,
    kWriteStorage = 0x44,
    kASSERT = 0x83,
    kChangeMovie = 0x84,
    kGetCheat = 0x85,
    kGetFlashGlobal = 0x86,
    kGetGore = 0x87,
    kGetLanguage = 0x88,
    kGetLocalSecond = 0x89,
    kGetRegion = 0x8a,
    kGetTextLocalizationScale = 0x8b,
    kGetWidescreen = 0x8c,
    kIsPalMode = 0x8d,
    kLOGPush = 0x8e,
    kQuitTo = 0x8f,
    kSetFlashController = 0x91,
    kSetFlashGlobal = 0x92,
    kf_LocalToGame = 0x99,
    kPreloadNews = 0x9a,
    kf_Quantize = 0x9c,
    kGetLocalGamerTag = 0x9d,
    kIsFullGame = 0x9f,
    kFlushStats = 0xa8,
    kGetGameMode = 0xa9,
    kReadSaveGame = 0x90,
    kSetTextboxAsInteger = 0x93,
    kSetTextNumeric = 0x94,
    kVibrateController = 0x95,
    kWriteSaveGame = 0x96,
    kShowLoadIcon = 0x97,
    kHideLoadIcon = 0x98,
    kSetMarketOffer = 0x9b,
    kSetTextToGamerTag = 0x9e,
    kReturnToArcade = 0xa0,
    kRichPresence = 0xa1,
    kSetRichPresence = 0xa2,
    kSetScores = 0xa3,
    kShowSignInScreen = 0xa4,
    kUnlockAchievement = 0xa5,
    kUnlockFullGame = 0xa6,
    kUnlockAvatarItem = 0xa7,
    kIsLocalPlayerInSession = 0xaa,
    kIsPlayerSignedIn = 0xab,
    kIsPortTalking = 0xac,
    kJoinSession = 0xad,
    kLeaveSession = 0xae,
    kStartSession = 0xaf,
    kEndSession = 0xb0,
    kWriteAllStats = 0xb1,
    kIsSameProfile = 0xb2,
    kIsNetGame = 0xb3,
    kIsRankedMatch = 0xb4,
    kIsInviteWaiting = 0xb5,
    kSetPortState = 0xb6,
    kShowGamerTag = 0xb7,
    kSetKeyboardMapping = 0xb8,
    // Game natives, before the scripts' own functions.
    kf_BSPCheckLastHitIndex = 0xbc,
    kf_BSPCheckLastHitType = 0xbd,
    kf_BSPCheckLastHitSlope = 0xbe,
    kf_BSPHitTest = 0xbf,
    kf_BSPLoadLevel = 0xc0,
    kf_GetClosestWaypoint = 0xc1,
    kf_GetWPHit = 0xc2,
    kf_GetWPX = 0xc3,
    kf_GetWPY = 0xc4,
    kf_SetWPHit = 0xc5,
    kf_Depth = 0xeb,
    kf_LargeObjectRanges = 0xec,
    kShowMouse = 0xb9,
};

int root_framerate = 30;

// castle.exe's matrix in its own units: a and d scaled by 100, b and c by 20,
// tx and ty in pixels.
struct Transform {
    float a = 100.0f, d = 100.0f, b = 0.0f, c = 0.0f, tx = 0.0f, ty = 0.0f;
};

// `outer` applied after `acc`, with its operation order.
Transform combine(const Transform& acc, const Transform& outer) {
    const float k0 = 0.05f, k4 = 0.01f, hundred = 100.0f, twenty = 20.0f;
    float od4 = outer.d * k4, ob0 = outer.b * k0, ac0 = acc.c * k0, oc0 = outer.c * k0;
    float ctx = outer.c * acc.ty * k0, aa4 = acc.a * k4, btx = outer.b * acc.tx * k0;
    float ad4 = acc.d * k4, oa4 = outer.a * k4, ab0 = acc.b * k0;
    Transform r;
    r.b = (aa4 * ob0 + ab0 * od4) * twenty;
    float atx = outer.a * acc.tx * k4;
    r.c = (ad4 * oc0 + ac0 * oa4) * twenty;
    r.a = (ab0 * oc0 + aa4 * oa4) * hundred;
    r.tx = ctx + atx + outer.tx;
    float dty = outer.d * acc.ty * k4;
    r.d = (ac0 * ob0 + ad4 * od4) * hundred;
    r.ty = btx + dty + outer.ty;
    return r;
}

Transform matrix_of(const Character& c) {
    Transform m;
    m.a = c.matrix.a;
    m.d = c.matrix.d;
    m.b = c.matrix.b;
    m.c = c.matrix.c;
    m.tx = c.matrix.tx;
    m.ty = c.matrix.ty;
    return m;
}

// the clip's matrix to the stage: its ancestors' from the top
// down (those with a matrix), then its own.
Transform global_transform(const Character& clip) {
    const float k0 = 0.05f, k4 = 0.01f;
    std::vector<const Character*> chain;
    for (const Character* p = clip.parent; p; p = p->parent)
        if (p->flags5c & Character::kHasMatrix) chain.push_back(p);
    Transform r;
    for (size_t i = chain.size(); i-- > 0;) {
        Transform m = matrix_of(*chain[i]);
        float rd4 = r.d * k4, rb0 = r.b * k0, rc0 = r.c * k0;
        float ma4 = m.a * k4, mc0 = m.c * k0, md4 = m.d * k4, ra4 = r.a * k4, mb0 = m.b * k0;
        Transform n;
        n.b = (ma4 * rb0 + mb0 * rd4) * 20.0f;
        float atx = r.a * m.tx;
        n.c = (md4 * rc0 + mc0 * ra4) * 20.0f;
        n.a = (mb0 * rc0 + ma4 * ra4) * 100.0f;
        n.d = (mc0 * rb0 + md4 * rd4) * 100.0f;
        n.tx = r.c * m.ty * k0 + atx * k4 + r.tx;
        n.ty = r.b * m.tx * k0 + r.d * m.ty * k4 + r.ty;
        r = n;
    }
    if (clip.flags5c & Character::kHasMatrix) r = combine(matrix_of(clip), r);
    return r;
}

// A point object's x and y (both defined), for localToGlobal/globalToLocal.
bool read_point(Interpreter& in, Object* point, float& x, float& y) {
    Value vx, vy;
    point->get_member(in, 0x79, vx);
    point->get_member(in, 0x7c, vy);
    if (vx.is_undefined() || vy.is_undefined()) return false;
    x = to_float(vx);
    y = to_float(vy);
    return true;
}

void write_point(Interpreter& in, Object* point, float x, float y) {
    Value vx, vy;
    vx.set_float(x);
    vy.set_float(y);
    point->set_member(in, 0x79, vx);
    point->set_member(in, 0x7c, vy);
}

int frame_argument(MovieClip* clip, const Value& v) {
    if (v.type == kInt || v.type == kFloat) return to_int(v);
    return clip->label_frame(to_name(v));
}

}  // namespace

void MovieClip::call_method(Interpreter& in, NameId id, Args args, Value& result, bool& abort) {
    if (!player) return;  // kept for a reference after its movie went (~Player): it does nothing
    Game& game = player->game();
    auto arg_int = [&](int i) { return i < args.count ? to_int(args[i]) : 0; };
    auto arg_float = [&](int i) { return i < args.count && !args[i].is_undefined() ? to_float(args[i]) : 0.0f; };
    auto arg_name = [&](int i) { return i < args.count ? to_name(args[i]) : NameId(0); };
    auto arg_text = [&](int i) { return names().str(arg_name(i)); };

    in.push_target(this);
    switch (id) {
    // ---- Display list (4.1)
    case name::kattachMovie: {
        auto it = player->movie().exports.find(arg_text(0));
        MovieClip* clip = it != player->movie().exports.end() ? player->instantiate(it->second) : nullptr;
        // (logged: a clip not made leaves the script going on without it, as a
        // level's players once were, never there)
        if (!clip)
            SDL_Log("attachMovie(%s): %s in %s", arg_text(0).c_str(),
                    it == player->movie().exports.end() ? "no such export" : "no clip free", player->name().c_str());
        if (clip) {
            clip->name = arg_name(1);
            clip->depth = arg_int(2);
            clip->birth_frame = 1;
            insert_child(clip, true);
            result.set_object(clip);
        }
        break;
    }
    case name::kcreateEmptyMovieClip: {
        if (MovieClip* clip = player->new_empty_clip()) {
            clip->name = arg_name(0);
            clip->depth = arg_int(1);
            insert_child(clip, true);
            result.set_object(clip);
        }
        break;
    }
    case name::kduplicateMovieClip:
        if ((flags5c & kSprite) && parent) {
            MovieClip* copy = allocate_clip(player);
            if (copy) {
                copy_to(copy);
                copy->name = arg_name(0);
                copy->depth = arg_int(1);
                if (!parent->child_at_depth(copy->depth)) parent->insert_child(copy, false);
            }
        }
        break;  // returns nothing
    case name::kgetDepth: result.set_int(depth); break;
    case name::kremoveMovieClip: {
        MovieClip* target_clip = args.count > 0 ? MovieClip::from(object_of(args[0])) : this;
        if (target_clip && target_clip->depth >= 0 && target_clip->parent) target_clip->parent->remove_child(target_clip);
        break;
    }
    case name::kswapDepths:
        if (parent && args.count > 0) {
            if (Character* other = MovieClip::from(object_of(args[0])); other && other->parent == parent) {
                std::swap(depth, other->depth);
            } else if (args[0].type == kInt || args[0].type == kFloat) {
                int d = to_int(args[0]);
                if (d >= 0 && d != depth) {
                    if (Character* at = parent->child_at_depth(d)) at->depth = depth;
                    depth = d;
                }
            }
            parent->flags |= kResort;
        }
        break;
    case name::ksetMask:
        // setMask(clip): this clip drawn only where `clip` covers, `clip`
        // itself not drawn (MovieClip::render); setMask(undefined/null)
        // undoes it. Drawn anyway, a character's water box showed as a white
        // block for a frame (Forest Entrance) where castle.exe shows nothing.
        script_mask = args.count > 0 ? MovieClip::from(object_of(args[0])) : nullptr;
        break;
    case kAddShader: shader = uint32_t(arg_int(0)); break;
    case kRemoveShader: shader = 0; break;
    case kIsActive: result.set_bool(flags & kActive); break;
    case kSetActive:
        if (args.count > 0 && to_bool(args[0])) flags |= kActive;
        else flags &= uint8_t(~kActive);
        break;
    case kSetBackgroundAlpha: break;

    // ---- Timeline (4.2)
    case name::kgotoAndPlay:
    case name::kgotoAndStop: {
        int f = args.count > 0 ? frame_argument(this, args[0]) : 0;
        if (id == name::kgotoAndStop && (args.count == 0 || args[0].is_undefined())) f = 1;
        if (f == 0) break;
        if (f > total_frames) f = total_frames;
        if (id == name::kgotoAndPlay) {
            if (f == current_frame()) flags |= kPlaying;
            flags &= uint8_t(~kStopRequested);
        } else {
            flags |= kStopRequested;
        }
        goto_frame(f);
        break;
    }
    case name::knextFrame:
        if (current_frame() < total_frames) {
            flags |= kStopRequested;
            goto_frame(current_frame() + 1);
        }
        break;
    case name::kprevFrame:
        if (current_frame() > 1) {
            flags |= kStopRequested;
            goto_frame(current_frame() - 1);
        }
        break;
    case name::kplay: flags = uint8_t((flags & ~kStopRequested) | kPlaying); break;
    case name::kstop: flags |= kStopRequested; break;
    case name::kstart:
    case name::kunescape:
    case name::kunloadMovie:
    case kErrorNative: break;

    // ---- Geometry (4.3)
    case name::khitTest: {
        float a[4], b[4];
        if (args.count == 1) {
            MovieClip* other = MovieClip::from(object_of(args[0]));
            bool hit = other && bounds(Matrix{}, a) && other->bounds(Matrix{}, b) && a[0] <= b[2] && b[0] <= a[2] &&
                       a[1] <= b[3] && b[1] <= a[3];
            result.set_bool(hit);
        } else if (args.count == 3) {
            float x = to_float(args[0]), y = to_float(args[1]);
            bool shape = to_bool(args[2]);
            result.set_bool(!shape && bounds(Matrix{}, a) && x >= a[0] && x <= a[2] && y >= a[1] && y <= a[3]);
        } else {
            result.set_bool(false);
        }
        break;
    }

    // ---- Engine natives (4.4)
    case kChangeMovie: game.change_movie(arg_text(0)); break;
    case kQuitTo: game.quit_to(arg_text(0)); break;
    case kSetFlashGlobal: game.set_flash_global(arg_name(0), arg_int(1)); break;
    case kGetFlashGlobal: result.set_int(game.get_flash_global(arg_name(0))); break;
    case kSetFlashController: game.set_flash_controller(args.count ? arg_text(0) : "", *player); break;
    case kGetWidescreen: result.set_bool(true); break;
    case kIsPalMode: result.set_bool(false); break;
    case kGetLanguage: result.set_int(0); break;
    case kGetRegion: result.set_int(0); break;
    case kGetGore: result.set_int(1); break;
    case kGetTextLocalizationScale: result.set_int(100); break;
    case kGetLocalSecond: result.set_int(int32_t(std::time(nullptr) % 60)); break;
    case kGetCheat: result.set_bool(false); break;
    case kPreloadNews:
    case kLOGPush:
    case kFlushStats: break;
    case kASSERT:
        if (args.count > 0 && !to_bool(args[0])) SDL_Log("ASSERT: %s", arg_text(1).c_str());
        break;
    case kGetLocalGamerTag: result.set_string(names().intern(game.gamer_tag.substr(0, 48))); break;
    case kIsFullGame: result.set_bool(true); break;
    case kGetGameMode: result.set_int(game.game_mode >= 0 ? game.game_mode : game.last_game_mode); break;

    // ---- Text (0x446f7f, 0x447355): a field with a
    // localized string (ntext) keeps it.
    case kSetTextNumeric:
    case kSetTextboxAsInteger: {
        auto* field = dynamic_cast<TextField*>(args.count > 0 ? object_of(args[0]) : nullptr);
        if (field && (field->ntext == -1 || (field->ntext & 0xffff) == 0)) field->text = std::to_string(arg_int(1));
        break;
    }
    case kSetTextToGamerTag:
        for (int i = 1; i <= 2; i++) {
            auto* field = dynamic_cast<TextField*>(args.count > i ? object_of(args[i]) : nullptr);
            if (field && (field->ntext == -1 || (field->ntext & 0xffff) == 0)) field->text = game.gamer_tag.substr(0, 48);
        }
        break;

    // ---- Save and stats
    case kWriteSaveGame: result.set_bool(game.write_save(arg_int(0))); break;
    case kReadSaveGame: game.read_save(arg_int(0)); break;
    case kWriteAllStats: game.write_all_stats(); break;
    case kSetScores: {  // 0x44749c: arena and quaff counters in the save
        int port = arg_int(0), a = arg_int(1), b = arg_int(2), c = arg_int(3);
        if (port < 0 || port > 3) break;
        save::Storage& st = game.storage;
        if (game.game_mode == 3) {  // the original's
            auto character = [&] {
                int ch = game.get_flash_global(NameId(0xcf + port)) - 1;  // g_nPortNCharId
                return uint32_t(ch) < 30 ? uint32_t(ch) : 0u;
            };
            if (a > 0) {
                int am = game.get_flash_global(0xd8);  // g_nArenaMode
                uint32_t ai = uint32_t(am) < 4 ? uint32_t(am) : 0;
                uint32_t pos = 0x5e + character() * 0x30 + ai * 2;
                st.write_be(pos, 2, std::min(st.read_be(pos, 2) + 1, 0xffffu));
                int pts = game.get_flash_global(0xd3);  // g_nArenaPoints
                uint32_t add = pts < 1 ? 1 : uint32_t(std::min(pts, 3));
                pos = 0x6a + character() * 0x30;
                st.write_be(pos, 4, st.read_be(pos, 4) + add);
            }
            if (b > 0) {
                uint32_t pos = 0x66 + character() * 0x30;
                st.write_be(pos, 4, st.read_be(pos, 4) + 1);
            }
        } else if (game.game_mode == 1) {  // the original's
            if (a > 0) {
                st.write_be(0x24, 4, st.read_be(0x24, 4) + uint32_t(a));
                st.write_be(0x2c, 4, st.read_be(0x2c, 4) + uint32_t(c > 0 ? c : 1));
            }
            if (b > 0) st.write_be(0x28, 4, st.read_be(0x28, 4) + uint32_t(b));
        }
        break;
    }

    // ---- Players and sessions (offline: one Steam user, all four ports
    // signed in)
    case kIsPlayerSignedIn: result.set_bool(uint32_t(arg_int(0)) <= 3); break;
    case kIsLocalPlayerInSession: result.set_bool(game.registered(arg_int(0))); break;
    case kJoinSession: game.register_port(arg_int(0)); break;
    case kLeaveSession:
        if (args.count == 0) game.leave_game();
        else game.unregister_port(arg_int(0));
        break;
    case kIsSameProfile: {  // every local player is the same Steam user
        int a = arg_int(0), b = arg_int(1);
        bool in_range = (a >= 0 && a < 4) || uint32_t(b) <= 3;
        result.set_bool(in_range && (a == b || (game.registered(a) && game.registered(b))));
        break;
    }
    case kIsNetGame:
    case kIsRankedMatch:
    case kIsInviteWaiting:
    case kIsPortTalking: result.set_bool(false); break;
    case kShowLoadIcon: game.show_load_icon(true); break;
    case kHideLoadIcon: game.show_load_icon(false); break;
    case kReturnToArcade:  // the original's
        game.reset_game();
        game.quit();
        break;
    // Steam and Xbox only: rumble, presence, achievements, the store,
    // sign-in, gamer cards, the Steam session.
    case kVibrateController:
    case kSetMarketOffer:
    case kRichPresence:
    case kSetRichPresence:
    case kShowSignInScreen:
    case kUnlockAchievement:
    case kUnlockFullGame:
    case kUnlockAvatarItem:
    case kStartSession:
    case kEndSession:
    case kShowGamerTag: break;
    case kSetKeyboardMapping: game.input.keyboard.select(arg_text(0)); break;
    case kSetPortState: {  // 0xb6 (port, state): ports from 1; the state nibble of its device
        int port = arg_int(0) - 1;
        if (port >= 0 && port < 4) {
            uint8_t& s = game.input.devices[port].port_state;
            s = uint8_t((s & 0xf0) | (arg_int(1) & 0xf));
        }
        break;
    }
    case kShowMouse: game.cursor_shown = args.count > 0 && to_bool(args[0]); break;  // the original's
    case kGetFramerate: result.set_int(root_framerate); break;
    case kSetFramerate: root_framerate = arg_int(0); break;
    case kReadStorage: {  // the original's 0x43: (port[, position])
        // A negative port or a failed read answers 0; the cursor is shared.
        uint8_t value = 0;
        if (arg_int(0) >= 0) {
            if (args.count == 2) game.storage.seek(uint32_t(arg_int(1)));
            if (!game.storage.read(value)) value = 0;
        }
        result.set_int(value);
        break;
    }
    case kWriteStorage: {  // 0x44: (port, value) or (port, position, value)
        if (arg_int(0) < 0) break;
        uint8_t value = uint8_t(arg_int(args.count == 2 ? 1 : 2));
        if (args.count != 2) game.storage.seek(uint32_t(arg_int(1)));
        if (!game.storage.write(value)) result.set_int(0);
        break;
    }

    // ---- Music (4.5, docs/engine/audio.md 2): all return nothing but the
    // getters.
    case kRegisterMusic: audio::manager().register_music(arg_int(0), arg_text(1)); break;
    case kUnregisterMusic: audio::manager().unregister_music(arg_int(0)); break;
    case kClearAllMusic: audio::manager().clear_all_music(); break;
    case kPlayMusic:  // (id[, loop]); an undefined argument does nothing
        if ((args.count == 1 || args.count == 2) && !args[0].is_undefined() &&
            (args.count == 1 || !args[1].is_undefined()))
            audio::manager().play_music(arg_int(0), args.count == 1 || to_bool(args[1]));
        break;
    case kStopMusic: audio::manager().stop_music(); break;
    case kPauseMusic: audio::manager().pause_music(args.count > 0 && to_bool(args[0])); break;
    case kFadeIn:  // (frames, id[, loop])
        if ((args.count == 2 || args.count == 3) && !args[0].is_undefined() && !args[1].is_undefined() &&
            (args.count == 2 || !args[2].is_undefined()))
            audio::manager().fade_in(arg_int(0), arg_int(1), args.count == 2 || to_bool(args[2]));
        break;
    case kFadeOut: audio::manager().fade_out(arg_int(0), args.count > 1 && to_bool(args[1])); break;
    case kGetMasterVolume: result.set_int(audio::manager().master_volume()); break;
    case kGetMusicVolume: result.set_int(audio::manager().music_volume()); break;
    case kGetSfxVolume: result.set_int(audio::manager().sfx_volume()); break;
    case kSetMasterVolume: audio::manager().set_master_volume(arg_int(0)); break;
    case kSetMusicVolume: audio::manager().set_music_volume(arg_int(0)); break;
    case kSetSfxVolume: audio::manager().set_sfx_volume(arg_int(0)); break;

    case name::klocalToGlobal: {  // 0x22, the original's -> the original's
        Object* point = args.count > 0 ? object_of(args[0]) : nullptr;
        float x = 0, y = 0;
        if (!point || !read_point(in, point, x, y)) break;
        Transform g = global_transform(*this);
        float by = g.b * x * 0.05f, dy = g.d * y * 0.01f;
        float gx = x * g.a * 0.01f + y * g.c * 0.05f + g.tx;
        write_point(in, point, gx, dy + by + g.ty);
        break;
    }
    case name::kglobalToLocal: {  // 0x1b, the original's -> the original's
        Object* point = args.count > 0 ? object_of(args[0]) : nullptr;
        float x = 0, y = 0;
        if (!point || !read_point(in, point, x, y)) break;
        Transform g = global_transform(*this);
        float d4 = g.d * 0.01f, a4 = g.a * 0.01f, c0 = g.c * 0.05f, b0 = g.b * 0.05f;
        // The inverse's terms are products of the matrix's own (each with
        // inv), then applied to the point, groups them.
        float inv = 1.0f / (d4 * a4 - b0 * c0);
        float ia = -c0 * inv, ib = -b0 * inv, ic = inv * a4, id = inv * d4;
        float itx = (g.ty * c0 - d4 * g.tx) * inv;
        float ity = -(g.ty * a4 - b0 * g.tx) * inv;
        float lx = y * ia + x * id + itx;
        float ly = y * ic + x * ib + ity;
        write_point(in, point, lx, ly);
        break;
    }
    case kf_LocalToGame: {  // 0x99: f_LocalToGame(clip, point)
        // The point, in the clip's coordinates, into those of the nearest
        // ancestor named "game" (the clip and its ancestors below it, those
        // with a matrix), written back as floats.
        constexpr NameId kX = 0x79, kY = 0x7c, kGame = 0xe9;
        Object* clip = args.count > 0 ? object_of(args[0]) : nullptr;
        Object* point = args.count > 1 ? object_of(args[1]) : nullptr;
        if (!clip || !point) break;
        Value vx, vy;
        point->get_member(in, kX, vx);
        point->get_member(in, kY, vy);
        float x = vx.is_undefined() ? 0.0f : to_float(vx), y = vy.is_undefined() ? 0.0f : to_float(vy);
        Transform acc;
        for (Character* c = MovieClip::from(clip); c; c = c->parent) {
            if (c->name == kGame) break;
            if (c->flags5c & Character::kHasMatrix) {
                Transform m;
                m.a = c->matrix.a;
                m.d = c->matrix.d;
                m.b = c->matrix.b;
                m.c = c->matrix.c;
                m.tx = c->matrix.tx;
                m.ty = c->matrix.ty;
                acc = combine(acc, m);
            }
        }
        Value rx, ry;
        rx.set_float(acc.c * y * 0.05f + acc.a * x * 0.01f + acc.tx);
        ry.set_float(acc.d * y * 0.01f + acc.b * x * 0.05f + acc.ty);
        point->set_member(in, kX, rx);
        point->set_member(in, kY, ry);
        break;
    }
    case kf_Quantize:  // 0x9c: to the nearest 1/256
        result.set_float(float(std::floor(double(arg_float(0) * 256.0f + 0.5f)) * 0.00390625));
        break;
    // ---- Level collision and waypoints (player/bsp.h)
    case kf_BSPCheckLastHitIndex: result.set_int(game.bsp.last_index); break;
    case kf_BSPCheckLastHitType: result.set_int(game.bsp.last_type); break;
    case kf_BSPCheckLastHitSlope: result.set_float(game.bsp.last_slope); break;
    case kf_BSPHitTest: {  // (x1, y1, x2, y2); any undefined: no answer
        bool all = args.count >= 4;
        for (int i = 0; all && i < 4; i++) all = !args[i].is_undefined();
        if (all) result.set_float(game.bsp.hit_test(to_float(args[0]), to_float(args[1]), to_float(args[2]), to_float(args[3])));
        break;
    }
    case kf_BSPLoadLevel:
        if (!game.bsp.load(game.bsp_path(arg_text(0)))) SDL_Log("can't load level collision %s", arg_text(0).c_str());
        break;
    case kf_GetClosestWaypoint: result.set_int(game.bsp.closest_waypoint(arg_float(0))); break;
    case kf_GetWPHit: result.set_float(game.bsp.waypoint(arg_int(0), 2)); break;
    case kf_GetWPX: result.set_float(game.bsp.waypoint(arg_int(0), 0)); break;
    case kf_GetWPY: result.set_float(game.bsp.waypoint(arg_int(0), 1)); break;
    case kf_SetWPHit: game.bsp.waypoint(arg_int(0), 2) += 1.0f; break;

    case kf_LargeObjectRanges: {  // replaces main's script
        // The zone's hit box (its child variable "zone"): x, y, half width and
        // height from the box clip's bounds and the zone's scales (x 0.01),
        // and the four edges, all floats.
        constexpr NameId kX = 0x79, kY = 0x7c, kTop = 0xf3, kBottom = 0xf4, kLeft = 0xf5, kRight = 0xf6, kW = 0xf7,
                         kH = 0xf8, kZone = 0xf9;
        MovieClip* zone = args.count > 0 ? MovieClip::from(object_of(args[0])) : nullptr;
        if (!zone) break;
        Value box_value;
        zone->get_member(in, kZone, box_value);
        MovieClip* box = MovieClip::from(object_of(box_value));
        if (box_value.is_undefined() || !box) break;
        float xs = std::fabs(zone->xscale()) * 0.01f;
        float ys = std::fabs(zone->yscale()) * 0.01f;
        Value v;
        float x = 0.0f, y = 0.0f;
        if (zone->get_member(in, kX, v); !v.is_undefined()) x = to_float(v);
        x = zone->xscale() <= 0.0f ? x - box->matrix.tx : box->matrix.tx + x;
        v = Value{};
        if (zone->get_member(in, kY, v); !v.is_undefined()) y = to_float(v);
        y = box->matrix.ty + y;
        Value hv, wv;
        box->get_member(in, name::k_height, hv);
        box->get_member(in, name::k_width, wv);
        float h = to_float(hv) * 0.5f * ys;
        float w = to_float(wv) * 0.5f * xs;
        auto put = [&](NameId n, float f) {
            Value o;
            o.set_float(f);
            box->set_member(in, n, o);
        };
        put(kX, x);
        put(kY, y);
        put(kW, w);
        put(kH, h);
        put(kTop, y - h);
        put(kBottom, h + y);
        put(kRight, w + x);
        put(kLeft, x - w);
        break;
    }
    case kf_Depth: {  // replaces main's f_Depth(zone, u_depth)
        // current_depth = (2000 - int(abs_bottom._y - int(u_depth))) * 1000
        // + depth_mod, with abs_bottom found by name under
        // _root.loader.game.game; without it nothing happens.
        constexpr NameId kLoader = 0xee, kGame = 0xe9, kCurrentDepth = 0xf0, kDepthMod = 0xf1, kAbsBottom = 0xf2;
        if (args.count < 2 || args[0].is_undefined() || args[1].is_undefined()) break;
        Object* zone = object_of(args[0]);
        if (!zone) break;
        int u_depth = to_int(args[1]);
        MovieClip* game_clip = nullptr;
        if (MovieClip* loader = in.root ? in.root->child_named(kLoader) : nullptr)
            if (MovieClip* outer = loader->child_named(kGame)) game_clip = outer->child_named(kGame);
        Value mod;
        zone->get_member(in, kDepthMod, mod);
        int depth_mod = mod.is_undefined() ? 0 : to_int(mod);
        Character* bottom = nullptr;
        for (Character* c = game_clip ? game_clip->first_child : nullptr; c; c = c->next)
            if (c->name == kAbsBottom) {
                bottom = c;
                break;
            }
        if (!bottom) break;
        int depth = (2000 - int(bottom->matrix.ty - float(u_depth))) * 1000 + depth_mod;
        Value v;
        v.set_int(depth);
        zone->set_member(in, kCurrentDepth, v);
        // swapDepths(depth) on the zone.
        if (MovieClip* z = MovieClip::from(zone); z && z->parent && depth >= 0 && depth != z->depth) {
            if (Character* at = z->parent->child_at_depth(depth)) at->depth = z->depth;
            z->depth = depth;
            z->parent->flags |= kResort;
        }
        break;
    }

    default:
        // An engine native not ported yet: said once.
        if ((id >= 0x2d && id <= 0x44) || (id >= 0x83 && id <= 0xb9)) {
            static bool said[0x100] = {};
            if (!said[id]) {
                said[id] = true;
                SDL_Log("native %s (0x%x) not ported", names().str(id).c_str(), unsigned(id));
            }
        }
        // Not a native: a function defined on this clip's timeline, or one
        // stored in a variable.
        in.call_function(runner, id, args, result, abort);
        break;
    }
    in.pop_target();
}

}  // namespace player
