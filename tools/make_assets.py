"""Makes assets/ (which git ignores) from your copy of Castle Crashers, for the
game to load.

    python tools/make_assets.py [--game <the game's folder>]

1. Finds the game: the folder given (--game, or CC_GAME in the environment:
   a depot download, say, which Steam doesn't list), else through Steam (app
   204360); and checks some of its files are the Steam copy's.
2. Makes assets/ from it:
   - swf/: the .pak archives decrypted, unwrapped from their COK6 wrappers and
     their scripts normalized (decrypt_pak.py, unwrap_cok6.py, normalize_swf.py);
   - png/: the pictures the archives hold;
   - bsp/, fonts/, shaders/, audio/music/, audio/sounds/: as the game has them;
   - text/: the game's text, from its castle.exe (extract_strings.py).

Nothing from the game is stored in this repository.
"""
import argparse
import hashlib
import os
import re
import shutil
import subprocess
import sys
from pathlib import Path

HERE = Path(__file__).resolve().parents[1]
TOOLS = Path(__file__).resolve().parent
ASSETS = HERE / 'assets'
WORK = HERE / 'build' / 'game'

STEAM_APP = 204360  # Castle Crashers
# The Steam copy's, checked before anything is made from it.
KNOWN_FILES = {
    'castle.exe': 'f39c3e6ff4600ca0452a908abe552c7d33cd3c9f27305ea766f02373da93490d',
    'data/game/main.pak': 'ac39e11817e35ff7fa7fd76927b2f468f94c13da7e12e16078bc03a28820405d',
    'data/game/player.pak': '522ae30f86ec49e4f9c35174c1b832ccca69fe8b913d6757110470f9ef1e65a7',
    'data/levels/level20.pak': '64f6dda6a977e98a703da4fc7419ad3bbead9389a2c105ea3df3073524ae41ff',
}


def check_game(game):
    """The folder, if it holds the game: its castle.exe and data/, and some
    files the Steam copy's. Exits, saying why, if not."""
    game = Path(game)
    if not (game / 'castle.exe').exists() or not (game / 'data').is_dir():
        sys.exit(f"{game} isn't Castle Crashers' folder (no castle.exe and data/ in it).")
    for name, digest in KNOWN_FILES.items():
        file = game / name
        if not file.exists() or hashlib.sha256(file.read_bytes()).hexdigest() != digest:
            sys.exit(f"{file} is missing or not the Steam copy's: a different build of the game, "
                     "or not all of it (verify the files in Steam, or download the depot again).")
    return game


def steam_game():
    """The game's folder, from Steam: Steam's path (the registry), its
    libraries (steamapps/libraryfolders.vdf), the library holding the app's
    manifest (appmanifest_204360.acf) and the folder that names; then its
    files checked. Exits, saying why, if any of it isn't there."""
    steam = None
    try:
        import winreg
        with winreg.OpenKey(winreg.HKEY_CURRENT_USER, r'Software\Valve\Steam') as key:
            steam = Path(winreg.QueryValueEx(key, 'SteamPath')[0])
    except (ImportError, OSError):
        pass
    if steam is None or not steam.exists():
        steam = Path(r'C:\Program Files (x86)\Steam')
    libraries = [steam]
    vdf = steam / 'steamapps' / 'libraryfolders.vdf'
    if vdf.exists():
        for path in re.findall(r'"path"\s+"([^"]+)"', vdf.read_text(encoding='utf-8', errors='replace')):
            libraries.append(Path(path.replace('\\\\', '\\')))  # the file doubles its backslashes
    for library in libraries:
        manifest = library / 'steamapps' / f'appmanifest_{STEAM_APP}.acf'
        if not manifest.exists():
            continue
        found = re.search(r'"installdir"\s+"([^"]+)"', manifest.read_text(encoding='utf-8', errors='replace'))
        game = library / 'steamapps' / 'common' / found.group(1) if found else None
        if game is None or not game.is_dir():
            sys.exit(f'Steam lists Castle Crashers ({manifest}) but its folder is missing: install it in Steam.')
        return check_game(game)
    sys.exit('Castle Crashers (Steam app 204360) is not installed through Steam on this PC: '
             'install it, or give the folder your copy is in with --game (a depot download, say).')


def find_game(chosen=None):
    """The game's folder: the one chosen (an argument, or CC_GAME), else Steam's."""
    chosen = chosen or os.environ.get('CC_GAME')
    return check_game(chosen) if chosen else steam_game()


def step(title, args):
    print(f'-- {title}', flush=True)
    subprocess.run([sys.executable, '-u'] + [str(a) for a in args], check=True)


def copy_files(src, dst, pattern='*'):
    dst.mkdir(parents=True, exist_ok=True)
    n = 0
    for f in sorted(src.rglob(pattern) if pattern.startswith('**') else src.glob(pattern)):
        if f.is_file():
            shutil.copy2(f, dst / f.name)
            n += 1
    return n


def main():
    parser = argparse.ArgumentParser(description='Makes assets/ from your copy of Castle Crashers.')
    parser.add_argument('--game', help="the game's folder (else CC_GAME, else Steam's)")
    game = find_game(parser.parse_args().game)
    print(f'The game: {game}', flush=True)
    for folder in (WORK, ASSETS):
        if folder.exists():
            shutil.rmtree(folder)
    pak, swf = WORK / 'pak', WORK / 'swf'
    step('decrypting the .pak archives', [TOOLS / 'decrypt_pak.py', '--game', game, '--out', pak])
    step('unwrapping them', [TOOLS / 'unwrap_cok6.py', '--pak', pak, '--out', swf])
    step('normalizing the scripts', [TOOLS / 'normalize_swf.py', '--swf', swf, '--out', ASSETS / 'swf'])
    step('the text', [TOOLS / 'extract_strings.py', '--exe', game / 'castle.exe', '--out', ASSETS / 'text'])
    print('-- the pictures, collision, fonts, shaders and sound', flush=True)
    data = game / 'data'
    print(f"   png {copy_files(swf, ASSETS / 'png', '**/*.png')}, "
          f"bsp {copy_files(pak / 'bsps', ASSETS / 'bsp', '*.pdag')}, "
          f"fonts {copy_files(data / 'fonts', ASSETS / 'fonts')}, "
          f"shaders {copy_files(data / 'shaders', ASSETS / 'shaders')}, "
          f"music {copy_files(data / 'music', ASSETS / 'audio' / 'music')}, "
          f"sounds {copy_files(data / 'sounds', ASSETS / 'audio' / 'sounds')}", flush=True)
    shutil.rmtree(WORK)
    print(f'The assets are made: {ASSETS}')


if __name__ == '__main__':
    main()
