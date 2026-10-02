// The pause menu (vtable 0x4d0a1c): the FlashController castle.exe makes at
// boot over the `pause` movie and shows when a player presses
// Start in a game (Game::open_pause). See docs/engine/pause.md.
#pragma once

#include "menu/menu.h"

namespace menu {

class PauseMenu : public BaseMenu {
public:
    PauseMenu(player::Game& game, player::Player& movie);
    void update(float dt) override;
    int kind() const override { return 2; }                // 0x4752b0

    // Opening: the main page again, set up for `port` (the original's's part
    // with the original's).
    void opened(uint32_t reason, int port);

    // page 3 asks "are you sure" (two lines of question when
    // `two_line`), Yes and No going to the given callbacks.
    void confirm(bool two_line, int line1, int line2, int return_page, Callback yes, Callback no, Callback update);

    uint16_t stats_index = 0;  // +0x18
    uint16_t stats_port = 0;   // +0x1a

private:
    void build_main(Page& p);         // page 0x12, the original's
    void build_messages(Page& p);     // page 0x13, the original's
    void build_player_left(Page& p);  // page 0x14, the original's
    void build_players(Page& p);      // page 0x15, the original's
    void build_information(Page& p);  // page 0x16, the original's
    void build_stats(Page& p, const char* clip, int depth);  // pages 0x17 the original's, 0x18 the original's
};

}  // namespace menu
