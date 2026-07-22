# Menu Sort Aroma

<img src="assets/logo.png" alt="Menu Sort Aroma icon" width="128" height="128">

Unofficial port of [Menu Sort](https://github.com/doino-gretchenliev/menu_sort) (originally by
Yardape8000 and doino-gretchenliev) from the legacy Homebrew Launcher toolchain to the modern
[Aroma](https://aroma.foryour.cafe/) environment, using [wut](https://github.com/devkitPro/wut) and
[libmocha](https://github.com/wiiu-env/libmocha) instead of the old `dynamic_libs`/`libiosuhax` stack.

Ported and maintained by [Yirr777](https://github.com/Yirr777).

With Menu Sort Aroma you can alphabetically sort icons on the Wii U Menu, per user account -
including inside folders, and homebrew icons injected by the
[Homebrew On Wii U Menu plugin](https://github.com/wiiu-env/homebrew_on_menu_plugin).

The following items will not move (though a folder's *contents* still get sorted): folders and
system icons (Disc, Settings, etc), the Homebrew Launcher, any IDs listed in `dontmove.txt`, and any
homebrew app whose name matches an entry in `dontmove_homebrew.txt`.

**Warning**: always use the backup option (`+`) before sorting anything for the first time. Bricked
consoles are not the author's (or this port's) responsibility.

## Controls

| Button | Action |
| --- | --- |
| `B` | Standard sorting |
| `A` | Standard sorting, ignoring a leading "The" |
| `X` | Bad naming mode sorting |
| `Y` | Bad naming mode sorting, ignoring a leading "The" |
| `+` | Backup the current order (including folders) |
| `-` | Restore the last backup (including folders) |
| `L` | Count items only, no changes |
| `HOME` | Exit without doing anything |

## Backups

Whenever an actual sort runs (any of `B`/`A`/`X`/`Y` — not `L`), Menu Sort Aroma automatically backs up the
current, unmodified order to `sd:/wiiu/apps/menu_sort_aroma/BaristaAccountSaveFile.dat` before touching
anything, whether or not you also pressed `+` first. Press `-` at any time to restore that backup.

## Homebrew On Wii U Menu plugin compatibility

If the [Homebrew On Wii U Menu plugin](https://github.com/wiiu-env/homebrew_on_menu_plugin) is
installed, it redirects the Wii U Menu's save data to its own copy on the SD card so it can inject
homebrew icons without touching the real system save. Menu Sort Aroma detects this and reads/writes that
same SD-card copy instead of the raw system save file, then mirrors the result back to the system
save - so both stay in sync and you never lose folders or icons the plugin hasn't flushed yet.

## What changed from the original

- `dynamic_libs` (runtime function-pointer loading) → native [wut](https://github.com/devkitPro/wut)
  headers (`coreinit`, `vpad/input.h`, `whb/proc.h`).
- Raw IOSUHAX / `libiosuhax` / MCP thread hijacking → [libmocha](https://github.com/wiiu-env/libmocha)
  (`Mocha_MountFS` / `Mocha_UnmountFS`).
- Manual SD card FAT mounting → dropped; wut mounts the SD card automatically at
  `fs:/vol/external01/...`.
- `libxml2` → a small hand-rolled XML text extractor (`src/utils/xmltext.c`), since libxml2 isn't part
  of the standard `wiiu-dev` package group.
- `nn_act_*` → `src/act_wrapper.cpp`, a tiny `extern "C"` shim, since wut only exposes `nn::act` in C++.
- Output on both the TV and GamePad screens (the original only wrote to the GamePad).
- Builds to a `.wuhb` (Aroma Homebrew Launcher) instead of a legacy `.elf`.
- Automatic backup before every real sort, in addition to the original's manual `+` backup.
- Homebrew icons injected by the
  [Homebrew On Wii U Menu plugin](https://github.com/wiiu-env/homebrew_on_menu_plugin) - which didn't
  exist when the original was written - are now sorted too, with their own exclude list
  (`dontmove_homebrew.txt`) and compatibility with the plugin's SD-card save redirection.

## Building

Requires [devkitPro](https://devkitpro.org/wiki/Getting_Started) with the `wiiu-dev` package group,
plus [libmocha](https://github.com/wiiu-env/libmocha) and
[libwuhbutils](https://github.com/wiiu-env/libwuhbutils) built from source and installed
(`git clone` each repo, then `make install` in it).

```
export DEVKITPRO=/opt/devkitpro
export DEVKITPPC=$DEVKITPRO/devkitPPC
export PATH="$DEVKITPPC/bin:$DEVKITPRO/tools/bin:$PATH"
make
```

Produces `menu_sort_aroma.wuhb`.

## Installing

Copy `menu_sort_aroma.wuhb`, `dontmove.txt`, `titlesmap.psv` and `dontmove_homebrew.txt` to
`sd:/wiiu/apps/menu_sort_aroma/` on your SD card. Edit them as described below, then launch the app
from the Aroma Homebrew Launcher.

**Upgrading from an older version installed under `sd:/wiiu/apps/menu_sort/`?** Move that whole
folder to `sd:/wiiu/apps/menu_sort_aroma/` (keeping your customized `dontmove.txt` /
`titlesmap.psv` / `dontmove_homebrew.txt`), then delete the old empty `menu_sort` folder. The
Homebrew Launcher will list it as a new entry either way.

### Don't Move list

Override `dontmove.txt` with a list of non-movable icons, identified by the last 4 bytes of the title
ID:

```
10179B00 # Brain Age: Train Your Brain In Minutes A Day
10105A00 # Netflix
10105700 # YouTube
```

You can also use `dontmoveX.txt` (`X`: 0-9, A, or B) for per-user-profile lists — `X` is the last
digit of the console's `8000000X` save folder for that user (shown by the app as `User ID: X`).
`dontmoveX.txt` takes priority over `dontmove.txt`; only one file is used per profile.

### Titles map

Override `titlesmap.psv` to sort by group name instead of title name ("bad naming mode"):

```
titleId#1|groupName
titleId#2|groupName
```

`titlesmapX.psv` works the same way as `dontmoveX.txt` for per-profile overrides.

### Homebrew exclude list

Homebrew icons injected by the
[Homebrew On Wii U Menu plugin](https://github.com/wiiu-env/homebrew_on_menu_plugin) don't have a
fixed title ID the way installed titles do (it's a hash of wherever you happen to have installed
them), so `dontmove.txt` can't target them reliably across different setups. Instead, edit
`dontmove_homebrew.txt` with one app name (or part of one) per line - any homebrew icon whose
resolved name contains that text (case-insensitive) is left in place:

```
Aroma Updater
Payload-Loader Installer
NUSspli
```

Ships with only Menu Sort Aroma itself excluded by default - add whichever other homebrew apps you want
left in place, one per line, no rebuild required.

## License

Mixed, per the original project. See the [original repository](https://github.com/doino-gretchenliev/menu_sort)
for details.
