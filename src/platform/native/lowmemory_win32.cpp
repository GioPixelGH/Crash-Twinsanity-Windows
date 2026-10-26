// The PS2's low memory for the retail code's null pointers, on Windows (x86-64): what lowmemory.cpp does with a signal handler
// on arm64, done with a vectored exception handler. Windows never maps the first 64 KB, so a load or store faulting below
// LowMemoryLimit is carried out on g_NativeLowMemory instead (native/NATIVE.md).
//
// The faulting instruction isn't emulated: a copy of it is run. Every x86 instruction reaching memory through a ModRM byte
// computes its address as registers + displacement, so the copy's displacement is made larger by g_NativeLowMemory's address
// (a 32 bit one: the executable is linked below 2 GB, native/CMakeLists.txt). The copy, in a trampoline of the thread's own,
// reads or writes g_NativeLowMemory's bytes at the address the original faulted on, with the instruction's own effect on
// registers and flags, then jumps back past the original (its length decoded: prefixes, opcode, ModRM, SIB, displacement and
// immediate). No single step: a debugger sees nothing of it. A jump or call through low memory crashes, as on the PS2.
//
// Also the crash report: an exception nothing handles is said with its registers and the stack's return addresses (module and
// offset: llvm-symbolizer or addr2line with the executable gives the source lines), since Windows ends the program without a word
#if defined(_WIN32)
#include "native.h"

#include "game/chunkdata.h"
#include "game/gamecontroller.h"
#include "game/player.h"

#include <algorithm>
#include <atomic>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <fstream>
#include <thread>

#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>

extern "C"
{
    alignas(16) u8 g_NativeLowMemory[NativeLowMemoryBytes] = {};
}

namespace
{
constexpr uiptr LowMemoryLimit = NativeLowMemoryBytes;
// The longest x86 instruction; the trampoline: the copy (its displacement grown to 32 bits) and an absolute jump back
constexpr u32 MaxInstruction = 15;
constexpr u32 TrampolineBytes = 64;

thread_local u8* t_Trampoline = nullptr;
LONG g_Emulated = 0;

// The bytes at an address that can be read (the instruction's, up to the end of what's committed)
u32 ReadableBytes(const u8* at, u32 wanted)
{
    MEMORY_BASIC_INFORMATION info;
    if (VirtualQuery(at, &info, sizeof(info)) == 0 || info.State != MEM_COMMIT ||
        (info.Protect & (PAGE_NOACCESS | PAGE_GUARD)) != 0)
    {
        return 0;
    }

    uiptr end = reinterpret_cast<uiptr>(info.BaseAddress) + info.RegionSize;
    uiptr left = end - reinterpret_cast<uiptr>(at);
    return left < wanted ? static_cast<u32>(left) : wanted;
}

// The size of the immediate after an instruction's ModRM operand (opcode map: 1 the one byte opcodes, 2 0F xx, 3 0F 38 xx,
// 4 0F 3A xx), with or without the operand size prefix; -1 for what isn't known (not an instruction reaching memory this way)
s32 ImmediateSize(u32 map, u8 opcode, u8 reg, bool operand16)
{
    s32 full = operand16 ? 2 : 4;
    if (map == 1)
    {
        switch (opcode)
        {
        case 0x69:
        case 0x81:
        case 0xC7:
            return full;
        case 0x6B:
        case 0x80:
        case 0x83:
        case 0xC0:
        case 0xC1:
        case 0xC6:
            return 1;
        case 0xF6:
            return reg <= 1 ? 1 : 0;
        case 0xF7:
            return reg <= 1 ? full : 0;
        default:
            break;
        }

        // The ALU operations (00-3F less the prefixes and the forms without ModRM), MOVSXD, TEST, XCHG, MOV, LEA, POP, the shifts
        // by 1 and CL, x87, INC/DEC and group 5
        bool alu = opcode < 0x40 && (opcode & 7) < 4;
        if (alu || opcode == 0x63 || (opcode >= 0x84 && opcode <= 0x8F) || (opcode >= 0xD0 && opcode <= 0xD3) ||
            (opcode >= 0xD8 && opcode <= 0xDF) || opcode == 0xFE || opcode == 0xFF)
        {
            return 0;
        }

        return -1;
    }

    if (map == 2)
    {
        switch (opcode)
        {
        case 0x70:
        case 0x71:
        case 0x72:
        case 0x73:
        case 0xA4:
        case 0xAC:
        case 0xBA:
        case 0xC2:
        case 0xC4:
        case 0xC5:
        case 0xC6:
            return 1;
        default:
            break;
        }

        // Those with no ModRM operand: SYSCALL, CLTS, SYSRET, INVD, WBINVD, UD2, WRMSR, RDTSC, RDMSR, RDPMC, SYSENTER, SYSEXIT,
        // EMMS, the jumps, PUSH/POP FS and GS, CPUID, RSM, BSWAP
        if ((opcode >= 0x80 && opcode <= 0x8F) || (opcode >= 0x05 && opcode <= 0x09) || opcode == 0x0B ||
            (opcode >= 0x30 && opcode <= 0x35) || opcode == 0x77 || opcode == 0xA0 || opcode == 0xA1 || opcode == 0xA2 ||
            opcode == 0xA8 || opcode == 0xA9 || opcode == 0xAA || (opcode >= 0xC8 && opcode <= 0xCF))
        {
            return -1;
        }

        return 0;
    }

    return map == 4 ? 1 : 0;
}

// The trampoline for the instruction at code: a copy of it with its memory operand moved by delta, then a jump to the
// instruction after it. False for what can't be done this way (no ModRM memory operand, RIP-relative, VEX/EVEX, a jump or
// call through memory, an opcode this doesn't know)
bool MakeTrampoline(const u8* code, u32 available, u32 delta, u8* out)
{
    u32 at = 0;
    bool address32 = false;
    bool operand16 = false;
    // Legacy prefixes
    while (at < available)
    {
        u8 byte = code[at];
        if (byte == 0x66)
        {
            operand16 = true;
            at++;
        }
        else if (byte == 0xF2 || byte == 0xF3 || byte == 0xF0 || byte == 0x2E || byte == 0x36 || byte == 0x3E ||
                 byte == 0x26 || byte == 0x64 || byte == 0x65)
        {
            at++;
        }
        else if (byte == 0x67)
        {
            address32 = true;
            at++;
        }
        else
        {
            break;
        }
    }

    // REX (W makes the operand 64 bits, over 66: the immediates stay 32 bits)
    if (at < available && (code[at] & 0xF0) == 0x40)
    {
        if ((code[at] & 8) != 0)
        {
            operand16 = false;
        }

        at++;
    }

    if (at >= available)
    {
        return false;
    }

    u8 opcode = code[at];
    if (opcode == 0xC4 || opcode == 0xC5 || opcode == 0x62)
    {
        // VEX and EVEX (AVX): the native build doesn't use them; 0x62 is BOUND outside 64 bit mode
        return false;
    }

    u32 length = 0;
    u32 copied = 0;
    if (opcode >= 0xA0 && opcode <= 0xA3)
    {
        // MOV with an absolute address (moffs): 8 bytes of it, 4 with the address size prefix
        u32 size = address32 ? 4 : 8;
        length = at + 1 + size;
        if (length > available)
        {
            return false;
        }

        std::memcpy(out, code, length);
        u64 offset = 0;
        std::memcpy(&offset, code + at + 1, size);
        offset += delta;
        std::memcpy(out + at + 1, &offset, size);
        copied = length;
    }
    else
    {
        u32 map = 1;
        if (opcode == 0x0F)
        {
            map = 2;
            at++;
            if (at < available && (code[at] == 0x38 || code[at] == 0x3A))
            {
                map = code[at] == 0x38 ? 3 : 4;
                at++;
            }

            if (at >= available)
            {
                return false;
            }

            opcode = code[at];
        }

        at++;
        if (at >= available)
        {
            return false;
        }

        u32 modrmAt = at;
        u8 modrm = code[at++];
        u8 mod = modrm >> 6;
        u8 reg = (modrm >> 3) & 7;
        u8 rm = modrm & 7;
        if (mod == 3)
        {
            return false;
        }

        if (map == 1 && opcode == 0xFF && reg >= 2 && reg <= 5)
        {
            // A call or jump through memory: the PS2 would have gone to whatever its low memory held
            return false;
        }

        s32 immediate = ImmediateSize(map, opcode, reg, operand16);
        if (immediate < 0)
        {
            return false;
        }

        bool sibNoBase = false;
        if (rm == 4)
        {
            if (at >= available)
            {
                return false;
            }

            sibNoBase = mod == 0 && (code[at] & 7) == 5;
            at++;
        }
        else if (mod == 0 && rm == 5)
        {
            // RIP-relative: next to the code, never low
            return false;
        }

        u32 dispAt = at;
        u32 dispSize = mod == 1 ? 1 : mod == 2 || sibNoBase ? 4 : 0;
        length = dispAt + dispSize + static_cast<u32>(immediate);
        if (length > available || length > MaxInstruction)
        {
            return false;
        }

        s64 displacement = 0;
        if (dispSize == 1)
        {
            displacement = static_cast<s8>(code[dispAt]);
        }
        else if (dispSize == 4)
        {
            s32 value;
            std::memcpy(&value, code + dispAt, 4);
            displacement = value;
        }

        s64 moved = displacement + delta;
        if (moved > INT32_MAX || moved < INT32_MIN)
        {
            return false;
        }

        // The copy: what comes before the displacement, the displacement as 32 bits (mod 2, or the SIB's no-base form kept),
        // then the immediate
        std::memcpy(out, code, dispAt);
        if (!sibNoBase)
        {
            out[modrmAt] = static_cast<u8>((2 << 6) | (reg << 3) | rm);
        }

        s32 moved32 = static_cast<s32>(moved);
        std::memcpy(out + dispAt, &moved32, 4);
        std::memcpy(out + dispAt + 4, code + dispAt + dispSize, static_cast<u32>(immediate));
        copied = dispAt + 4 + static_cast<u32>(immediate);
    }

    // jmp [rip + 0], then the address after the original
    static const u8 jump[] = {0xFF, 0x25, 0x00, 0x00, 0x00, 0x00};
    std::memcpy(out + copied, jump, sizeof(jump));
    u64 back = reinterpret_cast<u64>(code) + length;
    std::memcpy(out + copied + sizeof(jump), &back, sizeof(back));
    return true;
}

u8* Trampoline()
{
    if (t_Trampoline == nullptr)
    {
        t_Trampoline =
            static_cast<u8*>(VirtualAlloc(nullptr, TrampolineBytes, MEM_COMMIT | MEM_RESERVE, PAGE_EXECUTE_READWRITE));
    }

    return t_Trampoline;
}

void WriteError(const char* text)
{
    DWORD written;
    WriteFile(GetStdHandle(STD_ERROR_HANDLE), text, static_cast<DWORD>(std::strlen(text)), &written, nullptr);
}

LONG CALLBACK OnException(EXCEPTION_POINTERS* exception)
{
    EXCEPTION_RECORD* record = exception->ExceptionRecord;
    CONTEXT* context = exception->ContextRecord;
    if (record->ExceptionCode != EXCEPTION_ACCESS_VIOLATION || record->NumberParameters < 2)
    {
        return EXCEPTION_CONTINUE_SEARCH;
    }

    // [0] is 0 for a read, 1 for a write, 8 for an execution; [1] the address
    ULONG_PTR kind = record->ExceptionInformation[0];
    uiptr address = record->ExceptionInformation[1];
    u8* trampoline = Trampoline();
    DWORD64 start = reinterpret_cast<DWORD64>(trampoline);
    if (kind == 8 || address >= LowMemoryLimit || context->Rip == address || trampoline == nullptr ||
        (context->Rip >= start && context->Rip < start + TrampolineBytes))
    {
        return EXCEPTION_CONTINUE_SEARCH;
    }

    const u8* code = reinterpret_cast<const u8*>(context->Rip);
    u32 available = ReadableBytes(code, MaxInstruction);
    uiptr delta = reinterpret_cast<uiptr>(g_NativeLowMemory);
    if (delta > INT32_MAX - LowMemoryLimit || !MakeTrampoline(code, available, static_cast<u32>(delta), trampoline))
    {
        return EXCEPTION_CONTINUE_SEARCH;
    }

    FlushInstructionCache(GetCurrentProcess(), trampoline, TrampolineBytes);
    context->Rip = start;
    if (InterlockedIncrement(&g_Emulated) == 1 && Native::GetSettings().logStubs)
    {
        // Once: the PS2's way with its null pointers is being followed (see native/NATIVE.md)
        WriteError("[native] a retail null pointer read/write went to the PS2's low memory\n");
    }

    return EXCEPTION_CONTINUE_EXECUTION;
}

// The executable's functions by address, from its COFF symbol table: read from its file the first time a report needs a name
// (Windows doesn't load the table), into memory of its own (not the heap a crash may have broken). The names are as the linker
// has them (C++'s mangled: llvm-cxxfilt reads them)
struct FunctionSymbol
{
    DWORD64 address;
    const char* name;
};
FunctionSymbol* g_Symbols = nullptr;
u32 g_SymbolCount = 0;
bool g_SymbolsRead = false;

void ReadSymbols()
{
    g_SymbolsRead = true;
    auto* base = reinterpret_cast<const u8*>(GetModuleHandleW(nullptr));
    wchar_t path[MAX_PATH];
    if (base == nullptr || GetModuleFileNameW(nullptr, path, MAX_PATH) == 0)
    {
        return;
    }

    const auto* dos = reinterpret_cast<const IMAGE_DOS_HEADER*>(base);
    const auto* nt = reinterpret_cast<const IMAGE_NT_HEADERS64*>(base + dos->e_lfanew);
    DWORD tableAt = nt->FileHeader.PointerToSymbolTable;
    DWORD count = nt->FileHeader.NumberOfSymbols;
    HANDLE file = CreateFileW(path, GENERIC_READ, FILE_SHARE_READ | FILE_SHARE_WRITE, nullptr, OPEN_EXISTING, 0, nullptr);
    LARGE_INTEGER fileSize = {};
    if (tableAt == 0 || count == 0 || file == INVALID_HANDLE_VALUE || !GetFileSizeEx(file, &fileSize) ||
        static_cast<u64>(fileSize.QuadPart) <= tableAt)
    {
        if (file != INVALID_HANDLE_VALUE)
        {
            CloseHandle(file);
        }
        return;
    }

    // The symbols (18 bytes each), then the string table to the file's end
    auto bytes = static_cast<DWORD>(static_cast<u64>(fileSize.QuadPart) - tableAt);
    auto* data = static_cast<u8*>(VirtualAlloc(nullptr, bytes, MEM_COMMIT | MEM_RESERVE, PAGE_READWRITE));
    // Room for every symbol, and for the short names' copies (8 bytes and a terminator)
    g_Symbols = static_cast<FunctionSymbol*>(VirtualAlloc(nullptr, count * sizeof(FunctionSymbol), MEM_COMMIT | MEM_RESERVE,
                                                          PAGE_READWRITE));
    auto* shortNames = static_cast<char*>(VirtualAlloc(nullptr, count * 9, MEM_COMMIT | MEM_RESERVE, PAGE_READWRITE));
    LARGE_INTEGER at = {};
    at.QuadPart = tableAt;
    DWORD read = 0;
    bool ok = data != nullptr && g_Symbols != nullptr && shortNames != nullptr && SetFilePointerEx(file, at, nullptr, FILE_BEGIN) &&
              ReadFile(file, data, bytes, &read, nullptr) && read == bytes && static_cast<u64>(count) * 18 < bytes;
    CloseHandle(file);
    if (!ok)
    {
        g_Symbols = nullptr;
        return;
    }

    const char* strings = reinterpret_cast<const char*>(data + count * 18);
    const IMAGE_SECTION_HEADER* sections = IMAGE_FIRST_SECTION(nt);
    WORD sectionCount = nt->FileHeader.NumberOfSections;
    for (DWORD i = 0; i < count; i++)
    {
        const u8* symbol = data + i * 18;
        u32 nameWord;
        u32 value;
        s16 section;
        std::memcpy(&nameWord, symbol, 4);
        std::memcpy(&value, symbol + 8, 4);
        std::memcpy(&section, symbol + 12, 2);
        u8 storage = symbol[16];
        u8 auxiliary = symbol[17];
        // A symbol of code (in an executable section), external or static
        if (section > 0 && section <= sectionCount && (sections[section - 1].Characteristics & IMAGE_SCN_MEM_EXECUTE) != 0 &&
            (storage == IMAGE_SYM_CLASS_EXTERNAL || storage == IMAGE_SYM_CLASS_STATIC))
        {
            const char* name;
            if (nameWord == 0)
            {
                u32 offset;
                std::memcpy(&offset, symbol + 4, 4);
                name = strings + offset;
            }
            else
            {
                char* copy = shortNames + g_SymbolCount * 9;
                std::memcpy(copy, symbol, 8);
                copy[8] = '\0';
                name = copy;
            }

            // Section names (".text") aren't functions
            if (name[0] != '.')
            {
                g_Symbols[g_SymbolCount++] = {reinterpret_cast<DWORD64>(base) + sections[section - 1].VirtualAddress + value, name};
            }
        }

        i += auxiliary;
    }

    std::sort(g_Symbols, g_Symbols + g_SymbolCount,
              [](const FunctionSymbol& a, const FunctionSymbol& b) { return a.address < b.address; });
}

// The function of the executable's an address is in (the nearest symbol at or before it, within 1 MB), or none
const FunctionSymbol* FunctionAt(DWORD64 address)
{
    if (!g_SymbolsRead)
    {
        ReadSymbols();
    }

    if (g_Symbols == nullptr || g_SymbolCount == 0)
    {
        return nullptr;
    }

    const FunctionSymbol* begin = g_Symbols;
    const FunctionSymbol* end = begin + g_SymbolCount;
    const FunctionSymbol* after = std::upper_bound(begin, end, address,
                                                   [](DWORD64 value, const FunctionSymbol& symbol) { return value < symbol.address; });
    if (after == begin)
    {
        return nullptr;
    }

    const FunctionSymbol* found = after - 1;
    return address - found->address < 0x100000 ? found : nullptr;
}

// A return address as its module and offset in it, and its function's name in the executable
void DescribeAddress(DWORD64 address, char* out, size_t size)
{
    HMODULE module = nullptr;
    char name[MAX_PATH] = "?";
    if (GetModuleHandleExA(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS | GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,
                           reinterpret_cast<LPCSTR>(address), &module) &&
        module != nullptr)
    {
        char path[MAX_PATH];
        if (GetModuleFileNameA(module, path, MAX_PATH) != 0)
        {
            const char* slash = std::strrchr(path, '\\');
            std::snprintf(name, sizeof(name), "%s", slash != nullptr ? slash + 1 : path);
        }

        int used = std::snprintf(out, size, "%016llx %s+%#llx", static_cast<unsigned long long>(address), name,
                                 static_cast<unsigned long long>(address - reinterpret_cast<DWORD64>(module)));
        const FunctionSymbol* function = module == GetModuleHandleW(nullptr) ? FunctionAt(address) : nullptr;
        if (function != nullptr && used > 0 && static_cast<size_t>(used) < size)
        {
            std::snprintf(out + used, size - static_cast<size_t>(used), " %s+%#llx", function->name,
                          static_cast<unsigned long long>(address - function->address));
        }
        return;
    }

    std::snprintf(out, size, "%016llx ?", static_cast<unsigned long long>(address));
}

// The stack's return addresses from a context, by the executable's unwinding tables
void WriteBacktrace(CONTEXT context, int most)
{
    char line[MAX_PATH + 64];
    char where[MAX_PATH + 48];
    // (A call through a null pointer stops at 0: its caller is the return address on the stack, as a leaf's)
    for (int frame = 0; frame < most && (context.Rip != 0 || frame == 0); frame++)
    {
        DescribeAddress(context.Rip, where, sizeof(where));
        std::snprintf(line, sizeof(line), "  #%-2d %s\n", frame, where);
        WriteError(line);
        DWORD64 imageBase = 0;
        PRUNTIME_FUNCTION function = RtlLookupFunctionEntry(context.Rip, &imageBase, nullptr);
        if (function == nullptr)
        {
            // A leaf function: its return address is at the top of the stack
            context.Rip = *reinterpret_cast<DWORD64*>(context.Rsp);
            context.Rsp += 8;
            continue;
        }

        PVOID handlerData = nullptr;
        DWORD64 establisher = 0;
        RtlVirtualUnwind(UNW_FLAG_NHANDLER, imageBase, context.Rip, function, &context, &handlerData, &establisher, nullptr);
    }
}

// Memory that can be read (the crash report reads the game's structures, which may be what went wrong)
bool Readable(const void* address, size_t size)
{
    MEMORY_BASIC_INFORMATION info;
    if (address == nullptr || VirtualQuery(address, &info, sizeof(info)) == 0 || info.State != MEM_COMMIT ||
        (info.Protect & (PAGE_NOACCESS | PAGE_GUARD)) != 0)
    {
        return false;
    }

    const auto* end = static_cast<const u8*>(info.BaseAddress) + info.RegionSize;
    return static_cast<const u8*>(address) + size <= end;
}

const char* ReadableText(const String& text)
{
    if (!Readable(&text, sizeof(text)) || text.string == nullptr || !Readable(text.string, 1))
    {
        return "?";
    }

    for (size_t i = 0; i < 256; i++)
    {
        if (!Readable(text.string + i, 1))
        {
            return "?";
        }

        if (text.string[i] == '\0')
        {
            return text.string;
        }
    }

    return "?";
}

// Where the game was: the player's chunk and the save's place (after the backtrace: reading them may fail too)
void WriteGameContext()
{
    char line[600];
    const char* playerChunk = "?";
    if (Readable(&g_PlayerInstance, sizeof(g_PlayerInstance)) && g_PlayerInstance != nullptr &&
        Readable(g_PlayerInstance, sizeof(Reference)))
    {
        auto* player = static_cast<ReferencedObject*>(g_PlayerInstance->object);
        if (player != nullptr && Readable(player, sizeof(ReferencedObject)) && player->chunk != nullptr &&
            Readable(player->chunk, sizeof(ChunkData)))
        {
            playerChunk = ReadableText(player->chunk->name);
        }
    }

    const char* startChunk = "?";
    if (G_GameController != nullptr && Readable(G_GameController, sizeof(GameController)))
    {
        startChunk = ReadableText(G_GameController->progress.startChunk);
    }

    std::snprintf(line, sizeof(line), "  the player's chunk %s, the save's %s\n", playerChunk, startChunk);
    WriteError(line);
}

LONG CALLBACK OnUnhandled(EXCEPTION_POINTERS* exception)
{
    EXCEPTION_RECORD* record = exception->ExceptionRecord;
    CONTEXT* context = exception->ContextRecord;
    char message[256];
    char where[MAX_PATH + 48];
    DescribeAddress(context->Rip, where, sizeof(where));
    unsigned long long address = record->NumberParameters >= 2 ? record->ExceptionInformation[1] : 0;
    std::snprintf(message, sizeof(message), "[native] fatal exception %#lx at %#llx (pc %s), backtrace:\n",
                  static_cast<unsigned long>(record->ExceptionCode), address, where);
    WriteError(message);
    const DWORD64 registers[] = {context->Rax, context->Rbx, context->Rcx, context->Rdx, context->Rsi, context->Rdi,
                                 context->Rbp, context->Rsp, context->R8,  context->R9,  context->R10, context->R11,
                                 context->R12, context->R13, context->R14, context->R15};
    static const char* const names[] = {"rax", "rbx", "rcx", "rdx", "rsi", "rdi", "rbp", "rsp",
                                        "r8",  "r9",  "r10", "r11", "r12", "r13", "r14", "r15"};
    for (int i = 0; i < 16; i += 4)
    {
        std::snprintf(message, sizeof(message), "  %-3s %016llx  %-3s %016llx  %-3s %016llx  %-3s %016llx\n", names[i],
                      static_cast<unsigned long long>(registers[i]), names[i + 1],
                      static_cast<unsigned long long>(registers[i + 1]), names[i + 2],
                      static_cast<unsigned long long>(registers[i + 2]), names[i + 3],
                      static_cast<unsigned long long>(registers[i + 3]));
        WriteError(message);
    }

    WriteBacktrace(*context, 48);
    WriteGameContext();
    // The session's summary (its time, its play's frame rate) as its last line
    Native::WriteCrashSessionEnd(WriteError);
    return EXCEPTION_CONTINUE_SEARCH;
}
}

namespace
{
std::atomic<u64> g_GameFrames{0};
HANDLE g_GameThread = nullptr;

// The game's thread's stack and the game's state, said in the log while it's stopped
void ReportHang(u32 seconds)
{
    char line[256];
    u32 state = ~0u;
    u32 nextState = ~0u;
    if (G_GameController != nullptr && Readable(G_GameController, sizeof(GameController)))
    {
        state = G_GameController->State();
        nextState = G_GameController->NextState();
    }

    std::snprintf(line, sizeof(line),
                  "[native] hang: the game's thread hasn't finished a frame in %u s (the game's state %u, next %u); its stack:\n",
                  seconds, state, nextState);
    WriteError(line);
    if (SuspendThread(g_GameThread) == static_cast<DWORD>(-1))
    {
        WriteError("  (the thread couldn't be stopped to read it)\n");
        return;
    }

    CONTEXT context = {};
    context.ContextFlags = CONTEXT_FULL;
    if (GetThreadContext(g_GameThread, &context))
    {
        WriteBacktrace(context, 48);
    }

    ResumeThread(g_GameThread);
    WriteGameContext();
}
}

namespace Native
{
void LogBacktraceHere()
{
    std::fflush(stderr);
    CONTEXT context = {};
    RtlCaptureContext(&context);
    WriteBacktrace(context, 24);
}

void GameFrameDone()
{
    g_GameFrames.fetch_add(1, std::memory_order_relaxed);
}

void StartHangWatch()
{
    if (g_GameThread != nullptr || !DuplicateHandle(GetCurrentProcess(), GetCurrentThread(), GetCurrentProcess(), &g_GameThread,
                                                   THREAD_SUSPEND_RESUME | THREAD_GET_CONTEXT | THREAD_QUERY_INFORMATION, FALSE, 0))
    {
        return;
    }

    std::thread(
        []
        {
            constexpr u32 HangSeconds = 20;
            u64 seen = g_GameFrames.load();
            u32 stopped = 0;
            bool reported = false;
            for (;;)
            {
                Sleep(1000);
                u64 frames = g_GameFrames.load();
                // Counted from the first frame (the start-up's loading has none to count)
                if (frames != seen || frames == 0)
                {
                    if (reported)
                    {
                        char line[96];
                        std::snprintf(line, sizeof(line), "[native] hang: the game's thread went on after %u s\n", stopped);
                        WriteError(line);
                    }

                    seen = frames;
                    stopped = 0;
                    reported = false;
                    continue;
                }

                if (++stopped == HangSeconds && !reported)
                {
                    reported = true;
                    ReportHang(stopped);
                }
            }
        })
        .detach();
}

// Loads the PS2's low memory dump when there's one, and starts emulating the retail code's low accesses
void StartLowMemory()
{
    std::ifstream dump("build/native/ps2_low_memory.bin", std::ios::binary);
    if (dump)
    {
        dump.read(reinterpret_cast<char*>(g_NativeLowMemory), NativeLowMemoryBytes);
        Log("the PS2's low memory from build/native/ps2_low_memory.bin");
    }

    SetUnhandledExceptionFilter(OnUnhandled);
    if (reinterpret_cast<uiptr>(g_NativeLowMemory) > INT32_MAX - LowMemoryLimit)
    {
        Log("the PS2's low memory isn't emulated: the executable wasn't linked below 2 GB, a retail null read will crash");
        return;
    }

    AddVectoredExceptionHandler(1, OnException);
}
}

// A free of memory that isn't the game's pool (memory.cpp): where it came from, once a place
void NativeBadFree(void* memory, const void* pages, const void* pagesEnd)
{
    static int reported = 0;
    if (reported++ < 8)
    {
        char message[128];
        std::snprintf(message, sizeof(message), "[native] a free of %p, outside the small pages %p-%p, left alone:\n", memory,
                      pages, pagesEnd);
        WriteError(message);
        CONTEXT context;
        RtlCaptureContext(&context);
        WriteBacktrace(context, 24);
    }
}

bool NativeDebugHeap()
{
    static const bool debug = std::getenv("TWIN_DEBUG_HEAP") != nullptr;
    return debug;
}

void NativeHeapProblem(const char* what, void* memory)
{
    char message[160];
    std::snprintf(message, sizeof(message), "[native] heap: %s (%p):\n", what, memory);
    WriteError(message);
    CONTEXT context;
    RtlCaptureContext(&context);
    WriteBacktrace(context, 16);
}

#endif
