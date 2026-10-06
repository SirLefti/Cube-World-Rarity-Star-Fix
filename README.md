# Cube World Rarity Star Fix

Items of rarity `uncommon`, `rare`, `epic` and `legendary` are marked with a
coloured star in the bottom right corner of the item preview panel. That is the
same corner the price goes into when trading, so the star sits on top of it.

This mod moves the star to the bottom left corner of the same panel, the same
distance in from the edge.

This mod is for the **alpha** build of Cube World.

## Installation

* requires [coremaze Cube-World-Mod-Launcher](https://github.com/coremaze/Cube-World-Mod-Launcher/releases/tag/v1.5)
* unpack Mod Launcher in the game directory
* put `RarityStarFix.dll` into `Mods`
* start the game via `CubeModLauncher.exe`

## Compilation

Requires a compiler that targets 32-bit Windows. Due to the `asm` style, `MSVC`
will not work, use `GCC` or `Clang`. Run `make` in this directory and specify
the compiler as `CXX` variable if necessary (e.g.
`make CXX=i686-w64-mingw32-g++`).
