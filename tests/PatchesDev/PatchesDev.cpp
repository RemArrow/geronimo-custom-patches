// PatchesDev - in-game test of the Custom Patches plugin. Not shipped; installed only for a run.
// Loads the team room; logs the patch list and which kit items have patch slots; puts such an item on the
// player and applies each custom patch (then a game patch to compare) to all its slots through the gear
// manager, rendering the player with a scene capture (base colour) to PNGs in [Test] ShotDir. Then saves the
// patch setup to a preset, swaps the patch, loads the preset back and logs whether the custom patch returns.
#include <GML/GML.hpp>
#include <windows.h>
#include <algorithm>
#include <cmath>
#include <vector>

using namespace gml;

GML_PLUGIN("com.remarrow.patchesdev", "PatchesDev", "0.1.0", {{"com.remarrow.patches", "0.1.0", GML_DEPENDENCY_SOFT}});

struct Vec { double x, y, z; };
struct Rot { double pitch, yaw, roll; };

static std::string s_shotDir;
static int s_step = -1;
static ULONGLONG s_at = 0;
static Object s_gear, s_item, s_rt, s_cap, s_gi;
static std::vector<uint8_t> s_types;
static std::vector<GUObject*> s_ours;

static std::string NameStr(uint64_t n) { char b[128] = ""; API->NameToString(n, b, sizeof b); return b; }
static std::wstring W(const std::string& s) { return std::wstring(s.begin(), s.end()); }

static Object Pawn() {
    Params p(Lib("GameplayStatics"), "GetPlayerPawn");
    p.SetObj("WorldContextObject", Object(API->WorldContext())).Set("PlayerIndex", (int32_t)0).Call();
    return p.Return<GUObject*>();
}

static std::vector<uint8_t> PatchKeys(Object o) {
    std::vector<uint8_t> keys;
    GFProperty* prop = API->FindProperty(o.Struct(), "Patches");
    void* map = o.PropPtr("Patches");
    if (!prop || !map) return keys;
    API->MapForEach(map, prop, [](void* u, void* key, void*) -> int {
        ((std::vector<uint8_t>*)u)->push_back(*(uint8_t*)key);
        return 0;
    }, &keys);
    return keys;
}

static std::string GearPatches() {
    std::string s;
    GFProperty* prop = API->FindProperty(s_gear.Struct(), "Patches");
    API->MapForEach(s_gear.PropPtr("Patches"), prop, [](void* u, void* key, void* val) -> int {
        Object t(*(GUObject**)val);
        *(std::string*)u += std::format("{}={} ", *(uint8_t*)key, t ? t.Name() : "null");
        return 0;
    }, &s);
    return s;
}

static void ApplyAll(GUObject* tex) {
    for (uint8_t t : s_types) {
        Params a(s_gear, "ApplyPatch");
        a.Set("Type", t).SetObj("Patch", Object(tex)).Call();
    }
    Log("APPLY {} to {} slots -> gear: {}", Object(tex).Name(), s_types.size(), GearPatches());
}

static void EnsureCapture() {
    if (s_rt) return;
    Params rt(Lib("KismetRenderingLibrary"), "CreateRenderTarget2D");
    rt.SetObj("WorldContextObject", Object(API->WorldContext())).Set("Width", (int32_t)1024).Set("Height", (int32_t)1024)
        .Set("Format", (uint8_t)2).Call();
    s_rt = rt.Return<GUObject*>();
    Params mk(Lib("KismetMathLibrary"), "MakeTransform");
    mk.Set("Scale", Vec{1, 1, 1}).Call();
    GML_PropInfo xi;
    API->GetPropertyInfo(API->FindProperty((GUStruct*)API->FindFunction(Lib("KismetMathLibrary").Struct(), "MakeTransform"), "ReturnValue"), &xi);
    Params b(Lib("GameplayStatics"), "BeginDeferredActorSpawnFromClass");
    b.SetObj("WorldContextObject", Object(API->WorldContext())).Set("ActorClass", FindClass("SceneCapture2D")).Set("CollisionHandlingOverride", (uint8_t)1);
    std::memcpy(b.Ptr("SpawnTransform"), mk.Ptr("ReturnValue"), xi.size);
    b.Call();
    Object actor = b.Return<GUObject*>();
    Params f(Lib("GameplayStatics"), "FinishSpawningActor");
    f.SetObj("Actor", actor);
    std::memcpy(f.Ptr("SpawnTransform"), mk.Ptr("ReturnValue"), xi.size);
    f.Call();
    s_cap = actor.GetObj("CaptureComponent2D");
    s_cap.Set<GUObject*>("TextureTarget", s_rt.ptr);
    s_cap.Set("CaptureSource", (uint8_t)7);
    s_cap.SetBool("bCaptureEveryFrame", false);
    s_cap.Set("FOVAngle", 40.0f);
}

static void Capture(const std::string& file) {
    EnsureCapture();
    Object pawn = Pawn();
    Vec o{};
    Params gc(pawn, "K2_GetComponentsByClass");
    gc.Set("ComponentClass", FindClass("SkeletalMeshComponent")).Call();
    auto* arr = (GML_TArray*)gc.Ptr("ReturnValue");
    for (int i = 0; arr && i < arr->Num; i++) {
        Object c(((GUObject**)arr->Data)[i]);
        Params bi(c, "GetBoneIndex");
        *(uint64_t*)bi.Ptr("BoneName") = API->MakeName("spine_03");
        bi.Call();
        if (bi.Return<int32_t>() < 0) continue;
        Params sl(c, "GetSocketLocation");
        sl.Set("InSocketName", API->MakeName("spine_03")).Call();
        o = sl.Return<Vec>();
        break;
    }
    double reach = 130;
    Params ar(pawn, "K2_GetActorRotation");
    ar.Call();
    double yaw = ar.Return<Rot>().yaw * 3.14159265358979 / 180;
    struct View { const char* name; Vec at; } views[] = {
        {"front", {o.x + std::cos(yaw) * reach, o.y + std::sin(yaw) * reach, o.z + reach * 0.2}},
        {"left", {o.x + std::sin(yaw) * reach, o.y - std::cos(yaw) * reach, o.z + reach * 0.2}},
        {"right", {o.x - std::sin(yaw) * reach, o.y + std::cos(yaw) * reach, o.z + reach * 0.2}},
        {"back", {o.x - std::cos(yaw) * reach, o.y - std::sin(yaw) * reach, o.z + reach * 0.2}},
        {"above", {o.x + std::cos(yaw) * reach * 0.2, o.y + std::sin(yaw) * reach * 0.2, o.z + reach}},
    };
    for (auto& v : views) {
        Params look(Lib("KismetMathLibrary"), "FindLookAtRotation");
        look.Set("Start", v.at).Set("Target", o).Call();
        Params mv(Object(API->GetOuter(s_cap.ptr)), "K2_SetActorLocationAndRotation");
        mv.Set("NewLocation", v.at).Set("NewRotation", look.Return<Rot>()).Set("bSweep", false).Set("bTeleport", true).Call();
        Params cs(s_cap, "CaptureScene");
        cs.Call();
        Params ex(Lib("KismetRenderingLibrary"), "ExportRenderTarget");
        ex.SetObj("WorldContextObject", Object(API->WorldContext())).SetObj("TextureRenderTarget", s_rt)
            .SetString("FilePath", W(s_shotDir)).SetString("Filename", W(file + "_" + v.name + ".png")).Call();
    }
    Log("SHOT {}", file);
}

static void Wear(Object item) {
    uint64_t color = 0;
    GFProperty* cp = API->FindProperty(item.Struct(), "Colors");
    if (cp) API->MapForEach(item.PropPtr("Colors"), cp, [](void* u, void* key, void*) -> int { *(uint64_t*)u = *(uint64_t*)key; return 1; }, &color);
    struct { GUObject* data; uint64_t color; } gi{item.ptr, color};
    Params so(s_gear, "SetOutfitItem");
    std::memcpy(so.Ptr("GearItem"), &gi, sizeof gi);
    so.Call();
    Log("WEAR {} ({})", item.Name(), NameStr(color));
}

static void Step() {
    if (GetTickCount64() < s_at) return;
    auto next = [](int st, int ms) { s_step = st; s_at = GetTickCount64() + ms; };
    switch (s_step) {
        case 0: {
            s_gi = FindFirstOf("BP_GameInstance_C");
            Object pawn = Pawn();
            for (auto& g : FindAllOf("BP_GearManagerComponent_C"))
                if (API->GetOuter(g.ptr) == pawn.ptr) s_gear = g;
            if (!s_gi || !s_gear) { Error("missing: gi {} gear {}", s_gi.Name(), s_gear.Name()); s_step = 99; return; }
            TArrayView<GUObject*> list{(GML_TArray*)s_gi.PropPtr("Patches")};
            std::string tail;
            for (int i = (std::max)(0, list.Num() - 4); i < list.Num(); i++) tail += std::format("[{}] {} ", i, Object(list[i]).Name());
            Log("PATCH LIST {} entries: {}", list.Num(), tail);
            for (int i = 39; i < list.Num(); i++) s_ours.push_back(list[i]);
            Log("GEAR PATCHES at start: {}", GearPatches());
            Object chest;
            for (auto& it : FindAllOf("OutfitItem_C")) {
                auto k = PatchKeys(it);
                if (k.empty()) continue;
                std::string ks;
                for (auto b : k) ks += std::format("{} ", b);
                Log("ITEM WITH PATCH SLOTS {} (category {}): {}", it.Name(), it.Get<uint8_t>("Category"), ks);
                if (!s_item || it.Name().find("JPC") != std::string::npos) { s_item = it; s_types = k; }
            }
            if (!s_item || s_ours.empty()) { Error("nothing to test: item {} ours {}", s_item.Name(), s_ours.size()); s_step = 99; return; }
            Wear(s_item);
            for (uint8_t t : PatchKeys(s_gear))  // also the slots already in use (the helmet's)
                if (std::find(s_types.begin(), s_types.end(), t) == s_types.end()) s_types.push_back(t);
            next(1, 4000);
            return;
        }
        case 1: ApplyAll(s_ours[0]); next(2, 4000); return;
        case 2: Capture("patch_" + Object(s_ours[0]).Name()); if (s_ours.size() > 1) ApplyAll(s_ours[1]); next(3, 4000); return;
        case 3: if (s_ours.size() > 1) Capture("patch_" + Object(s_ours[1]).Name());
                ApplyAll(TArrayView<GUObject*>{(GML_TArray*)s_gi.PropPtr("Patches")}[5]); next(4, 4000); return;
        case 4: {
            Capture("patch_game5");
            // Save / load: our patch into preset 7, a game patch on, then preset 7 back from disk.
            ApplyAll(s_ours.back());
            Params sv(s_gear, "SavePatchSetup");
            sv.Set("Preset", (int32_t)7).Call();
            ApplyAll(TArrayView<GUObject*>{(GML_TArray*)s_gi.PropPtr("Patches")}[5]);
            Params ld(s_gear, "LoadPatchSetup");
            ld.Set("Preset", (int32_t)7).Call();
            Log("AFTER LOAD of preset 7: {} (expect {})", GearPatches(), Object(s_ours.back()).Name());
            next(5, 3000);
            return;
        }
        case 5: Capture("patch_loaded"); Log("DEV TEST COMPLETE"); s_step = 99; return;
    }
}

GML_AWAKE() {
    s_shotDir = Config.Bind<std::string>("Test", "ShotDir", "", "Where the captures go.").Value();
    On(GML_EVENT_WORLD_BEGIN_PLAY, [](void* gm) {
        std::string level = Object((GUObject*)gm).Path();
        if (level.find("/Game/Maps/MainMenu/") == 0) {
            Params p(Lib("GameplayStatics"), "OpenLevel");
            p.SetObj("WorldContextObject", Object(API->WorldContext())).Set("LevelName", API->MakeName("/Game/Maps/TeamRoom/Teamroom")).Set("bAbsolute", true).Call();
        } else if (level.find("Teamroom") != std::string::npos) {
            s_step = 0;
            s_at = GetTickCount64() + 20000;
        }
    });
    On(GML_EVENT_TICK, [](void*) { if (s_step >= 0 && s_step < 99) Step(); });
    return 0;
}
