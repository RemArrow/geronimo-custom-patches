// Custom Patches - GML plugin (com.remarrow.patches).
//
// Adds patches to the kit station's patch menu.
//
// How the game does patches (read from BP_GearManagerComponent and WB_CustomizationCategoryDatabase):
// BP_GameInstance.Patches is a list of patch textures (39); the station's menu shows one button per entry.
// Picking one calls the gear manager's ApplyPatch(Type, Patch), which keeps a map patch slot -> texture.
// Building the outfit, each patch's ID is its texture's index in BP_GameInstance.Patches, and the outfit
// material reads that slice of /Game/Characters/Common/PatchesTattoos (1024x1024 DXT1, an atlas whose
// rectangle x 28-495, y 328-991 is the patch; the rest is shared by every slice).
//
// So each new patch is:
//   1. a slice in PatchesTattoos - Paks\zzz_Patches_P is that package with the game's 39 slices untouched
//      and ours appended (build.bat; Utils\UAssetParser patchprep + texarray add); GML installs it;
//   2. its picture appended to BP_GameInstance.Patches, in the same order - a runtime texture built into
//      this DLL (also the menu button). If the list doesn't have the 39 entries the array was built
//      against (a game update), nothing is added: the slices wouldn't match.
#include <GML/GML.hpp>
#include <windows.h>
#include <sstream>
#include <fstream>
#include "version.h"

using namespace gml;

GML_PLUGIN("com.remarrow.patches", "Custom Patches", PATCHES_VERSION);

static constexpr int kGamePatches = 39;
static std::vector<std::string> s_names;
static std::vector<GUObject*> s_tex;
static bool s_done = false, s_refused = false;

static bool MakeTextures() {
    if (!s_tex.empty()) return true;
    Blob names = Resource("NAMES");
    if (!names) { Error("the patch list is missing from the DLL"); return false; }
    std::istringstream in(std::string((const char*)names.data, names.size));
    for (std::string n; std::getline(in, n);) {
        while (!n.empty() && (n.back() == '\r' || n.back() == ' ')) n.pop_back();
        if (!n.empty()) s_names.push_back(n);
    }
    for (size_t i = 0; i < s_names.size(); i++) {
        Blob b = Resource(std::format("PATCH{}", i).c_str());
        GUObject* t = b ? API->ImportTextureFromMemory(b.data, b.size, ("Patch_" + s_names[i]).c_str()) : nullptr;
        if (!t) { Error("could not make the texture for patch '{}'", s_names[i]); s_tex.clear(); return false; }
        s_tex.push_back(t);
    }
    return true;
}

static void AddToPatchList(Object gi) {
    auto* raw = (GML_TArray*)gi.PropPtr("Patches");
    if (!raw || !MakeTextures()) return;
    TArrayView<GUObject*> list{raw};
    if (list.Contains(s_tex[0])) {
        s_done = true;
        for (size_t i = 0; i < s_tex.size(); i++)
            if (list.Num() <= kGamePatches + (int)i || list[kGamePatches + (int)i] != s_tex[i]) s_done = false;
        return;
    }
    if (list.Num() != kGamePatches) {
        if (!s_refused) Error("the game's patch list has {} entries, not {}: the patch array in this plugin was built for "
                              "another version of the game, so no patches are added (their slices wouldn't match)", list.Num(), kGamePatches);
        s_refused = true;
        return;
    }
    for (size_t i = 0; i < s_tex.size(); i++) {
        if (!list.Add(s_tex[i])) { Error("could not add patch '{}'", s_names[i]); return; }
        Log("patch list: '{}' added as patch {}", s_names[i], kGamePatches + i);
    }
    s_done = true;
}

// ------------------------------------------------------------------ saved presets
//
// The game saves a patch preset (SavePatchSetup -> PatchesSerializable) as slot -> texture object path.
// Our textures are made at runtime under an engine-given name, so that path doesn't resolve after a
// restart and the slot comes back empty. So the plugin keeps its own record, GML\config\
// com.remarrow.patches.presets.txt ("preset slot patchName" per line): written after each save, and
// after each load our patches go back on the recorded slots.

static std::string PresetFile() {
    char path[MAX_PATH];
    HMODULE self = nullptr;
    GetModuleHandleExA(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS | GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT, (LPCSTR)&PresetFile, &self);
    GetModuleFileNameA(self, path, MAX_PATH);
    std::string p = path;  // ...\GML\plugins\Patches\Patches.dll
    for (int i = 0; i < 3; i++) p = p.substr(0, p.find_last_of('\\'));
    return p + "\\config\\com.remarrow.patches.presets.txt";
}

struct Rec { int preset, slot; std::string name; };

static std::vector<Rec> ReadRecs() {
    std::vector<Rec> r;
    std::ifstream in(PresetFile());
    for (Rec x; in >> x.preset >> x.slot >> x.name;) r.push_back(x);
    return r;
}

static int IntParam(GUFunction* fn, void* params, const char* name) {
    GML_PropInfo pi{};
    GFProperty* p = API->FindProperty((GUStruct*)fn, name);
    return p && API->GetPropertyInfo(p, &pi) ? *(int32_t*)((uint8_t*)params + pi.offset) : -1;
}

static void AfterSave(Object gear, GUFunction* fn, void* params) {
    int preset = IntParam(fn, params, "Preset");
    if (preset < 0 || s_tex.empty()) return;
    auto recs = ReadRecs();
    std::erase_if(recs, [&](const Rec& r) { return r.preset == preset; });
    struct Ctx { std::vector<Rec>* recs; int preset; } ctx{&recs, preset};
    GFProperty* prop = API->FindProperty(gear.Struct(), "Patches");
    if (prop) API->MapForEach(gear.PropPtr("Patches"), prop, [](void* u, void* key, void* val) -> int {
        auto* c = (Ctx*)u;
        for (size_t i = 0; i < s_tex.size(); i++)
            if (*(GUObject**)val == s_tex[i]) c->recs->push_back({c->preset, *(uint8_t*)key, s_names[i]});
        return 0;
    }, &ctx);
    std::ofstream out(PresetFile(), std::ios::trunc);
    for (auto& r : recs) out << r.preset << ' ' << r.slot << ' ' << r.name << '\n';
}

static void AfterLoad(Object gear, GUFunction* fn, void* params) {
    int preset = IntParam(fn, params, "Preset");
    if (preset < 0 || s_tex.empty()) return;
    for (auto& r : ReadRecs()) {
        if (r.preset != preset) continue;
        for (size_t i = 0; i < s_names.size(); i++) {
            if (s_names[i] != r.name) continue;
            Params a(gear, "ApplyPatch");
            a.Set("Type", (uint8_t)r.slot).SetObj("Patch", Object(s_tex[i])).Call();
            Log("preset {}: '{}' back on slot {}", preset, r.name, r.slot);
        }
    }
}

static void Check() {
    if (s_done || s_refused) return;
    if (Object gi = FindFirstOf("BP_GameInstance_C")) AddToPatchList(gi);
}

GML_AWAKE() {
    if (API->size < offsetof(GML_API, ImportTextureFromMemory) + sizeof(void*)) {
        Error("this plugin needs GML 2.2 or newer");
        return 1;
    }
    HookAfter("BP_GearManagerComponent_C:SavePatchSetup", AfterSave);
    HookAfter("BP_GearManagerComponent_C:LoadPatchSetup", AfterLoad);
    On(GML_EVENT_WORLD_BEGIN_PLAY, [](void*) { Check(); });
    On(GML_EVENT_TICK, [](void*) {
        static ULONGLONG next = 0;
        if (GetTickCount64() < next) return;
        next = GetTickCount64() + 2000;
        Check();
    });
    Log("Custom Patches {}", PATCHES_VERSION);
    return 0;
}
