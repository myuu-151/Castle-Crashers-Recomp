// Generated from docs/engine/builtin-names.txt by tools/gen_builtins.py: the
// names castle.exe registers with fixed IDs (the original's calls).
#pragma once

#include <cstdint>

namespace as {

struct BuiltinName {
    uint16_t id;
    const char* name;
};

inline constexpr BuiltinName kBuiltinNames[] = {
    {0x01, "length"},
    {0x02, "pop"},
    {0x03, "push"},
    {0x04, "splice"},
    {0x05, "charAt"},
    {0x06, "charCodeAt"},
    {0x07, "substr"},
    {0x08, "isDown"},
    {0x09, "abs"},
    {0x0a, "acos"},
    {0x0b, "atan"},
    {0x0c, "atan2"},
    {0x0d, "ceil"},
    {0x0e, "cos"},
    {0x0f, "floor"},
    {0x10, "min"},
    {0x11, "max"},
    {0x12, "PI"},
    {0x13, "pow"},
    {0x14, "random"},
    {0x15, "round"},
    {0x16, "sin"},
    {0x17, "sqrt"},
    {0x18, "attachMovie"},
    {0x19, "createEmptyMovieClip"},
    {0x1a, "duplicateMovieClip"},
    {0x1b, "globalToLocal"},
    {0x1c, "getBounds"},
    {0x1d, "getDepth"},
    {0x1e, "gotoAndPlay"},
    {0x1f, "gotoAndStop"},
    {0x20, "hitTest"},
    {0x21, "loadMovie"},
    {0x22, "localToGlobal"},
    {0x23, "nextFrame"},
    {0x24, "play"},
    {0x25, "prevFrame"},
    {0x26, "removeMovieClip"},
    {0x27, "setMask"},
    {0x28, "start"},
    {0x29, "stop"},
    {0x2a, "swapDepths"},
    {0x2b, "unescape"},
    {0x2c, "unloadMovie"},
    {0x2d, "AddShader"},
    {0x2e, "Error"},
    {0x2f, "GetFramerate"},
    {0x30, "IsActive"},
    {0x31, "RemoveShader"},
    {0x32, "SetActive"},
    {0x33, "SetBackgroundAlpha"},
    {0x34, "SetFramerate"},
    {0x35, "FadeIn"},
    {0x36, "FadeOut"},
    {0x37, "GetMasterVolume"},
    {0x38, "GetMusicVolume"},
    {0x39, "GetSfxVolume"},
    {0x3a, "PauseMusic"},
    {0x3b, "PlayMusic"},
    {0x3c, "RegisterMusic"},
    {0x3d, "SetMasterVolume"},
    {0x3e, "SetMusicVolume"},
    {0x3f, "SetSfxVolume"},
    {0x40, "StopMusic"},
    {0x41, "UnregisterMusic"},
    {0x42, "ClearAllMusic"},
    {0x43, "ReadStorage"},
    {0x44, "WriteStorage"},
    {0x45, "attachSound"},
    {0x46, "getPan"},
    {0x47, "getVolume"},
    {0x48, "setPan"},
    {0x49, "setVolume"},
    {0x4a, "setRGB"},
    {0x4b, "setTransform"},
    {0x4c, "Down"},
    {0x4d, "Left"},
    {0x4e, "Right"},
    {0x4f, "Up"},
    {0x50, "_x"},
    {0x51, "_y"},
    {0x52, "_xscale"},
    {0x53, "_yscale"},
    {0x54, "_currentframe"},
    {0x55, "_totalframes"},
    {0x56, "_alpha"},
    {0x57, "_visible"},
    {0x58, "_width"},
    {0x59, "_height"},
    {0x5a, "_rotation"},
    {0x5b, "_target"},
    {0x5c, "_framesloaded"},
    {0x5d, "_name"},
    {0x5e, "_droptarget"},
    {0x5f, "_url"},
    {0x60, "_highquality"},
    {0x61, "_focusrect"},
    {0x62, "_soundbuftime"},
    {0x63, "_quality"},
    {0x64, "this"},
    {0x65, "xmax"},
    {0x66, "ntext"},
    {0x67, "text"},
    {0x68, "_parent"},
    {0x69, "_root"},
    {0x6a, "Array"},
    {0x6b, "Color"},
    {0x6c, "console_version"},
    {0x6d, "Key"},
    {0x6e, "Math"},
    {0x6f, "Object"},
    {0x70, "Sound"},
    {0x71, "ra"},
    {0x72, "ga"},
    {0x73, "ba"},
    {0x74, "aa"},
    {0x75, "rb"},
    {0x76, "gb"},
    {0x77, "bb"},
    {0x78, "ab"},
    {0x79, "x"},
    {0x7a, "xMax"},
    {0x7b, "xMin"},
    {0x7c, "y"},
    {0x7d, "yMax"},
    {0x7e, "yMin"},
    {0x7f, "_level0"},
    {0x80, "TRUE"},
    {0x81, "FALSE"},
    {0x83, "ASSERT"},
    {0x84, "ChangeMovie"},
    {0x85, "GetCheat"},
    {0x86, "GetFlashGlobal"},
    {0x87, "GetGore"},
    {0x88, "GetLanguage"},
    {0x89, "GetLocalSecond"},
    {0x8a, "GetRegion"},
    {0x8b, "GetTextLocalizationScale"},
    {0x8c, "GetWidescreen"},
    {0x8d, "IsPalMode"},
    {0x8e, "LOGPush"},
    {0x8f, "QuitTo"},
    {0x90, "ReadSaveGame"},
    {0x91, "SetFlashController"},
    {0x92, "SetFlashGlobal"},
    {0x93, "SetTextboxAsInteger"},
    {0x94, "SetTextNumeric"},
    {0x95, "VibrateController"},
    {0x96, "WriteSaveGame"},
    {0x97, "ShowLoadIcon"},
    {0x98, "HideLoadIcon"},
    {0x99, "f_LocalToGame"},
    {0x9a, "PreloadNews"},
    {0x9b, "SetMarketOffer"},
    {0x9c, "f_Quantize"},
    {0x9d, "GetLocalGamerTag"},
    {0x9e, "SetTextToGamerTag"},
    {0x9f, "IsFullGame"},
    {0xa0, "ReturnToArcade"},
    {0xa1, "RichPresence"},
    {0xa2, "SetRichPresence"},
    {0xa3, "SetScores"},
    {0xa4, "ShowSignInScreen"},
    {0xa5, "UnlockAchievement"},
    {0xa6, "UnlockFullGame"},
    {0xa7, "UnlockAvatarItem"},
    {0xa8, "FlushStats"},
    {0xa9, "GetGameMode"},
    {0xaa, "IsLocalPlayerInSession"},
    {0xab, "IsPlayerSignedIn"},
    {0xac, "IsPortTalking"},
    {0xad, "JoinSession"},
    {0xae, "LeaveSession"},
    {0xaf, "StartSession"},
    {0xb0, "EndSession"},
    {0xb1, "WriteAllStats"},
    {0xb2, "IsSameProfile"},
    {0xb3, "IsNetGame"},
    {0xb4, "IsRankedMatch"},
    {0xb5, "IsInviteWaiting"},
    {0xb6, "SetPortState"},
    {0xb7, "ShowGamerTag"},
    {0xb8, "SetKeyboardMapping"},
    {0xb9, "ShowMouse"},
    {0xbc, "f_BSPCheckLastHitIndex"},
    {0xbd, "f_BSPCheckLastHitType"},
    {0xbe, "f_BSPCheckLastHitSlope"},
    {0xbf, "f_BSPHitTest"},
    {0xc0, "f_BSPLoadLevel"},
    {0xc1, "f_GetClosestWaypoint"},
    {0xc2, "f_GetWPHit"},
    {0xc3, "f_GetWPX"},
    {0xc4, "f_GetWPY"},
    {0xc5, "f_SetWPHit"},
    {0xc7, "g_nPort1State"},
    {0xc8, "g_nPort2State"},
    {0xc9, "g_nPort3State"},
    {0xca, "g_nPort4State"},
    {0xcb, "g_nPlayer1Port"},
    {0xcc, "g_nPlayer2Port"},
    {0xcd, "g_nPlayer3Port"},
    {0xce, "g_nPlayer4Port"},
    {0xcf, "g_nPort1CharId"},
    {0xd0, "g_nPort2CharId"},
    {0xd1, "g_nPort3CharId"},
    {0xd2, "g_nPort4CharId"},
    {0xd3, "g_nArenaPoints"},
    {0xd4, "g_nGoodExit"},
    {0xd5, "g_nNumGamesPlayed"},
    {0xd6, "g_nPDLC"},
    {0xd7, "g_bAllowHiddenCharacters"},
    {0xd8, "g_nArenaMode"},
    {0xd9, "g_bStartedArena"},
    {0xda, "g_bStartedQuaff"},
    {0xdb, "g_bNoPause"},
    {0xdc, "g_nPausePlayer"},
    {0xdd, "quit"},
    {0xde, "quitto"},
    {0xdf, "g_bExitGame"},
    {0xe0, "g_bLoadIcon"},
    {0xe1, "g_bLoading"},
    {0xe2, "g_bReadyToLoad"},
    {0xe3, "g_bNoLoadingScreen"},
    {0xe4, "g_bMap"},
    {0xe5, "g_bMenu"},
    {0xe6, "g_bAllowInvites"},
    {0xe7, "f_ChangeLevel"},
    {0xe8, "f_WeaponStats"},
    {0xe9, "game"},
    {0xeb, "f_Depth"},
    {0xec, "f_LargeObjectRanges"},
    {0xee, "loader"},
    {0xef, "p_game"},
    {0xf0, "current_depth"},
    {0xf1, "depth_mod"},
    {0xf2, "abs_bottom"},
    {0xf3, "top"},
    {0xf4, "bottom"},
    {0xf5, "left"},
    {0xf6, "right"},
    {0xf7, "w"},
    {0xf8, "h"},
    {0xf9, "zone"},
    {0xfa, "player_pt"},
    {0xfb, "hud1"},
    {0xfc, "hud2"},
    {0xfd, "hud3"},
    {0xfe, "hud4"},
    {0xff, "port"},
    {0x100, "animal_unlocks"},
    {0x101, "item_unlocks"},
    {0x102, "item_unlocks_expansion"},
    {0x103, "weapon"},
    {0x104, "weapon_strength"},
    {0x105, "weapon_defense"},
    {0x106, "weapon_magic"},
    {0x107, "weapon_agility"},
    {0x108, "weapon_critical"},
    {0x109, "weapon_magic_type"},
    {0x10a, "weapon_level"},
    {0x10b, "pet"},
    {0x10c, "animal_type"},
};

// name::k<name>, for example name::k_x or name::kattachMovie.
namespace name {
inline constexpr uint16_t klength = 0x01;
inline constexpr uint16_t kpop = 0x02;
inline constexpr uint16_t kpush = 0x03;
inline constexpr uint16_t ksplice = 0x04;
inline constexpr uint16_t kcharAt = 0x05;
inline constexpr uint16_t kcharCodeAt = 0x06;
inline constexpr uint16_t ksubstr = 0x07;
inline constexpr uint16_t kisDown = 0x08;
inline constexpr uint16_t kabs = 0x09;
inline constexpr uint16_t kacos = 0x0a;
inline constexpr uint16_t katan = 0x0b;
inline constexpr uint16_t katan2 = 0x0c;
inline constexpr uint16_t kceil = 0x0d;
inline constexpr uint16_t kcos = 0x0e;
inline constexpr uint16_t kfloor = 0x0f;
inline constexpr uint16_t kmin = 0x10;
inline constexpr uint16_t kmax = 0x11;
inline constexpr uint16_t kPI = 0x12;
inline constexpr uint16_t kpow = 0x13;
inline constexpr uint16_t krandom = 0x14;
inline constexpr uint16_t kround = 0x15;
inline constexpr uint16_t ksin = 0x16;
inline constexpr uint16_t ksqrt = 0x17;
inline constexpr uint16_t kattachMovie = 0x18;
inline constexpr uint16_t kcreateEmptyMovieClip = 0x19;
inline constexpr uint16_t kduplicateMovieClip = 0x1a;
inline constexpr uint16_t kglobalToLocal = 0x1b;
inline constexpr uint16_t kgetBounds = 0x1c;
inline constexpr uint16_t kgetDepth = 0x1d;
inline constexpr uint16_t kgotoAndPlay = 0x1e;
inline constexpr uint16_t kgotoAndStop = 0x1f;
inline constexpr uint16_t khitTest = 0x20;
inline constexpr uint16_t kloadMovie = 0x21;
inline constexpr uint16_t klocalToGlobal = 0x22;
inline constexpr uint16_t knextFrame = 0x23;
inline constexpr uint16_t kplay = 0x24;
inline constexpr uint16_t kprevFrame = 0x25;
inline constexpr uint16_t kremoveMovieClip = 0x26;
inline constexpr uint16_t ksetMask = 0x27;
inline constexpr uint16_t kstart = 0x28;
inline constexpr uint16_t kstop = 0x29;
inline constexpr uint16_t kswapDepths = 0x2a;
inline constexpr uint16_t kunescape = 0x2b;
inline constexpr uint16_t kunloadMovie = 0x2c;
inline constexpr uint16_t kAddShader = 0x2d;
inline constexpr uint16_t kError = 0x2e;
inline constexpr uint16_t kGetFramerate = 0x2f;
inline constexpr uint16_t kIsActive = 0x30;
inline constexpr uint16_t kRemoveShader = 0x31;
inline constexpr uint16_t kSetActive = 0x32;
inline constexpr uint16_t kSetBackgroundAlpha = 0x33;
inline constexpr uint16_t kSetFramerate = 0x34;
inline constexpr uint16_t kFadeIn = 0x35;
inline constexpr uint16_t kFadeOut = 0x36;
inline constexpr uint16_t kGetMasterVolume = 0x37;
inline constexpr uint16_t kGetMusicVolume = 0x38;
inline constexpr uint16_t kGetSfxVolume = 0x39;
inline constexpr uint16_t kPauseMusic = 0x3a;
inline constexpr uint16_t kPlayMusic = 0x3b;
inline constexpr uint16_t kRegisterMusic = 0x3c;
inline constexpr uint16_t kSetMasterVolume = 0x3d;
inline constexpr uint16_t kSetMusicVolume = 0x3e;
inline constexpr uint16_t kSetSfxVolume = 0x3f;
inline constexpr uint16_t kStopMusic = 0x40;
inline constexpr uint16_t kUnregisterMusic = 0x41;
inline constexpr uint16_t kClearAllMusic = 0x42;
inline constexpr uint16_t kReadStorage = 0x43;
inline constexpr uint16_t kWriteStorage = 0x44;
inline constexpr uint16_t kattachSound = 0x45;
inline constexpr uint16_t kgetPan = 0x46;
inline constexpr uint16_t kgetVolume = 0x47;
inline constexpr uint16_t ksetPan = 0x48;
inline constexpr uint16_t ksetVolume = 0x49;
inline constexpr uint16_t ksetRGB = 0x4a;
inline constexpr uint16_t ksetTransform = 0x4b;
inline constexpr uint16_t kDown = 0x4c;
inline constexpr uint16_t kLeft = 0x4d;
inline constexpr uint16_t kRight = 0x4e;
inline constexpr uint16_t kUp = 0x4f;
inline constexpr uint16_t k_x = 0x50;
inline constexpr uint16_t k_y = 0x51;
inline constexpr uint16_t k_xscale = 0x52;
inline constexpr uint16_t k_yscale = 0x53;
inline constexpr uint16_t k_currentframe = 0x54;
inline constexpr uint16_t k_totalframes = 0x55;
inline constexpr uint16_t k_alpha = 0x56;
inline constexpr uint16_t k_visible = 0x57;
inline constexpr uint16_t k_width = 0x58;
inline constexpr uint16_t k_height = 0x59;
inline constexpr uint16_t k_rotation = 0x5a;
inline constexpr uint16_t k_target = 0x5b;
inline constexpr uint16_t k_framesloaded = 0x5c;
inline constexpr uint16_t k_name = 0x5d;
inline constexpr uint16_t k_droptarget = 0x5e;
inline constexpr uint16_t k_url = 0x5f;
inline constexpr uint16_t k_highquality = 0x60;
inline constexpr uint16_t k_focusrect = 0x61;
inline constexpr uint16_t k_soundbuftime = 0x62;
inline constexpr uint16_t k_quality = 0x63;
inline constexpr uint16_t kthis = 0x64;
inline constexpr uint16_t kxmax = 0x65;
inline constexpr uint16_t kntext = 0x66;
inline constexpr uint16_t ktext = 0x67;
inline constexpr uint16_t k_parent = 0x68;
inline constexpr uint16_t k_root = 0x69;
inline constexpr uint16_t kArray = 0x6a;
inline constexpr uint16_t kColor = 0x6b;
inline constexpr uint16_t kconsole_version = 0x6c;
inline constexpr uint16_t kKey = 0x6d;
inline constexpr uint16_t kMath = 0x6e;
inline constexpr uint16_t kObject = 0x6f;
inline constexpr uint16_t kSound = 0x70;
inline constexpr uint16_t kra = 0x71;
inline constexpr uint16_t kga = 0x72;
inline constexpr uint16_t kba = 0x73;
inline constexpr uint16_t kaa = 0x74;
inline constexpr uint16_t krb = 0x75;
inline constexpr uint16_t kgb = 0x76;
inline constexpr uint16_t kbb = 0x77;
inline constexpr uint16_t kab = 0x78;
inline constexpr uint16_t kx = 0x79;
inline constexpr uint16_t kxMax = 0x7a;
inline constexpr uint16_t kxMin = 0x7b;
inline constexpr uint16_t ky = 0x7c;
inline constexpr uint16_t kyMax = 0x7d;
inline constexpr uint16_t kyMin = 0x7e;
inline constexpr uint16_t k_level0 = 0x7f;
inline constexpr uint16_t kTRUE = 0x80;
inline constexpr uint16_t kFALSE = 0x81;
inline constexpr uint16_t kASSERT = 0x83;
inline constexpr uint16_t kChangeMovie = 0x84;
inline constexpr uint16_t kGetCheat = 0x85;
inline constexpr uint16_t kGetFlashGlobal = 0x86;
inline constexpr uint16_t kGetGore = 0x87;
inline constexpr uint16_t kGetLanguage = 0x88;
inline constexpr uint16_t kGetLocalSecond = 0x89;
inline constexpr uint16_t kGetRegion = 0x8a;
inline constexpr uint16_t kGetTextLocalizationScale = 0x8b;
inline constexpr uint16_t kGetWidescreen = 0x8c;
inline constexpr uint16_t kIsPalMode = 0x8d;
inline constexpr uint16_t kLOGPush = 0x8e;
inline constexpr uint16_t kQuitTo = 0x8f;
inline constexpr uint16_t kReadSaveGame = 0x90;
inline constexpr uint16_t kSetFlashController = 0x91;
inline constexpr uint16_t kSetFlashGlobal = 0x92;
inline constexpr uint16_t kSetTextboxAsInteger = 0x93;
inline constexpr uint16_t kSetTextNumeric = 0x94;
inline constexpr uint16_t kVibrateController = 0x95;
inline constexpr uint16_t kWriteSaveGame = 0x96;
inline constexpr uint16_t kShowLoadIcon = 0x97;
inline constexpr uint16_t kHideLoadIcon = 0x98;
inline constexpr uint16_t kf_LocalToGame = 0x99;
inline constexpr uint16_t kPreloadNews = 0x9a;
inline constexpr uint16_t kSetMarketOffer = 0x9b;
inline constexpr uint16_t kf_Quantize = 0x9c;
inline constexpr uint16_t kGetLocalGamerTag = 0x9d;
inline constexpr uint16_t kSetTextToGamerTag = 0x9e;
inline constexpr uint16_t kIsFullGame = 0x9f;
inline constexpr uint16_t kReturnToArcade = 0xa0;
inline constexpr uint16_t kRichPresence = 0xa1;
inline constexpr uint16_t kSetRichPresence = 0xa2;
inline constexpr uint16_t kSetScores = 0xa3;
inline constexpr uint16_t kShowSignInScreen = 0xa4;
inline constexpr uint16_t kUnlockAchievement = 0xa5;
inline constexpr uint16_t kUnlockFullGame = 0xa6;
inline constexpr uint16_t kUnlockAvatarItem = 0xa7;
inline constexpr uint16_t kFlushStats = 0xa8;
inline constexpr uint16_t kGetGameMode = 0xa9;
inline constexpr uint16_t kIsLocalPlayerInSession = 0xaa;
inline constexpr uint16_t kIsPlayerSignedIn = 0xab;
inline constexpr uint16_t kIsPortTalking = 0xac;
inline constexpr uint16_t kJoinSession = 0xad;
inline constexpr uint16_t kLeaveSession = 0xae;
inline constexpr uint16_t kStartSession = 0xaf;
inline constexpr uint16_t kEndSession = 0xb0;
inline constexpr uint16_t kWriteAllStats = 0xb1;
inline constexpr uint16_t kIsSameProfile = 0xb2;
inline constexpr uint16_t kIsNetGame = 0xb3;
inline constexpr uint16_t kIsRankedMatch = 0xb4;
inline constexpr uint16_t kIsInviteWaiting = 0xb5;
inline constexpr uint16_t kSetPortState = 0xb6;
inline constexpr uint16_t kShowGamerTag = 0xb7;
inline constexpr uint16_t kSetKeyboardMapping = 0xb8;
inline constexpr uint16_t kShowMouse = 0xb9;
inline constexpr uint16_t kf_BSPCheckLastHitIndex = 0xbc;
inline constexpr uint16_t kf_BSPCheckLastHitType = 0xbd;
inline constexpr uint16_t kf_BSPCheckLastHitSlope = 0xbe;
inline constexpr uint16_t kf_BSPHitTest = 0xbf;
inline constexpr uint16_t kf_BSPLoadLevel = 0xc0;
inline constexpr uint16_t kf_GetClosestWaypoint = 0xc1;
inline constexpr uint16_t kf_GetWPHit = 0xc2;
inline constexpr uint16_t kf_GetWPX = 0xc3;
inline constexpr uint16_t kf_GetWPY = 0xc4;
inline constexpr uint16_t kf_SetWPHit = 0xc5;
inline constexpr uint16_t kg_nPort1State = 0xc7;
inline constexpr uint16_t kg_nPort2State = 0xc8;
inline constexpr uint16_t kg_nPort3State = 0xc9;
inline constexpr uint16_t kg_nPort4State = 0xca;
inline constexpr uint16_t kg_nPlayer1Port = 0xcb;
inline constexpr uint16_t kg_nPlayer2Port = 0xcc;
inline constexpr uint16_t kg_nPlayer3Port = 0xcd;
inline constexpr uint16_t kg_nPlayer4Port = 0xce;
inline constexpr uint16_t kg_nPort1CharId = 0xcf;
inline constexpr uint16_t kg_nPort2CharId = 0xd0;
inline constexpr uint16_t kg_nPort3CharId = 0xd1;
inline constexpr uint16_t kg_nPort4CharId = 0xd2;
inline constexpr uint16_t kg_nArenaPoints = 0xd3;
inline constexpr uint16_t kg_nGoodExit = 0xd4;
inline constexpr uint16_t kg_nNumGamesPlayed = 0xd5;
inline constexpr uint16_t kg_nPDLC = 0xd6;
inline constexpr uint16_t kg_bAllowHiddenCharacters = 0xd7;
inline constexpr uint16_t kg_nArenaMode = 0xd8;
inline constexpr uint16_t kg_bStartedArena = 0xd9;
inline constexpr uint16_t kg_bStartedQuaff = 0xda;
inline constexpr uint16_t kg_bNoPause = 0xdb;
inline constexpr uint16_t kg_nPausePlayer = 0xdc;
inline constexpr uint16_t kquit = 0xdd;
inline constexpr uint16_t kquitto = 0xde;
inline constexpr uint16_t kg_bExitGame = 0xdf;
inline constexpr uint16_t kg_bLoadIcon = 0xe0;
inline constexpr uint16_t kg_bLoading = 0xe1;
inline constexpr uint16_t kg_bReadyToLoad = 0xe2;
inline constexpr uint16_t kg_bNoLoadingScreen = 0xe3;
inline constexpr uint16_t kg_bMap = 0xe4;
inline constexpr uint16_t kg_bMenu = 0xe5;
inline constexpr uint16_t kg_bAllowInvites = 0xe6;
inline constexpr uint16_t kf_ChangeLevel = 0xe7;
inline constexpr uint16_t kf_WeaponStats = 0xe8;
inline constexpr uint16_t kgame = 0xe9;
inline constexpr uint16_t kf_Depth = 0xeb;
inline constexpr uint16_t kf_LargeObjectRanges = 0xec;
inline constexpr uint16_t kloader = 0xee;
inline constexpr uint16_t kp_game = 0xef;
inline constexpr uint16_t kcurrent_depth = 0xf0;
inline constexpr uint16_t kdepth_mod = 0xf1;
inline constexpr uint16_t kabs_bottom = 0xf2;
inline constexpr uint16_t ktop = 0xf3;
inline constexpr uint16_t kbottom = 0xf4;
inline constexpr uint16_t kleft = 0xf5;
inline constexpr uint16_t kright = 0xf6;
inline constexpr uint16_t kw = 0xf7;
inline constexpr uint16_t kh = 0xf8;
inline constexpr uint16_t kzone = 0xf9;
inline constexpr uint16_t kplayer_pt = 0xfa;
inline constexpr uint16_t khud1 = 0xfb;
inline constexpr uint16_t khud2 = 0xfc;
inline constexpr uint16_t khud3 = 0xfd;
inline constexpr uint16_t khud4 = 0xfe;
inline constexpr uint16_t kport = 0xff;
inline constexpr uint16_t kanimal_unlocks = 0x100;
inline constexpr uint16_t kitem_unlocks = 0x101;
inline constexpr uint16_t kitem_unlocks_expansion = 0x102;
inline constexpr uint16_t kweapon = 0x103;
inline constexpr uint16_t kweapon_strength = 0x104;
inline constexpr uint16_t kweapon_defense = 0x105;
inline constexpr uint16_t kweapon_magic = 0x106;
inline constexpr uint16_t kweapon_agility = 0x107;
inline constexpr uint16_t kweapon_critical = 0x108;
inline constexpr uint16_t kweapon_magic_type = 0x109;
inline constexpr uint16_t kweapon_level = 0x10a;
inline constexpr uint16_t kpet = 0x10b;
inline constexpr uint16_t kanimal_type = 0x10c;
}  // namespace name

}  // namespace as
