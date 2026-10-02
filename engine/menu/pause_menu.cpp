// PauseMenu (vtable 0x4d0a1c): the in-game menu over the `pause` movie.
// Its 25 pages: 0-17 shared with every controller (BaseMenu::build_0_17),
// 0x12 the pause menu itself, 0x13 messages, 0x14 "a player left", 0x15 the
// players, 0x16 Information, 0x17 and 0x18 the weapon and animal books.

#include "menu/pause_menu.h"

#include <SDL3/SDL_log.h>

#include <algorithm>

#include "audio/audio.h"
#include "input/input.h"
#include "player/game.h"
#include "player/movieclip.h"
#include "player/player.h"

namespace menu {

namespace {

constexpr float kW = 848.0f;  // the original's +0xc
constexpr float kH = 480.0f;  // the original's +0x10

constexpr as::NameId kMap = 0xe4;       // g_bMap
constexpr as::NameId kMenu = 0xe5;      // g_bMenu
constexpr as::NameId kExitGame = 0xdf;  // g_bExitGame

PauseMenu& P(BaseMenu& b) { return static_cast<PauseMenu&>(b); }

void todo(const char* what) { SDL_Log("pause menu: %s not ported yet", what); }

// The pause root's child at `depth`, if a clip.
player::MovieClip* clip_at(BaseMenu& m, int depth) {
    for (player::Character* c = m.movie.root()->first_child; c; c = c->next)
        if (c->depth == depth) return player::MovieClip::from(c);
    return nullptr;
}

// the original's after setting "stop": gotoAndStop, the frame clamped.
void goto_and_stop(player::MovieClip* c, int f) {
    if (!c) return;
    c->flags |= player::MovieClip::kStopRequested;
    c->goto_frame(uint16_t(std::min(f, int(c->total_frames))));
}

// back to the page's return page.
void go_back(BaseMenu& m) { m.set_page(m.current->return_page); }

// Resume Game, and B on the main page.
void resume(BaseMenu& m) { m.game.close_pause(m.current->port, 1); }

// the Steam store's overlay. Nothing here.
void download_content(BaseMenu&) { todo("Download Content"); }

// Help & Options, with the book background on its page.
void help(BaseMenu& m) {
    goto_and_stop(clip_at(m, 2001), 17);
    int port = m.current->port;
    if (port >= 0 && port < 5 && m.signed_in(port)) m.set_page(5);
}

void leaderboards(BaseMenu&) { todo("Leaderboards"); }


void information(BaseMenu& m) { m.set_page(0x16); }

void players(BaseMenu&) { todo("Players, online only"); }

// yes to Exit To Map. The port's state becomes 3; the level
// sees it on the next tick and goes to the map itself.
void exit_to_map_yes(BaseMenu& m) {
    int port = m.current->port;
    if (port >= 0 && port < 4) {
        uint8_t& s = m.game.input.devices[port].port_state;
        s = uint8_t((s & 0xf3) | 3);
    }
    audio::manager().menu_fade_out(false);  // the music fades out over 3 s
    m.game.close_pause(port, 1);
}


void exit_to_map(BaseMenu& m) { P(m).confirm(false, 0x21a, 0x21b, 0x12, exit_to_map_yes, go_back, nullptr); }

// yes to Exit Game. The game is left and the level plays its
// "quit" clip to the main menu.
void exit_game_yes(BaseMenu& m) {
    int port = m.current->port;
    m.game.set_flash_global(kExitGame, 1);
    m.game.leave_game();
    audio::manager().menu_fade_out(true);  // fades out, then all music is cleared
    m.game.quit_to("menu.cok6");
    m.game.close_pause(port, 1);
}


void exit_game(BaseMenu& m) { P(m).confirm(true, 0x21a, 0x21b, 0x12, exit_game_yes, go_back, nullptr); }

// the weapon and animal books.
void weapon_stats(BaseMenu& m) {
    todo("Weapon Statistics' contents");
    m.set_page(0x17);
}
void animal_stats(BaseMenu& m) {
    todo("Animal Statistics' contents");
    m.set_page(0x18);
}

void achievements(BaseMenu&) { todo("Achievements overlay"); }

// the original's (the pause's branch): the book background back on its page,
// then the return page.
void book_back(BaseMenu& m) {
    goto_and_stop(clip_at(m, 2001), 17);
    m.set_page(m.current->return_page);
}

}  // namespace

PauseMenu::PauseMenu(player::Game& g, player::Player& m) : BaseMenu(g, m) {
    pages.resize(25);
    build_0_17();
    build_main(page(0x12));
    build_messages(page(0x13));
    build_player_left(page(0x14));
    build_players(page(0x15));
    build_information(page(0x16));
    build_stats(page(0x17), "weapstats", 0x7e0);
    build_stats(page(0x18), "anmlstats", 0x7e1);
    set_page(0x12);
}

void PauseMenu::update(float dt) {

    check_each_tick(current->flags);
    current->update(*this, dt);
    // The pause movie shows the level's page (2) or the map's (1), or the
    // "player left" one (3).
    int target;
    if (current_index == 0x14) {
        target = 3;
    } else {
        target = 1;
        // Before any game, castle.exe reads its dt argument's bits as the
        // mode: never 0, 1 or 3.
        int mode = game.game_mode;
        if (mode == 0) target = game.get_flash_global(kMap) == 1 ? 1 : 2;
        else if (mode == 1 || mode == 3) target = game.get_flash_global(kMenu) == 1 ? 1 : 2;
    }
    goto_and_stop(movie.root(), target);
}

void PauseMenu::opened(uint32_t reason, int port) {
    set_page(0x12);

    if (reason == 0) {
        Page& p = *current;
        p.fade = 1;
        p.input_on = false;
        p.fade_alpha = 0.0f;
        if (p.port < 0) p.port = p.input_port = -2;
        return;
    }
    if (reason & 0xe0) {
        todo("the pause's message page (achievement/save/avatar failures)");
    } else {
        Page& p = *current;
        p.items[p.selected]->color = 0x777777ff;
        p.selected = 1;
        p.items[1]->color = 0xffff00ff;
        auto t = [&](size_t i) { return static_cast<TextItem*>(p.items[i]); };
        t(0)->set_text_id(0x188);  // "Paused" (blank online)
        t(2)->set_text_id(0x259);  // "Download Content": Steam's app ID is the full game's
        // Information needs a player with weapons: any in a
        // level.
        bool info = int16_t(port) >= 0;
        t(5)->disabled = !info;
        t(5)->set_text_id(info ? 0x2d9 : 0);
        t(6)->disabled = true;  // Players: online only
        t(6)->set_text_id(0);
        int mode = game.game_mode >= 0 ? game.game_mode : port;
        bool to_map = mode == 0 && game.get_flash_global(kMap) != 1;
        t(7)->disabled = !to_map;
        t(7)->set_text_id(to_map ? 0x189 : 0);
    }
    int16_t v = int16_t(port < -3 ? -1 : port);
    current->input_port = v;
    current->port = v;
}

void PauseMenu::confirm(bool two_line, int line1, int line2, int return_page, Callback yes, Callback no,
                        Callback upd) {

    Page& p = page(3);
    p.items[p.selected]->color = 0x777777ff;
    p.selected = 2;
    p.items[2]->color = 0xffff00ff;
    p.port = p.input_port = current->port;
    p.on_back = no;
    p.return_page = uint16_t(return_page);
    p.ctx = this;
    p.on_update = upd;
    auto t = [&](size_t i) { return static_cast<TextItem*>(p.items[i]); };
    t(0)->set_text_id(two_line ? 0x2e1 : 0x1be);
    t(1)->set_text_id(two_line ? 0x2e2 : 0);
    t(2)->ctx = this;
    t(2)->callback = no;
    t(3)->ctx = this;
    t(3)->callback = yes;
    t(5)->set_text_id(line1);
    t(6)->set_text_id(line2);
    set_page(3);
}

// Page 0x12: Paused.
void PauseMenu::build_main(Page& p) {
    title(p, 0x188);
    text(p, 0x25b, resume);
    text(p, 600, download_content);
    text(p, 599, help);
    text(p, 0x255, leaderboards);
    text(p, 0x2d9, information);
    text(p, 0x26f, players);
    text(p, 0x189, exit_to_map);
    text(p, 0x62, exit_game);
    button(p, 0x11);
    p.ctx = this;
    p.on_back = resume;
    p.return_page = 0;
    button(p, 0);
    p.active = true;
    p.flags = 0;
}

// Page 0x13 (as page 15): a message, Retry or Continue.
void PauseMenu::build_messages(Page& p) { build_15(p); }

// Page 0x14: "... has left the game" (online only).
void PauseMenu::build_player_left(Page& p) {
    for (int i = 0; i < 3; i++) {
        TextItem* t = text(p, 0);
        t->disabled = true;
    }
    for (Item* i : p.items) i->y += 100.0f;
    p.ctx = this;
    p.on_update = [](BaseMenu&) { todo("player left"); };
}

// Page 0x15: the players (online only).
void PauseMenu::build_players(Page& p) {
    title(p, 0x26f);
    auto card = [](BaseMenu&) { todo("gamer card"); };
    static const float kRows[3] = {200.0f, 250.0f, 300.0f};
    for (float y : kRows) text(p, 0, card, 0, true, kW * 0.5f, y)->disabled = true;
    float x = 325.0f;  // the game is online-capable (the original's +9); 221 if not
    graphic(p, "talkicon", 0x7f6, x, 202.0f);
    graphic(p, "talkicon", 0x7f7, x, 252.0f);
    graphic(p, "talkicon", 0x7f8, x, 302.0f);
    p.ctx = this;
    p.on_update = [](BaseMenu&) {};  // icons while a player talks
    button(p, 7);
    p.ctx = this;
    p.on_x = [](BaseMenu&) { todo("kick"); };
    p.flags1f4 |= 1;
    button(p, 0x17);
    button(p, 0);
    p.ctx = this;
    p.on_back = go_back;
    p.return_page = 0x12;
    p.on_enter = [](BaseMenu&) {};  // icons hidden
    p.active = true;
}

// Page 0x16: Information.
void PauseMenu::build_information(Page& p) {
    title(p, 0x2d9);
    text(p, 0x256, achievements);
    text(p, 0x2d7, weapon_stats);
    text(p, 0x2d8, animal_stats);
    button(p, 0x11);
    p.ctx = this;
    p.on_back = go_back;
    p.return_page = 0x12;
    button(p, 0);
    p.active = true;
}

// Pages 0x17, 0x18: the weapon and animal books.
void PauseMenu::build_stats(Page& p, const char* clip, int depth) {
    graphic(p, clip, depth, kW * 0.5f, kH == 576.0f ? 300.0f : 220.0f);
    graphic(p, "bookbg", 0x7d1, 0, 0);
    graphic(p, "pageturn", 0x7f5, kW * 0.5f - 250.0f, 70.0f);
    p.ctx = this;
    p.on_update = [](BaseMenu&) {};  // turning pages
    p.on_back = book_back;
    p.return_page = 0x16;
    button(p, 0);
    p.active = true;
}

}  // namespace menu
