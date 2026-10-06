// Cube World Alpha - Rarity Star Fix
//
// Items of rarity uncommon, rare, epic and legendary get a star drawn in the
// BOTTOM RIGHT corner of the item preview panel - the same corner as the
// price when trading, which is covered by the stars.
//
// This mod moves the star to the bottom LEFT corner of the same panel.

// ---- build requirements -----------------------------------------------------
#include <stdint.h>   // UINTPTR_MAX - pointer width, checked portably

#if !defined(_WIN32)
#  error "Target must be Windows: this is a DLL injected into Cube.exe. Cross-compile, e.g. i686-w64-mingw32-g++."
#endif
#if UINTPTR_MAX != 0xFFFFFFFFu
#  error "Target must be 32-bit: Cube.exe is PE32."
#endif
#if defined(__GNUC__) && !defined(__clang__) && __GNUC__ < 8
#  error "GCC 8 or newer: older GCC silently IGNORES __attribute__((naked)) on x86 and builds a broken DLL."
#endif

#include <windows.h>
#include <math.h>    // fabsf

// we move by exactly this amount of pixels to the left
static const float SHIFT_LEFT = 228.0f;

// address offsets for relevant properties on plasma::Widget
// plasma::Widget
// ⌞+0x148 node tree plasma::Node
//    ... [via children list on root node, FindByName walks over these]
//        ⌞+0x38 Transformation* plasma::Transformation
//           ⌞+0x48 translation plasma::Attribute
//              ⌞+0x20 frame uint32
//              ⌞+0x4C frames Vector2*
//              ⌞+0x50 frames_end Vector2*
static const uint32_t WIDGET_NODE_TREE = 0x148;
static const uint32_t NODE_TRANSFORMATION = 0x38;
static const uint32_t TRANSFORMATION_TRANSLATION = 0x48;
static const uint32_t ATTRIBUTE_FRAME = 0x20;
static const uint32_t ATTRIBUTE_FRAMES = 0x4C;
static const uint32_t ATTRIBUTE_FRAMES_END = 0x50;

// Every translation attribute in gui.plx has exactly two keyframes: an
// on-screen position and an off-screen position. 
// Maybe it was used to toggle visibility in the past, now it uses a different
// approach to handle visibility. The second keyframe could be a leftover.
// In any case, we expect two keyframes and want to manipulate the first one,
// which is the on-screen position. Receiving a translation attribute with a
// different frame count means that we are not looking at what we expect it to
// be, and we would change things we didn't want to change.
static const uint32_t EXPECTED_FRAMES = 2;

// one keyframe of the translation attribute - plasma::Vector<2,float>
// own type for code beauty
struct Vector2
{
    float x;
    float y;
};

// rebuilding MSVC's std::wstring by hand
// It's not available in MSVC's shape in our toolchain, but we have to pass it
// in the same shape MSVC expects to call the FindByName function.
struct ShortWString
{
    wchar_t  data[8];     // +0x00  NUL terminated characters
    uint32_t size;        // +0x10  length without counting the NUL
    uint32_t capacity;    // +0x14  below 8 means the characters can inline

    template <size_t N>
    constexpr ShortWString(const wchar_t (&text)[N]) : data{}, size(N - 1), capacity(7)
    {
        static_assert(N - 1 <= 7, "too long for MSVC's inline buffer");
        for (size_t i = 0; i < N - 1; i++) data[i] = text[i];
    }
};

struct Star
{
    ShortWString name;
    float original_x;
};

// Stars must be matched via name using FindByName.
// Stars are only moved if their x position is still the value we expect.
// Legendary star4 is the odd one out, it's two pixels wider than the others,
// which moves its anchor position half of it to the left.
static constexpr Star STARS[4] = {
    { L"star1", 288.176f },
    { L"star2", 288.176f },
    { L"star3", 288.176f },
    { L"star4", 287.176f }
};

// Load base address once it's available and calculate virtual addresses from
// here.
static uint32_t base;

// Index of the RebuildMatrix function in the transformation objects vtable.
// Applying the translation alone is not enough, the composed matrix needs to be
// rebuilt.
static const int VTABLE_REBUILD_MATRIX = 1;
typedef void (__attribute__((thiscall)) *RebuildMatrixFn)(void* transformation, int unused);

static void RebuildMatrix(void* transformation)
{
    RebuildMatrixFn* vtable = *(RebuildMatrixFn**)transformation;
    vtable[VTABLE_REBUILD_MATRIX](transformation, 1);
}

// plasma::Node* __thiscall plasma::Node::FindByName(Node*, const wstring&)
typedef void* (__attribute__((thiscall)) *FindByNameFn)(void* node, const void* name);
static const uint32_t RVA_FIND_BY_NAME = 0x00233D70;
static FindByNameFn FindByName;

// relative virtual address (RVA) of the constructor we patch
static const uint32_t RVA_PREVIEW_CTOR = 0x000D4FA7;
static const int      HOOK_LEN         = 10;  // mov dword [edi+0x168], -1

// The exact bytes we expect to replace - a mismatch means a different build.
static const BYTE EXPECTED_BYTES[HOOK_LEN] = {
    0xC7, 0x87, 0x68, 0x01, 0x00, 0x00,  // mov dword [edi + 0x168],
    0xFF, 0xFF, 0xFF, 0xFF               // 0xffffffff
};

// ---- hook body --------------------------------------------------------------
static void MoveStarLeft(void* node, float original_x)
{
    if (!node) return;

    uint8_t* transformation = *(uint8_t**)((uint8_t*)node + NODE_TRANSFORMATION);
    if (!transformation) return;

    uint8_t* translation = transformation + TRANSFORMATION_TRANSLATION;      // pointer to translation attribute
    Vector2* frames      = *(Vector2**)(translation + ATTRIBUTE_FRAMES);     // pointer to frames array start
    Vector2* framesEnd   = *(Vector2**)(translation + ATTRIBUTE_FRAMES_END); // pointer to frames array end (last frame + 1)
    uint32_t frame       = *(uint32_t*)(translation + ATTRIBUTE_FRAME);      // index of the keyframe in effect

    // calculate frame count by measuring distance between frames array start and end pointers
    uint32_t frameCount = (frames && framesEnd > frames) ? (uint32_t)(framesEnd - frames) : 0;
    // reject if frame count is not what we expect or in-effect keyframe is outside our frame count
    if (frameCount != EXPECTED_FRAMES) return;
    if (frame >= frameCount) return;
    Vector2* xy = frames + frame;

    // be nice and compare floats with a tolerance to 0 instead of hard equality
    const float tolerance = 0.01f;
    if (fabsf(xy->x - original_x) >= tolerance) return;

    xy->x -= SHIFT_LEFT;
    RebuildMatrix(transformation);
}

extern "C" void OnPreviewWidgetCreated(void* widget)
{
    if (!widget) return;

    void* nodeTree = *(void**)((uint8_t*)widget + WIDGET_NODE_TREE);
    if (!nodeTree) return;

    for (int i = 0; i < 4; i++) {
        MoveStarLeft(FindByName(nodeTree, &STARS[i].name), STARS[i].original_x);
    }
}

// ---- hook stub --------------------------------------------------------------
extern "C" {
    uint32_t jmp_back = 0;
}

// Naked: no prologue, so the game's registers and stack stay untouched.
// EFLAGS and xmm are not saved on purpose - the replaced instruction is a
// constant store between two more of its kind, so nothing live is in either.
// The overwritten instruction is replayed at the end, before jumping back
// past it; it depends on no register but edi, which pushad/popad preserves.
extern "C" __attribute__((naked)) void HookStub()
{
    asm("pushad");                              // push registers to stack

    asm("push edi");                            // edi = the PreviewWidget
    asm("call _OnPreviewWidgetCreated");
    asm("add esp, 4");                          // cdecl, caller cleans

    asm("popad");                               // pops stack back to registers
    asm("mov dword ptr [edi + 0x168], -1");     // original code
    asm("jmp dword ptr [_jmp_back]");           // -> mov dword [edi+0x16c], eax
}

// ---- patching ---------------------------------------------------------------
static bool WriteHook(BYTE* location, void* target)
{
    DWORD oldProtection;
    if (!VirtualProtect(location, HOOK_LEN, PAGE_EXECUTE_READWRITE, &oldProtection)) {
        return false;
    }
    location[0] = 0xE9; // jmp rel32
    *(uint32_t*)(location + 1) = (uint32_t)target - (uint32_t)location - 5;
    for (int i = 5; i < HOOK_LEN; i++) location[i] = 0x90; // no-op the rest of the store
    VirtualProtect(location, HOOK_LEN, oldProtection, &oldProtection);
    FlushInstructionCache(GetCurrentProcess(), location, HOOK_LEN);
    return true;
}

extern "C" __declspec(dllexport) BOOL APIENTRY DllMain(HINSTANCE hinstDLL, DWORD fdwReason, LPVOID lpvReserved)
{
    if (fdwReason != DLL_PROCESS_ATTACH) {
        return TRUE;
    }

    base       = (uint32_t)GetModuleHandle(NULL);
    FindByName = (FindByNameFn)(base + RVA_FIND_BY_NAME);

    BYTE* site = (BYTE*)(base + RVA_PREVIEW_CTOR);

    // Refuse to patch anything that is not the expected Cube.exe build.
    if (memcmp(site, EXPECTED_BYTES, HOOK_LEN) != 0) {
        return TRUE;
    }

    jmp_back = base + RVA_PREVIEW_CTOR + HOOK_LEN;
    WriteHook(site, (void*)&HookStub);
    return TRUE;
}
