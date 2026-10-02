# Mods

Optional replacement graphics, off unless asked for, so the default game stays
1:1 with the original:

```sh
build/Release/castle.exe --mod NAME
```

A mod is a folder `mods/NAME/` with one folder per movie (`logo/`, `level20/`,
...). Each movie folder holds pictures (PNG) and a `mod.txt` of commands, one
per line (`#` starts a comment):

| Command | What it does |
|---|---|
| `replace ID FILE [scale S] [nearest]` | Draws FILE instead of character ID, fitted and centred into its bounds, S times larger; with `nearest`, its pixels unsmoothed |
| `hide ID...` | Doesn't draw these characters |
| `clip IDS to ID [boxes]` | Draws IDS (such as `21-31`, or a list) only inside the silhouette of ID's instance in the same clip; if ID was replaced, IDS are stretched from its old bounds to the picture's. With `boxes`, each shape is drawn as its bounding box in its first fill colour |
| `flash SPRITE shine IDS face ID over ID [grow PX]` | Measures the white flash the shine shapes IDS make in SPRITE, frame by frame, widens it by PX pixels and plays it over ID's instance, clipped to it. Put it before `hide` and `replace` |

Without a `mod.txt`, every `ID.png` in the folder replaces character ID.
Placements, movement and animation of the characters are kept. The engine's
side is `engine/player/mods.h` and `mods.cpp`.
