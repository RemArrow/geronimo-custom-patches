# Custom Patches for Geronimo

A [GML](https://github.com/RemArrow/geronimo-mod-loader) plugin that adds patches to the kit station's patch
menu. Each picture in `Assets\` becomes one more patch after the game's 39. This build has three: **LarpTac**,
**Rogue** and **Stripes** (the orange and black shield).

## Install

Needs [GML 2.4 or newer](https://github.com/RemArrow/geronimo-mod-loader/releases). Install it from GML Manager's
Browse tab, or close the game, download `Patches-<version>.zip` from
[Releases](https://github.com/RemArrow/geronimo-custom-patches/releases) and extract it into
`...\GERONIMO\Geronimo\Binaries\Win64`. That adds `GML\plugins\Patches`. The new patches are at the end of the kit
station's patch menu.

The plugin replaces the game's patch texture array with a copy that has the new patches appended. After a game
update that changes the patch list, it adds nothing until it's rebuilt. Multiplayer is untested: other players
may see a default patch where you wear a custom one.

## How patches work in Geronimo

Read from `BP_GearManagerComponent` and `WB_CustomizationCategoryDatabase`:

- `BP_GameInstance.Patches` is a list of 39 patch textures. The station's patch menu shows one button per
  entry.
- Picking one calls the gear manager's `ApplyPatch(Type, Patch)`, which keeps a map from patch slot to
  texture. Kit items with patch areas list their slots in their own `Patches` map.
- When the outfit is built, each patch's ID is its texture's index in `BP_GameInstance.Patches`. The outfit
  material reads that slice of `/Game/Characters/Common/PatchesTattoos`, a texture array of 1024x1024
  DXT1 slices with one mip. Each slice is an atlas: the rectangle x 28-495, y 328-991 is the patch, and
  the rest is the same in every slice.

## What the plugin does

1. **New slices.** `Paks\zzz_Patches_P` is the game's patch array package with its 39 slices untouched and
   ours appended (slice 39, 40 and so on). GML copies it to `Content\Paks\~GML` at launch, where it
   overrides the game's package.
2. **The patch list.** Each picture, as a runtime texture built into the DLL (it is also the menu button),
   is appended to `BP_GameInstance.Patches` in the same order. If the list doesn't have exactly 39
   entries (a game update), nothing is added, because the slices wouldn't line up.

## Build and install

```bat
rem put the pictures in Assets\<Name>.png (any size; fitted to the 468x664 patch area, aspect kept, on black)
build.bat install
```

`build.bat`:
- renders the game's slice 0 to get the atlas around the patch (`Utils\UAssetParser textures`);
- builds one slice per picture (`patchprep`);
- extracts the game's array with retoc and appends the slices (`texarray add`);
- packs the container (`retoc to-zen`) and compiles the plugin.

Install copies `Patches.dll` and `Paks\` to `...\Binaries\Win64\GML\plugins\Patches\`. To remove the
plugin, delete that folder.

`tests\run_test.ps1` runs `tests\PatchesDev` in the game, flat in a window. It snapshots and restores
SaveGames, the GML configs and the plugins folder. The harness puts on a kit item with patch slots,
applies each patch to every slot, captures the player (base colour) to `build\shots`, and saves and
reloads a patch preset.

## Pictures

Only use pictures you made or have the right to use. LarpTac, Rogue and Stripes were supplied by the author. The
pictures aren't in this repository (`Assets\` is gitignored); the release zip carries them built in.
