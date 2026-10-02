# GameCube mod

Swaps the Xbox 360 controller on the logo screen ("We recommend a controller
to play!") for a GameCube controller, keeping its movement and its white
shine as it appears.

```sh
build/Release/castle.exe logo --mod gamecube
```

In `logo.swf` the controller is sprite 34: body shape 19, face shape 20 and
shine shapes 21-33. The shine flashes the controller white as it appears:
white rises through the body under the face, covers the face, then fades.
`logo/mod.txt` measures that flash from the original shapes frame by frame
(with the face hiding the white beneath it, as in the original), widens it
by 34 px to cover the GameCube controller's different outline, and plays it
over the new controller, clipped to its outline. The picture replaces the
body at 1.05x the original's size, and the Xbox face and shine are hidden.

`logo/controller.png` is `source/gamecube-controller.png` (already
transparent and drawn at the original controller's angle) at 4x the original
graphic's size, so it stays sharp when the window is large:

```sh
py -3 tools/mod_image.py mods/gamecube/source/gamecube-controller.png mods/gamecube/logo/controller.png --fit 988x828
```
