// MainMenu (vtable 0x49c9b0): SetFlashController("menu").
#pragma once

#include <initializer_list>

#include "as/names.h"
#include "menu/menu.h"

namespace menu {

// KeyboardInteractiveGraphicMenuItem (0x1b4 bytes): one key of the keyboard
// settings page.
class KeyItem : public GraphicItem {
public:
    KeyItem(BaseMenu& menu, int depth, float x, float y);
    bool selectable() const override { return true; }
    int type() const override { return kInteractive; }
    void on_focus() override;
    void on_blur() override;
};

class MainMenu : public BaseMenu {
public:
    MainMenu(player::Game& game, player::Player& movie);
    void update(float dt) override;
    int kind() const override { return 1; }

    player::MovieClip* mode_book_hand();
    as::NameId mode_global() const;

    uint16_t mode = 0;          // +0x0c: the chosen game mode
    uint16_t stats_index = 0;   // +0x18: the character shown in Player Statistics
    uint16_t stats_port = 0;    // +0x1a: whose save it reads
    bool online_capable = true;  // the original's +9

    // A port's own pages, after castle.exe's 41 (a console's memory card
    // page): how many, and what builds them once the others are built.
    // With on_online set, Online Multiplayer does that instead.
    static inline int platform_pages = 0;
    static inline void (*build_platform_pages)(MainMenu& menu) = nullptr;
    static inline Callback on_online = nullptr;
    // The building blocks, for those pages.
    using BaseMenu::button;
    using BaseMenu::text;

private:
    void build_18(Page& p);
    void build_19(Page& p);
    void build_20(Page& p);
    void build_21(Page& p);
    void build_22(Page& p);
    void build_27(Page& p);
    void build_28(Page& p);
    void build_35(Page& p);
    void build_36(Page& p);
    void build_38(Page& p);
    void build_arena(Page& p, int heading, int);
    void build_arena_31(Page& p);
    void build_arena_32(Page& p, int heading);
    void build_arena_34(Page& p);
    void build_lobby(Page& p, bool host);
};

}  // namespace menu
