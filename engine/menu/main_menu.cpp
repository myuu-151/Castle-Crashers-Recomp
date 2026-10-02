// MainMenu (vtable 0x49c9b0): the title screen's menus, built by
// SetFlashController("menu"). Every page is built up front and
// attaches its clips then, hidden; changing page shows the new page's items.
//
// Page numbers are castle.exe's (the index into the 41 pages at +0x40).

#include "menu/main_menu.h"

#include <SDL3/SDL_log.h>

#include <cmath>

#include "audio/audio.h"
#include "input/input.h"
#include "player/game.h"
#include "player/movieclip.h"
#include "player/player.h"

namespace menu {

namespace {

constexpr float kW = 848.0f;  // the original's +0xc
constexpr float kH = 480.0f;  // the original's +0x10

MainMenu& M(BaseMenu& b) { return static_cast<MainMenu&>(b); }

void todo(const char* what) { SDL_Log("menu: %s not ported yet", what); }

}  // namespace

// ---- Building blocks

TextItem* BaseMenu::text(Page& p, int id, Callback cb, int max, bool hit, float x, float y) {
    auto item = std::make_unique<TextItem>(this, cb, id, max, hit);
    TextItem* t = item.get();
    p.owned.push_back(std::move(item));
    p.add(t, x, y);
    return t;
}

TitleItem* BaseMenu::title(Page& p, int id, float x, float y) {
    auto item = std::make_unique<TitleItem>(id);
    TitleItem* t = item.get();
    p.owned.push_back(std::move(item));
    p.add(t, x, y);
    return t;
}

ButtonItem* BaseMenu::button(Page& p, int kind) {
    auto item = std::make_unique<ButtonItem>(*this, kind);
    ButtonItem* b = item.get();
    p.owned.push_back(std::move(item));
    p.add(b);
    return b;
}

GraphicItem* BaseMenu::graphic(Page& p, const char* name, int depth, float x, float y) {
    auto item = std::make_unique<GraphicItem>(*this);
    GraphicItem* g = item.get();
    g->attach(name, depth, x, y);
    p.owned.push_back(std::move(item));
    p.add(g);
    return g;
}

ToggleItem* BaseMenu::toggle(Page& p, Callback cb, int id, std::initializer_list<int> options, float x, float y) {
    auto item = std::make_unique<ToggleItem>(this, cb, id);
    ToggleItem* t = item.get();
    for (int o : options) t->add_option(o);
    p.owned.push_back(std::move(item));
    p.add(t, x, y);
    return t;
}

// ---- Callbacks shared by many pages

// back to the page's return page.
static void go_back(BaseMenu& m) { m.set_page(m.current->return_page); }


static void go_back_after_error(BaseMenu& m) { m.set_page(m.current->return_page); }

static bool port_signed_in(BaseMenu& m) {
    int port = m.current->port;
    return port >= 0 && port < 5 && (m.online.signed_in & (1u << port));
}

// ---- Page callbacks

// Help & Options.
static void open_help(BaseMenu& m) {
    if (port_signed_in(m)) m.set_page(5);
}
// How to Play.
static void open_how_to_play(BaseMenu& m) {
    if (port_signed_in(m)) m.set_page(6);
}
// Controls.
static void open_controls(BaseMenu& m) {
    if (port_signed_in(m)) m.set_page(8);
}
// Credits.
static void open_credits(BaseMenu& m) {
    if (port_signed_in(m)) m.set_page(10);
}
// Settings.
static void open_settings(BaseMenu&) { todo("Settings"); }
// Keyboard settings.
static void open_keyboard(BaseMenu& m) {
    if (port_signed_in(m)) {
        m.set_page(16);
        todo("keyboard settings refresh");
    }
}
static void how_to_play_topic(BaseMenu&) { todo("How to Play topic"); }
static void credits_update(BaseMenu&) {}  // fills and scrolls the credits
static void message_enter(BaseMenu&) {}
static void message_update(BaseMenu&) {}  // the message's text
static void unlock_full_game(BaseMenu&) { todo("Unlock Full Game"); }
static void achievements(BaseMenu&) { todo("Achievements overlay"); }
static void download_content(BaseMenu&) { todo("Download Content"); }
static void unlock_check(BaseMenu&) {}
static void please_wait_update(BaseMenu&) {}

// Local Game -> the mode book (page 20), opened at the page of
// the last choice.
static void local_game(BaseMenu& b) {
    MainMenu& m = M(b);
    if (!port_signed_in(m)) return;
    m.set_page(20);
    player::MovieClip* hand = m.mode_book_hand();
    m.game.set_flash_global(m.mode_global(), 1);
    static const uint16_t kFrames[3] = {0x3f, 0x10, 0x20};
    uint16_t f = kFrames[m.current->selected < 3 ? m.current->selected : 0];
    if (f && hand) {
        if (hand->total_frames < f) f = hand->total_frames;
        hand->flags |= player::MovieClip::kStopRequested;
        hand->goto_frame(f);
    }
}

// gotoAndPlay on a clip (the frame clamped to the clip's).
static void goto_and_play(player::MovieClip* c, int f) {
    if (!c) return;
    if (c->total_frames < f) f = c->total_frames;
    if (f == c->frame) c->flags |= player::MovieClip::kPlaying;
    c->flags &= uint8_t(~player::MovieClip::kStopRequested);
    c->goto_frame(f);
}

// start a local game: Arcade
// (mode 0), mode 3, or All You Can Quaff (mode 1, its own movie).
static void start_local(MainMenu& m, uint16_t mode, as::NameId flag, const char* movie) {
    int port = m.current->port;
    m.game.set_flash_global(0xcb, port + 1);
    m.game.set_flash_global(0xcc, 0);
    m.game.set_flash_global(0xcd, 0);
    m.game.set_flash_global(0xce, 0);
    m.game.set_flash_global(flag, 1);
    m.mode = mode;
    // the original's starts the local game; the music fades out over 3 s.
    m.game.start_local_game(port, mode);
    audio::manager().menu_fade_out(false);
    m.game.change_movie(movie);
}

// the mode book. Its pages turn by the "hand" clip's
// animation; each page reads the buttons of any player.
static void mode_update(BaseMenu& b) {
    MainMenu& m = M(b);
    Page& p = *m.current;
    if (!p.input_on) return;
    player::MovieClip* hand = m.mode_book_hand();
    int f = hand && hand->current_frame() ? hand->current_frame() : 1;
    using F = input::FlashPad;
    auto pressed = [&](int button) {
        int16_t saved = p.input_port;
        p.input_port = -2;
        bool hit = m.port_button(p, button, false);
        p.input_port = saved;
        return hit;
    };
    auto select = [&](uint16_t i) {
        p.items[p.selected]->color = 0x777777ff;
        p.selected = i;
        p.items[i]->color = 0xffff00ff;
    };
    auto accept = [&](uint16_t mode, bool check_steam, void (*start)(MainMenu&)) {
        (void)check_steam;  // Steam's app ID is always right here
        int port = m.current->port;
        if (port < 0 || port > 4 || !(m.online.signed_in & (1u << port))) return;
        m.mode = mode;
        if (m.online.can_play_online & (1u << p.port)) start(m);
        else m.set_page(21);
    };
    auto arcade = [](MainMenu& mm) { start_local(mm, 0, 0xe4, "main.cok6"); };
    auto mode3 = [](MainMenu& mm) { start_local(mm, 3, 0xe5, "main.cok6"); };
    auto quaff = [](MainMenu& mm) { start_local(mm, 1, 0xe5, "quoff.cok6"); };
    switch (p.selected) {
    case 0:
        if (!(f == 1 || (f >= 0x4e && f <= 0x52) || (f >= 0x3c && f <= 0x3f))) return;
        if (pressed(F::kLeft)) {
            select(1);
            goto_and_play(hand, 2);
        } else if (pressed(F::kAccept) || pressed(F::kStartButton)) {
            accept(0, false, arcade);
        }
        break;
    case 1:
        if (!((f >= 0xd && f <= 0x10) || (f >= 0x29 && f <= 0x30))) return;
        if (pressed(F::kUp)) {
            select(2);
            goto_and_play(hand, 0x11);
        } else if (pressed(F::kRight)) {
            select(0);
            goto_and_play(hand, 0x31);
        } else if (pressed(F::kAccept) || pressed(F::kStartButton)) {
            accept(3, true, mode3);
        }
        break;
    case 2:
        if (!((f >= 0x19 && f <= 0x20) || f >= 0x63)) return;
        if (pressed(F::kDown)) {
            select(1);
            goto_and_play(hand, 0x21);
        } else if (pressed(F::kRight)) {
            select(0);
            goto_and_play(hand, 0x40);
        } else if (pressed(F::kAccept) || pressed(F::kStartButton)) {
            accept(1, true, quaff);
        }
        break;
    default: break;
    }
}

// The mode book's mouse regions: the pointer over a page
// turns the book to it (as Left/Right/Up/Down do), a release on it starts that
// mode (as Accept does). Book frames as in mode_update.
static bool frames(int f, std::initializer_list<std::pair<int, int>> ranges) {
    for (auto [lo, hi] : ranges)
        if (f >= lo && f <= hi) return true;
    return false;
}

static void mode_select(MainMenu& m, uint16_t i, int frame) {
    Page& p = *m.current;
    p.items[p.selected]->color = 0x777777ff;
    p.selected = i;
    p.items[i]->color = 0xffff00ff;
    goto_and_play(m.mode_book_hand(), frame);
}

// over the top left page (All You Can Quaff).
static void region_quaff_hover(BaseMenu& b) {
    MainMenu& m = M(b);
    if (!m.current->input_on) return;
    player::MovieClip* hand = m.mode_book_hand();
    if (!hand) return;
    int f = hand->frame ? hand->frame : 1;
    uint16_t sel = m.current->selected;
    if (sel == 0 && frames(f, {{1, 1}, {0x4e, 0x52}, {0x3c, 0x3f}})) mode_select(m, 2, 0x53);
    else if (sel == 1 && frames(f, {{0xd, 0x10}, {0x29, 0x30}})) mode_select(m, 2, 0x11);
}

// over the bottom left page (Arena).
static void region_arena_hover(BaseMenu& b) {
    MainMenu& m = M(b);
    if (!m.current->input_on) return;
    player::MovieClip* hand = m.mode_book_hand();
    if (!hand) return;
    int f = hand->frame ? hand->frame : 1;
    uint16_t sel = m.current->selected;
    if (sel == 0 && frames(f, {{1, 1}, {0x4e, 0x52}, {0x3c, 0x3f}})) mode_select(m, 1, 2);
    else if (sel == 2 && (frames(f, {{0x19, 0x20}}) || f > 0x62)) mode_select(m, 1, 0x21);
}

// over the right page (Arcade).
static void region_arcade_hover(BaseMenu& b) {
    MainMenu& m = M(b);
    if (!m.current->input_on) return;
    player::MovieClip* hand = m.mode_book_hand();
    if (!hand) return;
    int f = hand->frame ? hand->frame : 1;
    uint16_t sel = m.current->selected;
    if (sel == 1 && !frames(f, {{0xd, 0x10}, {0x29, 0x30}})) return;
    if (sel == 2 && !(frames(f, {{0x19, 0x20}}) || f >= 99)) return;
    if (sel != 1 && sel != 2) return;
    mode_select(m, 0, 0x31);
}

// released over a page, on its
// turned-to frames, the local book starts that mode (the online book's
// branch is online only).
static void region_start(MainMenu& m, uint16_t mode, void (*start)(MainMenu&)) {
    if (m.current_index != 20) return;
    int port = m.current->port;
    if (port < 0 || port > 4 || !(m.online.signed_in & (1u << port))) return;
    m.mode = mode;
    if (m.online.can_play_online & (1u << port)) start(m);
    else m.set_page(21);
}
static void region_quaff_up(BaseMenu& b) {
    MainMenu& m = M(b);
    player::MovieClip* hand = m.mode_book_hand();
    if (!m.current->input_on || !hand) return;
    int f = hand->frame;
    if (f != 0 && (frames(f, {{0x19, 0x20}}) || f > 0x62))
        region_start(m, 1, [](MainMenu& mm) { start_local(mm, 1, 0xe5, "quoff.cok6"); });
}
static void region_arena_up(BaseMenu& b) {
    MainMenu& m = M(b);
    player::MovieClip* hand = m.mode_book_hand();
    if (!m.current->input_on || !hand) return;
    int f = hand->frame;
    if (f != 0 && frames(f, {{0xd, 0x10}, {0x29, 0x30}}))
        region_start(m, 3, [](MainMenu& mm) { start_local(mm, 3, 0xe5, "main.cok6"); });
}
static void region_arcade_up(BaseMenu& b) {
    MainMenu& m = M(b);
    player::MovieClip* hand = m.mode_book_hand();
    if (!m.current->input_on || !hand) return;
    int f = hand->frame;
    if (frames(f, {{0, 1}, {0x4e, 0x52}, {0x3c, 0x3f}}))
        region_start(m, 0, [](MainMenu& mm) { start_local(mm, 0, 0xe4, "main.cok6"); });
}

static void online_game(BaseMenu& m) {
    if (MainMenu::on_online) MainMenu::on_online(m);
    else todo("Online Multiplayer");
}
// ---- Player Statistics (page 22)

// the original's after "stop": gotoAndStop, the frame clamped.
static void goto_and_stop(player::MovieClip* c, int f) {
    if (!c) return;
    c->flags |= player::MovieClip::kStopRequested;
    c->goto_frame(uint16_t(std::min(f, int(c->total_frames))));
}

static player::MovieClip* child(player::MovieClip* c, const char* name) {
    return c ? c->child_named(as::names().intern(name)) : nullptr;
}

// the original's on a named text field: its text, unless it
// shows a localized string.
static void set_field(player::MovieClip* parent, const char* name, const std::string& text) {
    if (!parent) return;
    as::NameId n = as::names().intern(name);
    for (player::Character* c = parent->first_child; c; c = c->next) {
        if (c->name != n) continue;
        if (auto* t = dynamic_cast<player::TextField*>(c); t && (t->ntext == -1 || (t->ntext & 0xffff) == 0))
            t->text = text;
        return;
    }
}

// an int variable on a clip.
static void set_var(player::MovieClip* c, const char* name, int32_t v) {
    if (!c) return;
    as::Value value;
    value.set_int(v);
    c->props.get_or_add(as::names().intern(name)).store(value);
}

static player::MovieClip* stats_book(BaseMenu& m) {
    Page& p = *m.current;
    return p.clips.empty() ? nullptr : static_cast<GraphicItem*>(p.clips[0])->clip;
}

// the book's pages for the character shown, from the save:
// gold, level, experience (and its bar), weapon, animal and the four
// stats, each read at the shared storage cursor.
static void fill_stats(MainMenu& m) {
    player::MovieClip* book = stats_book(m);
    player::MovieClip* text = child(book, "textstuff");
    player::MovieClip* shadow = child(book, "textstuffbg");
    uint32_t index = m.stats_index;
    set_var(book, "portraitnum", int32_t(index + 1));
    std::string tag = m.game.gamer_tag.substr(0, 48);
    set_field(text, "txtGamerTag", tag);
    set_field(shadow, "txtGamerTag", tag);
    save::Storage& st = m.game.storage;
    uint32_t record = index * 0x30;
    auto both = [&](const char* name, const std::string& s) {
        set_field(text, name, s);
        set_field(shadow, name, s);
    };
    auto next = [&] {
        uint8_t b = 0;
        st.read(b);
        return b;
    };
    both("txtGold", std::to_string(int32_t(st.read_be(record + 0x53, 4))));
    st.seek(record + 0x40);
    next();                            // the record's flags
    uint8_t level = uint8_t(next() + 1);
    both("txtLevelNum", std::to_string(level));
    uint32_t xp = 0;
    for (int i = 0; i < 4; i++) xp = (xp << 8) | next();
    set_var(book, "expnum", int32_t(xp));
    // The bar: how far into the level, in percent (1 to 100).
    int32_t top = int32_t((level + 18) * level * 10);
    uint32_t base = uint32_t(top + (level * 5 - 5) * -4 - 190);
    double into = double(int64_t(int32_t(xp)) - int64_t(base));
    double span = double(uint32_t(top) - base);
    uint32_t percent = uint32_t(int64_t(std::nearbyint(into / span * 100.0)));
    if (percent == 0) percent = 1;
    else if (percent > 100) percent = 100;
    goto_and_stop(child(book, "xp"), int(percent));
    set_var(book, "weaponnum", uint8_t(next() - 1));
    set_var(book, "animalnum", uint8_t(next() + 1));
    uint8_t strength = next();
    both("txtStrength", std::to_string(strength));
    uint8_t defense = next();
    both("txtDefense", std::to_string(defense));
    both("txtHealthNum", std::to_string((level + 0x17) * 3 + defense * 0x1c));
    both("txtMagic", std::to_string(next()));
    both("txtAgility", std::to_string(next()));
}

// Player Statistics, from the first character.
static void player_stats(BaseMenu& b) {
    MainMenu& m = M(b);
    int port = m.current->port;
    if (port < 0 || port >= 5 || !(m.online.signed_in & (1u << port))) return;
    m.stats_port = uint16_t(port);
    m.stats_index = 0;
    m.set_page(22);
    Page& p = *m.current;
    player::MovieClip* book = stats_book(m);
    goto_and_play(book, 2);
    for (const char* name : {"areyousure", "portrait"}) {
        player::MovieClip* c = child(book, name);
        if (c) goto_and_stop(c, c->total_frames != 0 ? 1 : 0);
    }
    for (int i = 0; i < 3; i++) p.buttons[size_t(i)]->flag2b = true;
    p.buttons[3]->set_shown(false);
    p.buttons[4]->set_shown(false);
    p.buttons[2]->set_shown(true);
    fill_stats(m);
}

// Left and Right turn to the previous or next unlocked
// character (record flags exactly 0x80), unless the "are you sure" box is up.
static void stats_update(BaseMenu& b) {
    MainMenu& m = M(b);
    Page& p = *m.current;
    if (m.current_index == 15) return;
    player::MovieClip* book = stats_book(m);
    player::MovieClip* sure = child(book, "areyousure");
    if (!sure || (sure->frame != 0 && sure->frame != 1)) return;
    player::MovieClip* turn = p.clips.size() > 2 ? static_cast<GraphicItem*>(p.clips[2])->clip : nullptr;
    save::Storage& st = m.game.storage;
    auto unlocked = [&](uint16_t i) { return st.read_be(i * 0x30 + 0x40, 1) == 0x80; };
    using F = input::FlashPad;
    if (m.port_button(p, F::kRight, false)) {
        uint16_t i = m.stats_index;
        do {
            if (++i > 29) i = 0;
        } while (!unlocked(i));
        m.stats_index = i;
        goto_and_play(turn, 2);
        fill_stats(m);
        goto_and_play(book, 2);
    } else if (m.port_button(p, F::kLeft, false)) {
        uint16_t i = m.stats_index;
        do {
            if (i == 0) i = 30;
            i--;
        } while (!unlocked(i));
        m.stats_index = i;
        goto_and_play(turn, 18);
        fill_stats(m);
        goto_and_play(book, 18);
    }
}
static void leaderboards(BaseMenu&) { todo("Leaderboards"); }
// Quit. The full game (Steam's app ID matches) quits; a demo
// would go to the "upsell" movie.
static void quit_game(BaseMenu& m) { m.game.quit(); }
static void main_update(BaseMenu&) {}  // the store item's text
static void mode_region(BaseMenu&) {}  // the original's & co: mouse over the book

// ---- MainMenu

MainMenu::MainMenu(player::Game& g, player::Player& m) : BaseMenu(g, m) {
    pages.resize(size_t(41 + platform_pages));
    for (Page& p : pages) p.ctx = nullptr;
    build_0_17();
    build_19(page(19));
    build_18(page(18));
    build_20(page(20));
    build_21(page(21));
    build_22(page(22));
    {
        Page& p = page(26);  // "PLEASE WAIT"
        text(p, 0x17d);
        p.ctx = this;
        p.on_update = please_wait_update;
    }
    build_27(page(27));
    build_28(page(28));
    build_arena(page(29), 0x17e, 3);
    build_arena(page(30), 0x186, 3);
    build_arena_31(page(31));
    build_arena_32(page(32), 0x17e);
    build_arena_32(page(33), 0x186);
    build_arena_34(page(34));
    build_35(page(35));
    build_36(page(36));
    build_35(page(37));
    build_38(page(38));
    build_lobby(page(39), true);
    build_lobby(page(40), false);
    if (build_platform_pages) build_platform_pages(*this);
    // Not signed in or offline start-up paths are not taken here.
    set_page(18);
}

void MainMenu::update(float dt) {

    check_each_tick(current->flags);
    current->update(*this, dt);
    game.set_flash_global(0xe0, 0);
}

player::MovieClip* MainMenu::mode_book_hand() {
    // The "hand" child of the page's first clip.
    if (current->clips.empty()) return nullptr;
    auto* g = static_cast<GraphicItem*>(current->clips[0]);
    if (!g->clip) return nullptr;
    as::NameId hand = as::names().intern("hand");
    for (player::Character* c = g->clip->first_child; c; c = c->next) {
        if (c->name == hand) return player::MovieClip::from(c);
    }
    return nullptr;
}

as::NameId MainMenu::mode_global() const {
    return 0xd7;
}

// Pages 0-17.
void BaseMenu::build_0_17() {
    // Page 2: "unlock the full game".
    {
        Page& p = page(2);
        text(p, 0x17f);
        text(p, 0x180);
        text(p, 0x181);
        text(p, 0);
        button(p, 0);
        p.ctx = this;
        p.on_x = unlock_full_game;
        button(p, 21);
        p.ctx = this;
        p.on_update = unlock_check;
    }
    // Page 1: messages.
    {
        Page& p = page(1);
        for (int i = 0; i < 5; i++) text(p, 0);
        for (int i = 0; i < 5; i++) p.items[size_t(i)]->disabled = true;
        p.on_enter = message_enter;
        p.ctx = this;
        p.on_update = message_update;
        button(p, 3);
        p.flags = 0;
    }
    // Page 3: "Are you sure?".
    {
        Page& p = page(3);
        text(p, 0x1be);
        text(p, 0);
        text(p, 0x1c0, go_back);
        text(p, 0x1bf);
        text(p, 0);
        text(p, 0x21a);
        text(p, 0x21b);
        button(p, 17);
        button(p, 0);
    }
    // Page 4: "Are you sure?" alone.
    text(page(4), 0x1be);
    build_5(page(5));
    build_6(page(6));
    build_7(page(7));
    build_16(page(16));
    build_9(page(9));
    build_10(page(10));
    build_17(page(17));
    build_8(page(8));
    build_11(page(11));
    build_leaderboard(page(12), 0x17e, {0x5c, 0x1e6, 0x5b}, 5);
    build_leaderboard(page(13), 0x186, {0x264, 0x263, 0x1e6}, 5);
    build_leaderboard(page(14), 0x185, {0x264, 0x263}, 4);
    build_15(page(15));
}

// Page 5: Help & Options.
void BaseMenu::build_5(Page& p) {
    p.flags = 1;
    title(p, 599);
    text(p, 0x25c, open_how_to_play);
    text(p, 0x25d, open_controls);
    text(p, 0x25e, open_settings);
    text(p, 0x25f, open_credits);
    button(p, 17);
    p.ctx = this;
    p.on_back = go_back;
    p.return_page = 18;
    button(p, 0);
    graphic(p, "bookbg", 2001, 0, 0);
    p.active = true;
}

// Page 6: How to Play topics.
void BaseMenu::build_6(Page& p) {
    p.flags = 1;
    title(p, 0x25c);
    for (int id : {0x199, 0x99, 0x1ab, 0x1ac, 0x1ad, 0x1ae, 0x1b5}) text(p, id, how_to_play_topic);
    button(p, 17);
    p.ctx = this;
    p.on_back = go_back;
    p.return_page = 5;
    button(p, 0);
    graphic(p, "bookbg", 2001, 0, 0);
    p.active = true;
}

// Page 7: a How to Play topic.
void BaseMenu::build_7(Page& p) {
    p.flags = 1;
    graphic(p, "howtoplay", 2018, -((848 - int(kW)) * 0.5f), 0);
    p.ctx = this;
    p.on_back = [](BaseMenu& m) {
        todo("How to Play back");
        m.set_page(m.current->return_page);
    };
    p.return_page = 6;
    button(p, 0);
    graphic(p, "bookbg", 2001, 0, 0);
    p.active = true;
}

// Page 16: Controls.
void BaseMenu::build_16(Page& p) {
    p.flags = 1;
    title(p, 0x25d);
    float x = kW * 0.4761905074119568f;
    graphic(p, "controls", 2011, x, 300.0f);
    toggle(p, [](BaseMenu&) { todo("controls page toggle"); }, 0, {0x17e, 0x185}, x, 0);
    button(p, 17);
    p.ctx = this;
    p.on_back = go_back;
    p.return_page = 8;
    button(p, 0);
    graphic(p, "bookbg", 2001, 0, 0);
    p.active = true;
}

// Page 9: Settings: volumes, display and more.
void BaseMenu::build_9(Page& p) {
    p.flags = 1;
    float half = kW * 0.5f;
    graphic(p, "volume", 2012, half * 1.35f, 120.0f);
    graphic(p, "volume", 2013, half * 1.35f, 170.0f);
    graphic(p, "volume", 2014, half * 1.35f, 220.0f);
    title(p, 0x25e);
    text(p, 0x18a, nullptr, 0x14);
    text(p, 0x18b, nullptr, 0x14);
    text(p, 0x2de, nullptr, 0x14);
    toggle(p, nullptr, 0x2d0, {});  // display modes ("%d x %d")
    toggle(p, nullptr, 0x2d1, {0x18c, 0x18d});
    toggle(p, [](BaseMenu&) { todo("setting"); }, 0x253, {0x18c, 0x18d});
    toggle(p, [](BaseMenu&) { todo("setting"); }, 399, {0x18c, 0x18d});
    text(p, 0x2cf, [](BaseMenu& m) {
        todo("apply settings");
        m.set_page(m.current->return_page);
    });
    p.ctx = this;
    button(p, 17);
    p.ctx = this;
    p.on_back = [](BaseMenu& m) {
        todo("settings back");
        m.set_page(m.current->return_page);
    };
    p.return_page = 5;
    button(p, 0);
    p.ctx = this;
    p.on_x = [](BaseMenu&) { todo("reset defaults"); };
    button(p, 15);
    graphic(p, "bookbg", 2001, 0, 0);
    p.active = true;
}

// Page 10: Credits.
void BaseMenu::build_10(Page& p) {
    p.flags = 1;
    title(p, 0x25f);
    for (int i = 0; i < 11; i++) text(p, 0, nullptr, 0, false);
    p.on_update = credits_update;
    p.ctx = this;
    p.on_back = go_back;
    p.return_page = 5;
    button(p, 0);
    graphic(p, "bookbg", 2001, 0, 0);
    p.active = true;
}

// Page 17: keyboard settings.
void BaseMenu::build_17(Page& p) {
    title(p, 0x2ce);
    float left = kW * 0.5f - 165.0f, right = kW * 0.5f + 165.0f;
    static const float kRows[9] = {110, 140, 170, 200, 230, 260, 290, 320, 350};
    int depth = 2019;
    for (int column = 0; column < 2; column++) {
        for (int row = 0; row < 9; row++) {
            auto item = std::make_unique<KeyItem>(*this, depth++, column ? right : left, kRows[row]);
            KeyItem* k = item.get();
            p.owned.push_back(std::move(item));
            if (column == 0 && row == 0) k->on_focus();
            p.add(k);
        }
    }
    text(p, 0x18e, [](BaseMenu&) { todo("keyboard defaults"); }, 0, true, kW * 0.5f, 0);
    text(p, 0x2cf, [](BaseMenu& m) {
        todo("keyboard apply");
        m.set_page(m.current->return_page);
    }, 0, true, kW * 0.5f, 0);
    button(p, 17);
    p.ctx = this;
    p.on_back = go_back;
    p.return_page = 8;
    button(p, 0);
    graphic(p, "bookbg", 2001, 0, 0);
    p.active = true;
}

// Page 8: the controls menu.
void BaseMenu::build_8(Page& p) {
    p.flags = 1;
    title(p, 0x25d);
    text(p, 0x278, open_keyboard);
    text(p, 0x2ce, [](BaseMenu&) { todo("keyboard settings"); });
    button(p, 17);
    p.ctx = this;
    p.on_back = go_back;
    p.return_page = 5;
    button(p, 0);
    graphic(p, "bookbg", 2001, 0, 0);
    p.active = true;
}

// Page 11: which leaderboard.
void BaseMenu::build_11(Page& p) {
    title(p, 0x255);
    auto open = [](BaseMenu&) { todo("leaderboard"); };
    text(p, 0x17e, open);
    text(p, 0x186, open);
    text(p, 0x185, open);
    button(p, 17);
    p.ctx = this;
    p.on_back = go_back;
    p.return_page = 18;
    button(p, 0);
    p.active = true;
    p.flags = 0x15;
}

// Pages 12-14: a leaderboard.
void BaseMenu::build_leaderboard(Page& p, int heading, std::initializer_list<int> columns, int grid_columns) {
    title(p, heading);
    text(p, 0x1fa);
    text(p, 0);
    text(p, 0x215)->align = 0;
    text(p, 0x1fb)->align = 1;
    p.grid_columns = uint16_t(grid_columns);
    p.grid_rows = 10;
    p.grid_x = 0;
    p.grid_column = 1;
    p.grid_row = 1;
    p.grid_y = kH * 0.5f - 40.0f;
    p.grid = true;
    text(p, 0x1e8);
    text(p, 0x24e);
    for (int c : columns) text(p, c);
    for (int row = 0; row < 9; row++) {
        text(p, 0)->align = 0;
        text(p, 0, [](BaseMenu&) { todo("leaderboard entry"); }, 0x18);
        for (int c = 2; c < grid_columns; c++) text(p, 0);
    }
    text(p, 0x152);
    text(p, 0x1ee);
    graphic(p, "lbbg", 2003, 445.0f, 80.0f);
    graphic(p, "bookbg", 2001, 0, 0);
    button(p, 13);
    p.ctx = this;
    p.on_back = [](BaseMenu& m) {
        todo("leaderboard back");
        m.set_page(m.current->return_page);
    };
    p.return_page = 11;
    button(p, 12);
    p.ctx = this;
    p.on_y = [](BaseMenu&) { todo("leaderboard filter"); };
    button(p, 1);
    p.ctx = this;
    p.on_rb = [](BaseMenu&) { todo("leaderboard next"); };
    button(p, 11);
    p.ctx = this;
    p.on_lb = [](BaseMenu&) { todo("leaderboard previous"); };
    button(p, 10);
    p.ctx = this;
    p.on_update = [](BaseMenu&) {};
    p.active = true;
    p.flags = 0x15;
}

// Page 15: the in-game menu's "are you sure".
void BaseMenu::build_15(Page& p) {
    auto t = std::make_unique<TitleItem>(0x188);
    p.add(t.get());
    p.owned.push_back(std::move(t));
    for (int i = 0; i < 5; i++) text(p, 0);
    text(p, 0x214, [](BaseMenu&) { todo("retry"); });
    text(p, 0x5a, [](BaseMenu&) { todo("continue"); });
    button(p, 17);
}

// Page 19: "unlock the full game" from the main menu.
void MainMenu::build_19(Page& p) {
    text(p, 0x17f);
    text(p, 0x180);
    text(p, 0x181);
    text(p, 0);
    p.ctx = this;
    p.on_accept = [](BaseMenu&) {};
    p.next_page = 0;
    button(p, 3);
    p.ctx = this;
    p.on_back = go_back;
    p.return_page = 18;
    button(p, 0);
    p.ctx = this;
    p.on_x = unlock_full_game;
    button(p, 21);
}

// Page 18: the main menu.
void MainMenu::build_18(Page& p) {
    graphic(p, "title", 2000, kW * 0.5f, 70.0f);
    text(p, 0x182, local_game);
    text(p, 0x183, online_game);
    text(p, 0x184, player_stats);
    text(p, 0x255, leaderboards);
    text(p, 0x256, achievements);
    text(p, 599, open_help);
    text(p, 0x259, download_content);  // Steam's app ID matches
    text(p, 9, quit_game);
    text(p, 0);
    text(p, 0);
    // The build's version line, bottom right (its text is set elsewhere).
    auto version = std::make_unique<TextItem>(this, nullptr, 0, 0, false);
    version->scale = 0.5f;
    p.add(version.get(), kW - 64.0f, 0);
    p.owned.push_back(std::move(version));
    p.ctx = this;
    p.on_update = main_update;
    button(p, 17);
    p.ctx = this;
    p.on_x = nullptr;
}

// Page 20: the mode book (Local Game).
void MainMenu::build_20(Page& p) {
    p.flags = 0x11;
    // Three mouse regions over the book's pages.
    auto region = [&](float l, float t, float r, float b, Callback hover, Callback up) {
        Region g;
        g.box[0] = l;
        g.box[1] = t;
        g.box[2] = r;
        g.box[3] = b;
        g.ctx = this;
        g.on_hover = hover;
        g.on_up = up;
        p.regions.push_back(g);
    };
    region(137.5f, 40.0f, 412.5f, 200.0f, region_quaff_hover, region_quaff_up);
    region(137.5f, 230.0f, 412.5f, 370.0f, region_arena_hover, region_arena_up);
    region(447.5f, 57.5f, 722.5f, 322.5f, region_arcade_hover, region_arcade_up);
    text(p, 0);
    text(p, 0);
    text(p, 0);
    graphic(p, "modemenu", 2002, kW * 0.5f, 0);
    button(p, 17);
    p.ctx = this;
    p.on_back = go_back;
    p.return_page = 18;
    button(p, 0);
    p.ctx = this;
    p.on_update = mode_update;
    p.items[p.selected]->color = 0x777777ff;
    p.selected = 0;
    p.items[0]->color = 0xffff00ff;

}

// Page 21: sign in to post scores.
void MainMenu::build_21(Page& p) {
    p.flags = 0x11;
    text(p, 0x1f1);
    text(p, 0x1f2);
    text(p, 499);
    text(p, 0);
    p.ctx = this;
    p.on_accept = [](BaseMenu&) { todo("start without signing in"); };
    p.next_page = 20;
    button(p, 3);
    p.ctx = this;
    p.on_back = go_back;
    p.return_page = 20;
    button(p, 0);
}

// Page 22: player statistics.
void MainMenu::build_22(Page& p) {
    p.flags = 1;
    text(p, 0);
    float half = kW * 0.5f;
    graphic(p, "stats", 2015, half, kH == 576.0f ? 300.0f : 220.0f);
    graphic(p, "bookbg", 2001, 0, 0);
    graphic(p, "pageturn", 2037, half - 250.0f, 70.0f);
    p.ctx = this;
    p.on_update = stats_update;
    button(p, 14);
    p.ctx = this;
    p.on_y = [](BaseMenu&) { todo("reset character data"); };
    button(p, 22);
    p.ctx = this;
    p.on_x = [](BaseMenu&) { todo("reset save data"); };
    ButtonItem* back = button(p, 0);
    p.ctx = this;
    p.on_back = go_back;
    p.return_page = 18;
    ButtonItem* a = button(p, 3);
    a->x = 0;  // the original's/a4: placed by hand
    a->set_shown(false);
    ButtonItem* b = button(p, 0);
    b->set_shown(false);
    back->set_shown(true);
    p.active = true;
}

// Page 27: the online mode book.
void MainMenu::build_27(Page& p) {
    text(p, 0);
    text(p, 0);
    text(p, 0);
    graphic(p, "modemenu", 2002, kW * 0.5f, 0);
    button(p, 17);
    p.ctx = this;
    p.on_back = go_back;
    p.return_page = 18;
    button(p, 0);
    p.ctx = this;
    p.on_update = [](BaseMenu&) {};
    p.flags = 0xff;
}

// Page 28: online match type.
void MainMenu::build_28(Page& p) {
    title(p, 0x246);
    text(p, 0x251, [](BaseMenu&) { todo("quick match"); });
    text(p, 0x24d, [](BaseMenu&) { todo("custom match"); });
    text(p, 0x1d1, [](BaseMenu&) { todo("create match"); });
    button(p, 17);
    p.ctx = this;
    p.on_back = go_back;
    p.return_page = 27;
    button(p, 0);
    p.flags = 0xff;
}

// Pages 29, 30: create a match.
void MainMenu::build_arena(Page& p, int heading, int) {
    title(p, heading);
    toggle(p, [](BaseMenu&) {}, 0x2d3, {0x2d4, 0x2d5, 0x2d6});
    toggle(p, [](BaseMenu&) {}, 0x1d4, {0x217, 0x218, 0x219});
    ToggleItem* t = toggle(p, [](BaseMenu&) {}, 0x1d6, {});
    t->set_text_id(0x1d6, 0x12);
    t->add_option(0x1c0);
    t->add_option(0x1bf);
    text(p, 0x1d1, [](BaseMenu&) { todo("create match"); });
    button(p, 17);
    p.ctx = this;
    p.on_back = go_back;
    p.return_page = 0x1c;
    button(p, 0);
    p.flags = 0xff;
}

// Page 31.
void MainMenu::build_arena_31(Page& p) {
    title(p, 0x185);
    toggle(p, [](BaseMenu&) {}, 0x2d3, {0x2d4, 0x2d5, 0x2d6});
    toggle(p, [](BaseMenu&) {}, 0x1d4, {0x217, 0x218, 0x219});
    text(p, 0x1d1, [](BaseMenu&) { todo("create match"); });
    button(p, 17);
    p.ctx = this;
    p.on_back = go_back;
    p.return_page = 0x1c;
    button(p, 0);
    p.flags = 0xff;
}

// Pages 32, 33: find a match.
void MainMenu::build_arena_32(Page& p, int heading) {
    title(p, heading);
    toggle(p, [](BaseMenu&) {}, 0x1db, {0x2d4, 0x2d5});
    toggle(p, [](BaseMenu&) {}, 0x1d4, {0x217, 0x218, 0x219})->wraps = false;
    ToggleItem* t = toggle(p, [](BaseMenu&) {}, 0x1d6, {});
    t->set_text_id(0x1d6, 0x12);
    t->add_option(0x1c0);
    t->add_option(0x1bf);
    text(p, 0x1db, [](BaseMenu&) { todo("find match"); });
    button(p, 17);
    p.ctx = this;
    p.on_back = go_back;
    p.return_page = 0x1c;
    button(p, 0);
    p.flags = 0xff;
}

// Page 34.
void MainMenu::build_arena_34(Page& p) {
    title(p, 0x185);
    toggle(p, [](BaseMenu&) {}, 0x1db, {0x2d4, 0x2d5});
    toggle(p, [](BaseMenu&) {}, 0x1d4, {0x217, 0x218, 0x219})->wraps = false;
    text(p, 0x1db, [](BaseMenu&) { todo("find match"); });
    button(p, 17);
    p.ctx = this;
    p.on_back = go_back;
    p.return_page = 0x1c;
    button(p, 0);
    p.flags = 0xff;
}

// Pages 35, 37: searching.
void MainMenu::build_35(Page& p) {
    title(p, 0x246);
    text(p, 0x1da);
    p.ctx = this;
    p.on_update = [](BaseMenu&) {};
    p.flags = 0xff;
}

// Page 36: match list.
void MainMenu::build_36(Page& p) {
    title(p, 0x246);
    p.grid_x = 0;
    p.grid_columns = 3;
    p.grid_rows = 10;
    p.grid_y = 230.0f;
    p.grid_column = 1;
    p.grid_row = 1;
    p.grid = true;
    text(p, 0x247);
    text(p, 0x1d0);
    text(p, 0x1d4);
    for (int i = 0; i < 9; i++) {
        text(p, 0x24e, [](BaseMenu&) { todo("join match"); });
        text(p, 0x215);
        text(p, 0x215);
    }
    auto bg = std::make_unique<GraphicItem>(*this);
    bg->attach("lbbg", 2004, 445.0f, 110.0f);
    p.add(bg.get());
    p.owned.push_back(std::move(bg));
    button(p, 17);
    p.ctx = this;
    p.on_back = [](BaseMenu& m) { m.set_page(m.current->return_page); };
    p.return_page = 0x1c;
    button(p, 0);
    p.flags = 0xff;
}

// Page 38.
void MainMenu::build_38(Page& p) {
    title(p, 0x17d);
    p.ctx = this;
    p.on_update = [](BaseMenu&) {};
    p.flags = 0xff;
}

// Pages 39 (host) and 40: the online lobby.
void MainMenu::build_lobby(Page& p, bool host) {
    title(p, 0x246);
    p.input_blocked = true;
    float x = online_capable ? 444.0f : 340.0f;
    auto join = [](BaseMenu&) { todo("lobby join"); };
    for (int i = 0; i < 4; i++) text(p, 0x1de, join, 0x15);
    graphic(p, "gamerbox", 2005, x, 110.0f);
    graphic(p, "gamerbox", 2006, x, 180.0f);
    graphic(p, "gamerbox", 2007, x, 250.0f);
    graphic(p, "gamerbox", 2008, x, 320.0f);
    graphic(p, "countdown", 2041, x + 225.0f, 320.0f);
    static const float kRows[5] = {50.0f, 100.0f, 170.0f, 240.0f, 310.0f};
    for (int i = 0; i < 5; i++) p.items[size_t(i)]->y = kRows[i];
    button(p, 23);
    if (host) {
        button(p, 5);
        p.on_x = [](BaseMenu&) { todo("lobby invite"); };
    }
    p.ctx = this;
    p.on_back = [](BaseMenu& m) {
        todo("leave lobby");
        m.set_page(m.current->return_page);
    };
    p.return_page = 0x1c;
    button(p, 8);
    button(p, 20);
    p.on_y = [](BaseMenu&) { todo("lobby ready"); };
    p.ctx = this;
    p.on_update = [](BaseMenu&) {};  // the original's (host) / the original's
    p.flags = 0xf;
}

// ---- KeyItem (KeyboardInteractiveGraphicMenuItem)

KeyItem::KeyItem(BaseMenu& menu, int depth, float px, float py) : GraphicItem(menu) {
    attach("keyboard_entry", depth, px, py);
    box[0] = px - 35.0f;
    box[1] = py - 10.0f;
    box[2] = px + 105.0f;
    box[3] = py + 10.0f;
    hittable = true;
    flag2b = true;
}

void KeyItem::on_focus() {
    // the entry shows its selected frame.
    if (clip) {
        clip->flags |= player::MovieClip::kStopRequested;
        clip->goto_frame(2);
    }
}

void KeyItem::on_blur() {

    if (clip) {
        clip->flags |= player::MovieClip::kStopRequested;
        clip->goto_frame(1);
    }
}

// ---- create

std::unique_ptr<BaseMenu> create(const std::string& name, player::Game& game, player::Player& movie) {
    if (name == "menu") return std::make_unique<MainMenu>(game, movie);
    // "results" (ResultsMenu) is the Xbox 360 save / leaderboard
    // screen: results.swf asks for it only when console_version is set, which
    // no PC script does, and nothing loads results.swf. The PC's level ends
    // are the scripts' own (f_ChangeLevel back to the map).
    if (name == "results") SDL_Log("menu: results controller requested (Xbox only, not ported)");
    return nullptr;
}

}  // namespace menu
