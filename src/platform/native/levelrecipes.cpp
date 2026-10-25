// The community mod's level recipes (Crash-Twinsanity-Improved, mod/levels/*.ops and mod/skip_prompt.ops), made on the behaviour
// scripts as the game reads them from the disc: the scripts' bytes are taken apart as the mod's twinsdump does, its edits made in
// the recipes' order, and the result read by the game in place of the disc's. Nothing of the game's data is kept here: the recipes
// are the mod's own edits (script, state, body and command numbers), and what they copy comes from the disc's scripts.
//
// The format (the PS2's, as GraphData::Read reads it): the graph's bits, its name (a length and the characters), the states' count
// and the start state's index, every state (its bits, its control packet when bit 14: a byte count, a float count, the settings and
// the bytes and floats; another state follows when bit 15), then each state's bodies in turn when its count (bits 0-4) isn't 0 (a
// body: its bits, its jump when bit 10, its condition when bit 9 (its word and three floats), its commands when its count (bits
// 0-7) isn't 0, another body when bit 11). A command is its word (its ID, another follows when bit 24) and its arguments, as many
// bytes as the game's own ReadCommand takes for that ID
#include "fixes.h"

#include "native.h"

#include "game/agentlab.h"
#include "game/memory.h"
#include "game/stream.h"
#include "game/string.h"

#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <map>
#include <set>
#include <string>
#include <string_view>
#include <unordered_map>
#include <vector>

namespace NativeFixes
{
namespace
{
// Which option a recipe goes with: the restored cutscene skips (and their prompt) with the cutscene skip, the others with the
// bug fixes
enum Gate : u32
{
    GateSkips,
    GateFixes,
};

struct RecipeFile
{
    const char* source;
    Gate gate;
    const char* text;
};

// The mod's recipes as it ships them (its commit f5333507, 2026-09-26). mod/levels-wip isn't shipped and isn't here
const RecipeFile g_Recipes[] = {
#include "levelrecipes.inc"
};

// ------------------------------------------------------------------------------------------------------------- the scripts
constexpr u32 StateBodyCountMask = 0x1F;
constexpr u32 StatePacket = 1u << 14;
constexpr u32 StateNext = 1u << 15;
constexpr u32 StateChildSlot = 1u << 12;
constexpr u32 BodyCommandCountMask = 0xFF;
constexpr u32 BodyCondition = 1u << 9;
constexpr u32 BodyJump = 1u << 10;
constexpr u32 BodyNext = 1u << 11;
constexpr u32 CommandIdMask = 0xFFFFFF;
constexpr u32 CommandNext = 1u << 24;
constexpr u32 SkipCondition = 572;

struct Command
{
    // Its word and its arguments, as the file has them
    std::vector<u8> bytes;
};

struct Condition
{
    u32 bits;
    f32 values[3];
};

struct Body
{
    u32 bits;
    s32 jump;
    Condition condition;
    std::vector<Command> commands;
};

struct State
{
    u32 bits;
    // Its control packet's bytes as the file has them (none: empty)
    std::vector<u8> packet;
    std::vector<Body> bodies;
};

struct Script
{
    u32 bits;
    std::vector<u8> name;
    // The states' count as the file has it (the game doesn't read it)
    s32 stateCount;
    s32 start;
    std::vector<State> states;
};

// The bytes of a command's arguments in the file, as ReadCommand takes them (the command made by the builder and asked), kept by ID
bool ArgumentBytes(u32 id, u32* bytes)
{
    static std::unordered_map<u32, u32> known;
    auto found = known.find(id);
    if (found != known.end())
    {
        *bytes = found->second;
        return true;
    }

    // The game's builder (made by the first script read)
    if (g_ObjectBuilder == nullptr)
    {
        return false;
    }

    auto* command = static_cast<ScriptCommand*>(g_ObjectBuilder->Build(id, ObjectBuilder::CommandKind));
    if (command == nullptr)
    {
        return false;
    }

    const void* sizeFunction = command->vtable[ScriptCommand::SizeSlot].function;
    u32 count = 0;
    if (!NativeScriptCommandArguments(sizeFunction, nullptr, nullptr, &count))
    {
        count = CallVirtual<u32>(command, command->vtable, ScriptCommand::SizeSlot) - static_cast<u32>(sizeof(ScriptCommand));
    }

    command->next = nullptr;
    CallVirtual<void>(command, command->vtable, ScriptCommand::DestroySlot, u32{DestroyAndFree});
    known[id] = count;
    *bytes = count;
    return true;
}

struct Reader
{
    const u8* data;
    u32 size;
    u32 at = 0;
    bool failed = false;

    u32 U32()
    {
        if (size - at < 4 || at > size)
        {
            failed = true;
            return 0;
        }

        u32 value;
        std::memcpy(&value, data + at, 4);
        at += 4;
        return value;
    }

    void Bytes(std::vector<u8>* out, u32 count)
    {
        if (at > size || size - at < count)
        {
            failed = true;
            return;
        }

        out->insert(out->end(), data + at, data + at + count);
        at += count;
    }
};

bool Parse(const u8* data, u32 size, Script* script)
{
    Reader reader{data, size};
    script->bits = reader.U32();
    u32 nameLength = reader.U32();
    if (static_cast<s32>(nameLength) > 0)
    {
        reader.Bytes(&script->name, nameLength);
    }

    script->stateCount = static_cast<s32>(reader.U32());
    script->start = static_cast<s32>(reader.U32());
    while (!reader.failed)
    {
        State state;
        state.bits = reader.U32();
        if ((state.bits & StatePacket) != 0)
        {
            u32 counts = reader.U32();
            u32 settings = reader.U32();
            u8 head[8];
            std::memcpy(head, &counts, 4);
            std::memcpy(head + 4, &settings, 4);
            state.packet.assign(head, head + 8);
            reader.Bytes(&state.packet, (counts & 0xFF) + ((counts >> 8) & 0xFF) * 4);
        }

        script->states.push_back(std::move(state));
        if ((script->states.back().bits & StateNext) == 0)
        {
            break;
        }
    }

    for (State& state : script->states)
    {
        if ((state.bits & StateBodyCountMask) == 0)
        {
            continue;
        }

        while (!reader.failed)
        {
            Body body = {};
            body.bits = reader.U32();
            if ((body.bits & BodyJump) != 0)
            {
                body.jump = static_cast<s32>(reader.U32());
            }

            if ((body.bits & BodyCondition) != 0)
            {
                body.condition.bits = reader.U32();
                for (f32& value : body.condition.values)
                {
                    u32 word = reader.U32();
                    std::memcpy(&value, &word, 4);
                }
            }

            if ((body.bits & BodyCommandCountMask) != 0)
            {
                while (!reader.failed)
                {
                    Command command;
                    u32 word = reader.U32();
                    u32 arguments = 0;
                    if (!ArgumentBytes(word & CommandIdMask, &arguments))
                    {
                        return false;
                    }

                    command.bytes.resize(4);
                    std::memcpy(command.bytes.data(), &word, 4);
                    reader.Bytes(&command.bytes, arguments);
                    body.commands.push_back(std::move(command));
                    if ((word & CommandNext) == 0)
                    {
                        break;
                    }
                }
            }

            state.bodies.push_back(std::move(body));
            if ((state.bodies.back().bits & BodyNext) == 0)
            {
                break;
            }
        }
    }

    return !reader.failed && reader.at == size;
}

void PutU32(std::vector<u8>* out, u32 value)
{
    u8 bytes[4];
    std::memcpy(bytes, &value, 4);
    out->insert(out->end(), bytes, bytes + 4);
}

u32 Linked(u32 bits, u32 bit, bool more)
{
    return more ? bits | bit : bits & ~bit;
}

std::vector<u8> Serialise(const Script& script)
{
    std::vector<u8> out;
    PutU32(&out, script.bits);
    PutU32(&out, static_cast<u32>(script.name.size()));
    out.insert(out.end(), script.name.begin(), script.name.end());
    PutU32(&out, static_cast<u32>(script.stateCount));
    PutU32(&out, static_cast<u32>(script.start));
    for (size_t i = 0; i < script.states.size(); i++)
    {
        const State& state = script.states[i];
        PutU32(&out, Linked(Linked(state.bits, StatePacket, !state.packet.empty()), StateNext, i + 1 < script.states.size()));
        out.insert(out.end(), state.packet.begin(), state.packet.end());
    }

    for (const State& state : script.states)
    {
        if ((state.bits & StateBodyCountMask) == 0)
        {
            continue;
        }

        for (size_t b = 0; b < state.bodies.size(); b++)
        {
            const Body& body = state.bodies[b];
            PutU32(&out, Linked(body.bits, BodyNext, b + 1 < state.bodies.size()));
            if ((body.bits & BodyJump) != 0)
            {
                PutU32(&out, static_cast<u32>(body.jump));
            }

            if ((body.bits & BodyCondition) != 0)
            {
                PutU32(&out, body.condition.bits);
                for (f32 value : body.condition.values)
                {
                    u32 word;
                    std::memcpy(&word, &value, 4);
                    PutU32(&out, word);
                }
            }

            if ((body.bits & BodyCommandCountMask) == 0)
            {
                continue;
            }

            for (size_t c = 0; c < body.commands.size(); c++)
            {
                const Command& command = body.commands[c];
                u32 word;
                std::memcpy(&word, command.bytes.data(), 4);
                PutU32(&out, Linked(word, CommandNext, c + 1 < body.commands.size()));
                out.insert(out.end(), command.bytes.begin() + 4, command.bytes.end());
            }
        }
    }

    return out;
}

// ------------------------------------------------------------------------------------------------------------- the edits
// twinsdump's condition: its ID (the "VTableIndex"), the parameter at bit 17, the interval, the threshold and its inverse
Condition MakeCondition(u32 id, u32 parameter, f32 interval, f32 threshold)
{
    return {id | (parameter << 17), {interval, threshold, 1.0f / threshold}};
}

// A state's bits after a body is added (twinsdump: the count and the tools' copy of it, and bit 11, which the game's own
// skip-enabled states have)
void CountBodies(State* state)
{
    u32 count = static_cast<u32>(state->bodies.size());
    u32 low = ((state->bits & 0xFFFF) & ~0x3FFu) | (count << 5) | count | 0x800;
    state->bits = (state->bits & 0xFFFF0000) | (low & 0xFFFF);
}

void AppendCommands(Body* body, const std::vector<Command>& commands)
{
    body->commands.insert(body->commands.end(), commands.begin(), commands.end());
    body->bits = (body->bits & ~BodyCommandCountMask) | static_cast<u32>(body->commands.size());
}

bool MakeCommand(u32 id, std::initializer_list<u32> arguments, Command* command)
{
    u32 bytes = 0;
    if (!ArgumentBytes(id, &bytes) || bytes < arguments.size() * 4)
    {
        return false;
    }

    command->bytes.assign(4 + bytes, 0);
    std::memcpy(command->bytes.data(), &id, 4);
    u32 at = 4;
    for (u32 argument : arguments)
    {
        std::memcpy(command->bytes.data() + at, &argument, 4);
        at += 4;
    }

    return true;
}

u32 FloatWord(f32 value)
{
    u32 word;
    std::memcpy(&word, &value, 4);
    return word;
}

// The states reachable from the start by the bodies' jumps
std::set<s32> Reachable(const Script& script)
{
    std::set<s32> seen;
    std::vector<s32> todo{script.start};
    while (!todo.empty())
    {
        s32 index = todo.back();
        todo.pop_back();
        if (index < 0 || index >= static_cast<s32>(script.states.size()) || !seen.insert(index).second)
        {
            continue;
        }

        for (const Body& body : script.states[index].bodies)
        {
            if ((body.bits & BodyJump) != 0)
            {
                todo.push_back(body.jump);
            }
        }
    }

    return seen;
}

// twinsdump's SkipStates: the reachable states that play a cutscene (run a script, not a slot) and have a Triangle rule
std::set<s32> SkipStates(const Script& script)
{
    std::set<s32> result;
    for (s32 index : Reachable(script))
    {
        const State& state = script.states[index];
        s16 child = static_cast<s16>(state.bits >> 16);
        if (child < 0 || (state.bits & StateChildSlot) != 0)
        {
            continue;
        }

        for (const Body& body : state.bodies)
        {
            if ((body.bits & BodyCondition) != 0 && (body.condition.bits & 0xFFFF) == SkipCondition)
            {
                result.insert(index);
                break;
            }
        }
    }

    return result;
}

// twinsdump's AddSkipPrompt: the hint shown (BottomTextDisplay of the AgentLab texts' line 24, in the letterbox) on every jump into
// the states and cleared (BottomTextClear) on every jump out of them; a start state among them gets a new state before it
bool AddSkipPrompt(Script* script, const std::set<s32>& states)
{
    constexpr u32 PromptLine = 24;
    constexpr u32 BottomTextDisplay = 603;
    constexpr u32 BottomTextClear = 608;
    Command display;
    Command clear;
    if (!MakeCommand(BottomTextDisplay, {PromptLine, FloatWord(0.5f), FloatWord(0.92f), FloatWord(1.0f), FloatWord(1.0f), FloatWord(1.0f), 0},
                     &display) ||
        !MakeCommand(BottomTextClear, {}, &clear))
    {
        return false;
    }

    for (size_t i = 0; i < script->states.size(); i++)
    {
        for (Body& body : script->states[i].bodies)
        {
            if ((body.bits & BodyJump) == 0)
            {
                continue;
            }

            bool from = states.count(static_cast<s32>(i)) != 0;
            bool to = states.count(body.jump) != 0;
            if (!from && to)
            {
                AppendCommands(&body, {display});
            }
            else if (from && !to)
            {
                AppendCommands(&body, {clear});
            }
        }
    }

    if (states.count(script->start) != 0)
    {
        State entry;
        entry.bits = 0xFFFF0000 | 0x0421;
        Body body = {};
        body.bits = BodyCondition | BodyJump;
        body.jump = script->start;
        body.condition = MakeCondition(0, 0, 0.0f, 0.5f);
        AppendCommands(&body, {display});
        entry.bodies.push_back(std::move(body));
        script->start = static_cast<s32>(script->states.size());
        script->states.push_back(std::move(entry));
        script->stateCount++;
    }

    return true;
}

struct Op
{
    const RecipeFile* file;
    std::string line;
    std::vector<std::string> words;
};

// The ops of each script (the recipes' order), the explicit prompts' states, and the scripts the ops copy from
struct Recipes
{
    std::map<u32, std::vector<Op>> ops;
    std::map<u32, std::vector<std::set<s32>>> prompts;
    std::set<u32> sources;
    std::set<u32> targets;
};

std::vector<std::string> Words(std::string_view line)
{
    std::vector<std::string> words;
    size_t at = 0;
    while (at < line.size())
    {
        while (at < line.size() && (line[at] == ' ' || line[at] == '\t'))
        {
            at++;
        }

        size_t end = at;
        while (end < line.size() && line[end] != ' ' && line[end] != '\t')
        {
            end++;
        }

        if (end > at)
        {
            words.emplace_back(line.substr(at, end - at));
        }

        at = end;
    }

    return words;
}

const Recipes& AllRecipes()
{
    static Recipes recipes;
    static bool read = false;
    if (read)
    {
        return recipes;
    }

    read = true;
    // A script in two files (the rooftop director) has the same ops in both: its first file's are taken
    std::map<u32, std::string> fileOf;
    for (const RecipeFile& file : g_Recipes)
    {
        std::string_view text(file.text);
        std::string level;
        size_t at = 0;
        while (at < text.size())
        {
            size_t end = text.find('\n', at);
            if (end == std::string_view::npos)
            {
                end = text.size();
            }

            std::string_view line = text.substr(at, end - at);
            at = end + 1;
            size_t hash = line.find('#');
            std::vector<std::string> words = Words(line.substr(0, hash));
            if (words.empty())
            {
                continue;
            }

            if (words[0] == "file")
            {
                level = words.size() > 1 ? words[1] : "";
                continue;
            }

            if (words[0] == "skipprompt")
            {
                // "auto" is made for every script read (AutoPrompt); the others name their script and states
                if (words.size() >= 3 && words[1] != "auto")
                {
                    std::set<s32> states;
                    std::string_view list = words[2];
                    while (!list.empty())
                    {
                        size_t comma = list.find(',');
                        states.insert(std::atoi(std::string(list.substr(0, comma)).c_str()));
                        list = comma == std::string_view::npos ? std::string_view() : list.substr(comma + 1);
                    }

                    recipes.prompts[static_cast<u32>(std::atoi(words[1].c_str()))].push_back(states);
                }

                continue;
            }

            if (words.size() < 2)
            {
                continue;
            }

            u32 script = static_cast<u32>(std::atoi(words[1].c_str()));
            auto known = fileOf.find(script);
            if (known != fileOf.end() && known->second != level)
            {
                continue;
            }

            fileOf[script] = level;
            recipes.ops[script].push_back({&file, std::string(line), words});
            recipes.targets.insert(script);
            if (words[0] == "appendcmds" && words.size() >= 7)
            {
                recipes.sources.insert(static_cast<u32>(std::atoi(words[4].c_str())));
            }
        }
    }

    return recipes;
}

bool GateOn(Gate gate)
{
    return gate == GateFixes ? RetailFixes() : SkipCutscenes();
}

f32 Number(const std::string& word)
{
    return std::strtof(word.c_str(), nullptr);
}

s32 Integer(const std::string& word)
{
    return static_cast<s32>(std::strtol(word.c_str(), nullptr, 10));
}

State* StateAt(Script* script, s32 index)
{
    return index >= 0 && index < static_cast<s32>(script->states.size()) ? &script->states[index] : nullptr;
}

Body* BodyAt(State* state, s32 index)
{
    return state != nullptr && index >= 0 && index < static_cast<s32>(state->bodies.size()) ? &state->bodies[index] : nullptr;
}

// One op (twinsdump's edit): false when the script isn't what it was written for (the edit is then given up)
bool ApplyOp(const Op& op, Script* script, std::map<u32, Script>& others)
{
    const std::vector<std::string>& w = op.words;
    auto has = [&](size_t count) { return w.size() >= count; };
    State* state = has(3) ? StateAt(script, Integer(w[2])) : nullptr;
    if (state == nullptr)
    {
        return false;
    }

    if (w[0] == "addbody" && has(6))
    {
        // addbody SCRIPT STATE COND PARAM TARGET [INTERVAL [THRESHOLD]]
        Body body = {};
        body.bits = BodyCondition | BodyJump;
        body.jump = Integer(w[5]);
        body.condition = MakeCondition(static_cast<u32>(Integer(w[3])), static_cast<u32>(Integer(w[4])), has(7) ? Number(w[6]) : 0.0f,
                                       has(8) ? Number(w[7]) : 0.5f);
        state->bodies.push_back(std::move(body));
        CountBodies(state);
        return true;
    }

    if (w[0] == "copybody" && has(9))
    {
        // copybody SCRIPT TOSTATE FROMSTATE BODYIDX TARGET COND PARAM INTERVAL [THRESHOLD]: appended after the state's bodies
        // (twinsdump needs one there)
        Body* from = BodyAt(StateAt(script, Integer(w[3])), Integer(w[4]));
        if (from == nullptr || state->bodies.empty())
        {
            return false;
        }

        f32 threshold = has(10) ? Number(w[9]) : (from->bits & BodyCondition) != 0 ? from->condition.values[1] : 0.5f;
        Body body = {};
        body.bits = (from->bits & BodyCommandCountMask) | BodyCondition | BodyJump;
        body.jump = Integer(w[5]);
        body.condition = MakeCondition(static_cast<u32>(Integer(w[6])), static_cast<u32>(Integer(w[7])), Number(w[8]), threshold);
        body.commands = from->commands;
        state->bodies.push_back(std::move(body));
        CountBodies(state);
        return true;
    }

    if (w[0] == "clearbodies")
    {
        state->bodies.clear();
        state->bits &= ~0xFFFu;
        return true;
    }

    if (w[0] == "appendcmds" && has(7))
    {
        // appendcmds SCRIPT STATE BODYIDX FROMSCRIPT FROMSTATE FROMBODYIDX
        Body* body = BodyAt(state, Integer(w[3]));
        u32 fromId = static_cast<u32>(Integer(w[4]));
        Script* fromScript = fromId == static_cast<u32>(Integer(w[1])) ? script : nullptr;
        if (fromScript == nullptr)
        {
            auto found = others.find(fromId);
            fromScript = found != others.end() ? &found->second : nullptr;
        }

        Body* from = fromScript != nullptr ? BodyAt(StateAt(fromScript, Integer(w[5])), Integer(w[6])) : nullptr;
        if (body == nullptr || from == nullptr || from->commands.empty())
        {
            return false;
        }

        std::vector<Command> copy = from->commands;
        AppendCommands(body, copy);
        return true;
    }

    if (w[0] == "delcmd" && has(5))
    {
        Body* body = BodyAt(state, Integer(w[3]));
        s32 index = Integer(w[4]);
        if (body == nullptr || index < 0 || index >= static_cast<s32>(body->commands.size()))
        {
            return false;
        }

        body->commands.erase(body->commands.begin() + index);
        body->bits = (body->bits & ~BodyCommandCountMask) | static_cast<u32>(body->commands.size());
        return true;
    }

    if (w[0] == "movebody" && has(5))
    {
        s32 from = Integer(w[3]);
        s32 to = Integer(w[4]);
        s32 count = static_cast<s32>(state->bodies.size());
        if (from < 0 || from >= count || to < 0 || to > count - 1)
        {
            return false;
        }

        Body moved = std::move(state->bodies[from]);
        state->bodies.erase(state->bodies.begin() + from);
        state->bodies.insert(state->bodies.begin() + to, std::move(moved));
        return true;
    }

    if (w[0] == "settarget" && has(5))
    {
        Body* body = BodyAt(state, Integer(w[3]));
        if (body == nullptr)
        {
            return false;
        }

        body->jump = Integer(w[4]);
        body->bits |= BodyJump;
        return true;
    }

    if (w[0] == "setarg" && has(7))
    {
        // setarg SCRIPT STATE BODYIDX CMDIDX ARGIDX VALUE
        Body* body = BodyAt(state, Integer(w[3]));
        s32 index = Integer(w[4]);
        s32 argument = Integer(w[5]);
        if (body == nullptr || index < 0 || index >= static_cast<s32>(body->commands.size()) || argument < 0)
        {
            return false;
        }

        std::vector<u8>& bytes = body->commands[index].bytes;
        size_t at = 4 + static_cast<size_t>(argument) * 4;
        if (at + 4 > bytes.size())
        {
            return false;
        }

        u32 value = static_cast<u32>(std::strtoul(w[6].c_str(), nullptr, 10));
        std::memcpy(bytes.data() + at, &value, 4);
        return true;
    }

    return false;
}

// ------------------------------------------------------------------------------------------------------------- at load
struct Kept
{
    // The disc's bytes of the script (its last read), and the graph read from them while it's there and waits for a script its
    // ops copy from (then edited once that one's read: never once it's edited, as it may be running)
    std::vector<u8> bytes;
    ScriptGraph* graph = nullptr;
    bool waiting = false;
};

std::map<u32, Kept> g_Kept;

bool Tracing()
{
    static const bool tracing = std::getenv("TWIN_FIXES_TRACE") != nullptr;
    return tracing;
}

// $TWIN_SCRIPT_DUMP=6147,6149 (or "all"; testing): those scripts logged as the game reads them, and again as the recipes edit
// them: every state (its index, the start marked, its child slot and packet), each body's condition (its ID, parameter and
// interval, ! inverted), its jump and its commands (each one's ID and argument words)
bool DumpWanted(u32 id)
{
    static const char* wanted = std::getenv("TWIN_SCRIPT_DUMP");
    if (wanted == nullptr)
    {
        return false;
    }

    if (std::strcmp(wanted, "all") == 0)
    {
        return true;
    }

    for (const char* at = wanted; *at != '\0';)
    {
        char* end = nullptr;
        unsigned long number = std::strtoul(at, &end, 10);
        if (end == at)
        {
            break;
        }

        if (number == id)
        {
            return true;
        }

        at = *end == ',' ? end + 1 : end;
    }

    return false;
}

void Dump(const char* what, u32 id, const u8* data, u32 size)
{
    Script script;
    if (!Parse(data, size, &script))
    {
        Native::Log("script dump: %u (%s) can't be taken apart", id, what);
        return;
    }

    Native::Log("script dump: %u %.*s (%s), %zu states, the start %d", id, static_cast<int>(script.name.size()),
                reinterpret_cast<const char*>(script.name.data()), what, script.states.size(), script.start);
    for (size_t index = 0; index < script.states.size(); index++)
    {
        const State& state = script.states[index];
        std::string line;
        char part[96];
        std::snprintf(part, sizeof(part), "  state %zu%s bits %08x%s", index, static_cast<s32>(index) == script.start ? " (start)" : "",
                      state.bits, state.packet.empty() ? "" : " packet");
        line = part;
        Native::Log("script dump: %s", line.c_str());
        for (size_t number = 0; number < state.bodies.size(); number++)
        {
            const Body& body = state.bodies[number];
            line.clear();
            if ((body.bits & BodyCondition) != 0)
            {
                u32 condition = body.condition.bits;
                std::snprintf(part, sizeof(part), "if %s%u(%u) every %g", (condition & 0x10000) != 0 ? "!" : "", condition & 0xFFFF,
                              condition >> 17, static_cast<double>(body.condition.values[0]));
                line += part;
            }
            else
            {
                line += "always";
            }

            if ((body.bits & BodyJump) != 0)
            {
                std::snprintf(part, sizeof(part), " -> %d", body.jump);
                line += part;
            }

            Native::Log("script dump:    body %zu bits %08x: %s", number, body.bits, line.c_str());
            for (const Command& command : body.commands)
            {
                u32 word;
                std::memcpy(&word, command.bytes.data(), 4);
                line.clear();
                for (size_t at = 4; at + 4 <= command.bytes.size(); at += 4)
                {
                    u32 argument;
                    std::memcpy(&argument, command.bytes.data() + at, 4);
                    std::snprintf(part, sizeof(part), " %08x", argument);
                    line += part;
                }

                Native::Log("script dump:      command %u:%s", word & CommandIdMask, line.c_str());
            }
        }
    }
}

// Whether a graph as the game read it might want the prompt: a state that runs a script and has a Triangle rule (SkipStates then
// looks at what's reachable)
bool MightPrompt(const GraphData* graph)
{
    for (const GraphState* state = graph->states; state != nullptr; state = state->next)
    {
        if (static_cast<s16>(state->bits.child) < 0 || state->bits.childIsSlot != 0)
        {
            continue;
        }

        for (const StateBody* body = state->bodies; body != nullptr; body = body->next)
        {
            if (body->condition != nullptr && body->condition->bits.id == SkipCondition)
            {
                return true;
            }
        }
    }

    return false;
}

enum class Outcome
{
    Unchanged,
    Edited,
    Waiting,
    Failed,
};

// A script's edits (its ops whose option is on, then the prompt) made on its bytes: the edited bytes
Outcome Edit(u32 id, const u8* data, u32 size, std::vector<u8>* edited, bool prompts, bool fixes, bool skips, std::string* why)
{
    const Recipes& recipes = AllRecipes();
    Script script;
    if (!Parse(data, size, &script))
    {
        *why = "its bytes don't read as a script";
        return Outcome::Failed;
    }

    bool changed = false;
    auto ops = recipes.ops.find(id);
    if (ops != recipes.ops.end())
    {
        std::map<u32, Script> others;
        for (const Op& op : ops->second)
        {
            if (!(op.file->gate == GateFixes ? fixes : skips))
            {
                continue;
            }

            if (op.words[0] == "appendcmds" && op.words.size() >= 7)
            {
                u32 from = static_cast<u32>(Integer(op.words[4]));
                if (from != id && others.count(from) == 0)
                {
                    auto kept = g_Kept.find(from);
                    if (kept == g_Kept.end() || kept->second.bytes.empty())
                    {
                        *why = "waiting for script " + std::to_string(from);
                        return Outcome::Waiting;
                    }

                    if (!Parse(kept->second.bytes.data(), static_cast<u32>(kept->second.bytes.size()), &others[from]))
                    {
                        *why = "script " + std::to_string(from) + " doesn't read";
                        return Outcome::Failed;
                    }
                }
            }

            if (!ApplyOp(op, &script, others))
            {
                *why = "op \"" + op.line + "\" doesn't fit (" + op.file->source + ")";
                return Outcome::Failed;
            }

            changed = true;
        }
    }

    if (prompts && skips)
    {
        std::set<s32> states = SkipStates(script);
        if (!states.empty())
        {
            if (!AddSkipPrompt(&script, states))
            {
                *why = "no prompt commands";
                return Outcome::Failed;
            }

            changed = true;
        }

        auto listed = recipes.prompts.find(id);
        if (listed != recipes.prompts.end())
        {
            for (const std::set<s32>& explicitStates : listed->second)
            {
                if (!AddSkipPrompt(&script, explicitStates))
                {
                    *why = "no prompt commands";
                    return Outcome::Failed;
                }
            }

            changed = true;
        }
    }

    if (!changed)
    {
        return Outcome::Unchanged;
    }

    *edited = Serialise(script);
    return Outcome::Edited;
}

// The graph's data read again from edited bytes (the same GraphData: what holds it keeps it)
void ReadAgain(ScriptGraph* graph, std::vector<u8>& bytes)
{
    GraphData* data = graph->data;
    if (data->states != nullptr)
    {
        data->states->Destroy(DestroyAndFree);
    }

    StringDestroy(&data->name);
    data->name.string = nullptr;
    data->name.length = 0;
    data->name.capacity = 0;
    data->states = nullptr;
    data->start = nullptr;
    MemoryStream stream;
    MemoryStream::Construct(&stream, bytes.data(), static_cast<u32>(bytes.size()), 0, MemoryStream::FileAlignment);
    data->Read(&stream);
    stream.Destroy(DestroyOnly);
}

Outcome EditGraph(ScriptGraph* graph, u32 id, const u8* data, u32 size)
{
    bool fixes = RetailFixes();
    bool skips = SkipCutscenes();
    std::vector<u8> edited;
    std::string why;
    const Recipes& recipes = AllRecipes();
    bool prompts = skips && (recipes.targets.count(id) != 0 || recipes.prompts.count(id) != 0 || MightPrompt(graph->data));
    Outcome outcome = Edit(id, data, size, &edited, prompts, fixes, skips, &why);
    switch (outcome)
    {
    case Outcome::Edited:
        if (DumpWanted(id))
        {
            Dump("as the recipes edit it", id, edited.data(), static_cast<u32>(edited.size()));
        }

        ReadAgain(graph, edited);
        if (Tracing())
        {
            Native::Log("level recipes: script %u (%.*s) edited, %u bytes -> %zu", id, graph->data->name.length,
                        graph->data->name.string, size, edited.size());
        }
        break;
    case Outcome::Failed:
        Native::Log("level recipes: script %u left as the disc has it: %s", id, why.c_str());
        break;
    case Outcome::Waiting:
        if (Tracing())
        {
            Native::Log("level recipes: script %u %s", id, why.c_str());
        }
        break;
    case Outcome::Unchanged:
        break;
    }

    return outcome;
}
}

void ScriptRead(ScriptGraph* graph, u32 id, const u8* data, u32 size)
{
    if (graph == nullptr || graph->data == nullptr)
    {
        return;
    }

    if (DumpWanted(id))
    {
        Dump("as the disc has it", id, data, size);
    }

    const Recipes& recipes = AllRecipes();
    bool target = recipes.targets.count(id) != 0;
    bool source = recipes.sources.count(id) != 0;
    Kept* kept = nullptr;
    if (target || source)
    {
        kept = &g_Kept[id];
        kept->bytes.assign(data, data + size);
        kept->graph = graph;
        kept->waiting = false;
    }

    if (!RetailFixes() && !SkipCutscenes())
    {
        return;
    }

    if (EditGraph(graph, id, data, size) == Outcome::Waiting && kept != nullptr)
    {
        kept->waiting = true;
    }

    if (!source)
    {
        return;
    }

    // The targets read before this script that wait for it: edited now
    for (auto& [waitingId, other] : g_Kept)
    {
        if (waitingId != id && other.waiting && other.graph != nullptr)
        {
            other.waiting = EditGraph(other.graph, waitingId, other.bytes.data(), static_cast<u32>(other.bytes.size())) == Outcome::Waiting;
        }
    }
}

void ScriptDestroyed(ScriptGraph* graph)
{
    for (auto& [id, kept] : g_Kept)
    {
        if (kept.graph == graph)
        {
            kept.graph = nullptr;
            kept.waiting = false;
        }
    }
}
}

// ------------------------------------------------------------------------------------------------------------- the self-test
namespace NativeFixes
{
namespace
{
struct ArchiveFile
{
    std::string name;
    u32 offset;
    u32 size;
};

bool ReadDiscFile(const char* path, std::vector<u8>* out)
{
    Native::Disc::Entry entry;
    if (!Native::Disc::Find(path, &entry))
    {
        return false;
    }

    out->resize(entry.size);
    return Native::Disc::Read(entry.offset, out->data(), entry.size) == entry.size;
}

// CRASH.BH: a word, then each file's name (its length first), offset in CRASH.BD and size
std::vector<ArchiveFile> ArchiveFiles(const std::vector<u8>& header)
{
    std::vector<ArchiveFile> files;
    u32 at = 4;
    while (at + 4 <= header.size())
    {
        u32 length;
        std::memcpy(&length, header.data() + at, 4);
        if (at + 4 + length + 8 > header.size())
        {
            break;
        }

        ArchiveFile file;
        file.name.assign(reinterpret_cast<const char*>(header.data() + at + 4), length);
        std::memcpy(&file.offset, header.data() + at + 4 + length, 4);
        std::memcpy(&file.size, header.data() + at + 8 + length, 4);
        files.push_back(file);
        at += 12 + length;
    }

    return files;
}

// A section's items: its header (a word, the count, the content's size) and each item's offset (from the section), size and ID
struct Item
{
    u32 offset;
    u32 size;
    u32 id;
};

std::vector<Item> SectionItems(const std::vector<u8>& file, u32 base)
{
    std::vector<Item> items;
    if (base + 12 > file.size())
    {
        return items;
    }

    u32 count;
    std::memcpy(&count, file.data() + base + 4, 4);
    for (u32 i = 0; i < count && base + 12 + 12 * i + 12 <= file.size(); i++)
    {
        Item item;
        std::memcpy(&item, file.data() + base + 12 + 12 * i, 12);
        item.offset += base;
        if (item.offset + item.size <= file.size())
        {
            items.push_back(item);
        }
    }

    return items;
}

bool Locate(const std::vector<u8>& file, std::initializer_list<u32> path, u32* base, u32* size)
{
    *base = 0;
    *size = static_cast<u32>(file.size());
    for (u32 id : path)
    {
        bool found = false;
        for (const Item& item : SectionItems(file, *base))
        {
            if (item.id == id)
            {
                *base = item.offset;
                *size = item.size;
                found = true;
                break;
            }
        }

        if (!found)
        {
            return false;
        }
    }

    return true;
}

std::string Lower(std::string text)
{
    for (char& c : text)
    {
        c = (c >= 'A' && c <= 'Z') ? static_cast<char>(c - 'A' + 'a') : c;
    }

    return text;
}
}

int SelfTestRecipes()
{
    std::vector<u8> header;
    if (!ReadDiscFile("CRASH6/CRASH.BH", &header))
    {
        Native::Log("recipes self-test: FAILED (no CRASH.BH)");
        return 1;
    }

    Native::Disc::Entry archive;
    Native::Disc::Find("CRASH6/CRASH.BD", &archive);
    // The levels the mod's skip_prompt.ops runs "skipprompt auto" on (the native build makes the prompt for every script)
    std::set<std::string> autoLevels;
    std::map<std::string, std::set<u32>> levelTargets;
    for (const RecipeFile& file : g_Recipes)
    {
        std::string_view text(file.text);
        std::string level;
        size_t at = 0;
        while (at < text.size())
        {
            size_t end = text.find('\n', at);
            end = end == std::string_view::npos ? text.size() : end;
            std::vector<std::string> words = Words(text.substr(at, end - at).substr(0, text.substr(at, end - at).find('#')));
            at = end + 1;
            if (words.size() >= 2 && words[0] == "file")
            {
                level = Lower(words[1]);
            }
            else if (words.size() >= 2 && words[0] == "skipprompt" && words[1] == "auto")
            {
                autoLevels.insert(level);
            }
            else if (words.size() >= 2 && words[0] != "skipprompt")
            {
                levelTargets[level].insert(static_cast<u32>(std::atoi(words[1].c_str())));
            }
        }
    }

    u32 levels = 0;
    u32 scripts = 0;
    u32 roundTripFailures = 0;
    u32 edited = 0;
    u32 editFailures = 0;
    u32 promptsListed = 0;
    u32 promptsOther = 0;
    u32 crateFiles = 0;
    u32 crateShaders = 0;
    std::map<std::string, u32> sourceCopies;
    std::set<u32> targetsFound;
    for (const ArchiveFile& entry : ArchiveFiles(header))
    {
        std::string name = Lower(entry.name);
        if (name.size() < 4 || name.compare(name.size() - 4, 4, ".rm2") != 0)
        {
            continue;
        }

        std::vector<u8> file(entry.size);
        if (Native::Disc::Read(archive.offset + entry.offset, file.data(), entry.size) != entry.size)
        {
            continue;
        }

        levels++;
        // The crate materials' shaders the fix makes shadow receivers
        u32 base;
        u32 size;
        u32 changedHere = 0;
        if (Locate(file, {11, 1}, &base, &size))
        {
            for (const Item& item : SectionItems(file, base))
            {
                std::vector<u8> copy(file.begin() + item.offset, file.begin() + item.offset + item.size);
                MaterialRead(copy.data(), item.size);
                for (u32 i = 0; i < item.size; i++)
                {
                    changedHere += copy[i] != file[item.offset + i];
                }
            }
        }

        crateShaders += changedHere;
        crateFiles += changedHere != 0;
        if (!Locate(file, {10, 1}, &base, &size))
        {
            continue;
        }

        std::vector<Item> items = SectionItems(file, base);
        g_Kept.clear();
        for (const Item& item : items)
        {
            if (AllRecipes().sources.count(item.id) != 0)
            {
                g_Kept[item.id].bytes.assign(file.begin() + item.offset, file.begin() + item.offset + item.size);
                Script source;
                if (item.id == 1511 && Parse(file.data() + item.offset, item.size, &source))
                {
                    // What the Tiki Mon's recipe copies (state 7's first body): the same in every level that has the script
                    Body* body = BodyAt(StateAt(&source, 7), 0);
                    std::string key;
                    for (const Command& command : body != nullptr ? body->commands : std::vector<Command>{})
                    {
                        key.append(reinterpret_cast<const char*>(command.bytes.data()), command.bytes.size());
                    }

                    sourceCopies[key]++;
                }
            }
        }

        for (const Item& item : items)
        {
            if ((item.id & 1) == 0)
            {
                continue;
            }

            scripts++;
            const u8* data = file.data() + item.offset;
            Script script;
            if (!Parse(data, item.size, &script) || Serialise(script) != std::vector<u8>(data, data + item.size))
            {
                roundTripFailures++;
                Native::Log("recipes self-test: %s script %u doesn't read back as it was", entry.name.c_str(), item.id);
                continue;
            }

            bool target = levelTargets[name].count(item.id) != 0;
            bool listed = AllRecipes().prompts.count(item.id) != 0;
            bool prompted = !SkipStates(script).empty();
            std::vector<u8> result;
            std::string why;
            Outcome outcome = Outcome::Unchanged;
            if (target || listed || prompted || AllRecipes().targets.count(item.id) != 0)
            {
                outcome = Edit(item.id, data, item.size, &result, true, true, true, &why);
            }

            if (target)
            {
                targetsFound.insert(item.id);
            }

            if (outcome == Outcome::Failed || outcome == Outcome::Waiting || (target && outcome != Outcome::Edited))
            {
                editFailures++;
                Native::Log("recipes self-test: %s script %u: %s", entry.name.c_str(), item.id, why.c_str());
                continue;
            }

            if (outcome != Outcome::Edited)
            {
                continue;
            }

            Script after;
            if (!Parse(result.data(), static_cast<u32>(result.size()), &after))
            {
                editFailures++;
                Native::Log("recipes self-test: %s script %u: the edited script doesn't read", entry.name.c_str(), item.id);
                continue;
            }

            edited++;
            bool autoHere = autoLevels.count(name) != 0;
            if (!target && !listed)
            {
                (autoHere ? promptsListed : promptsOther)++;
                if (!autoHere)
                {
                    Native::Log("recipes self-test: prompt added outside the mod's prompted levels: %s script %u (%.*s)",
                                entry.name.c_str(), item.id, static_cast<int>(script.name.size()),
                                reinterpret_cast<const char*>(script.name.data()));
                }
            }

            Native::Log("recipes self-test: %s script %u (%.*s): %u -> %zu bytes%s%s", entry.name.c_str(), item.id,
                        static_cast<int>(script.name.size()), reinterpret_cast<const char*>(script.name.data()), item.size,
                        result.size(), target ? ", recipe" : "", prompted ? ", prompt" : "");
        }
    }

    g_Kept.clear();
    u32 missing = 0;
    for (u32 id : AllRecipes().targets)
    {
        if (targetsFound.count(id) == 0)
        {
            missing++;
            Native::Log("recipes self-test: script %u of the recipes isn't in its level", id);
        }
    }

    bool ok = levels > 0 && roundTripFailures == 0 && editFailures == 0 && missing == 0 && sourceCopies.size() == 1;
    Native::Log("recipes self-test: %s (%u levels, %u scripts read back; %u edited, %u failed, %u recipe scripts missing; prompts by "
                "\"auto\": %u in the mod's levels, %u elsewhere; the Tiki Mon's copied commands: %zu kind(s); crate shaders made "
                "shadow receivers: %u in %u levels)",
                ok ? "passed" : "FAILED", levels, scripts, edited, editFailures, missing, promptsListed, promptsOther,
                sourceCopies.size(), crateShaders, crateFiles);
    return ok ? 0 : 1;
}

namespace
{
// The skip survey's commands and conditions by ID
constexpr u32 TriangleCondition = 575;
constexpr u32 NextCondition = 0;
constexpr u32 SendUserMessage = 54;
constexpr u32 SendUserMessageToo = 65;
constexpr u32 RunSlotBehaviourOnLinked = 171;
constexpr u32 StopTargetBehaviour = 172;
constexpr u32 SetTargetOwnerToSelf = 183;
constexpr u32 RestoreOwnObject = 184;
constexpr u32 SetAgent = 515;
constexpr u32 SetPlayerInput = 521;
constexpr u32 SetFocusToGameActor = 576;
constexpr u32 CutsceneStart = 589;
constexpr u32 CutsceneEnd = 590;
constexpr u32 CutsceneCameraMove = 591;
constexpr u32 ToggleCutsceneCamera = 594;
constexpr u32 PositionWarp = 3;
constexpr u32 PositionWarpToo = 105;
// RunSlotBehaviourOnLinked's slot word: the node it runs on takes its object from the agent's
constexpr u32 GivesSourceBit = 1u << 18;
// SetPlayerInput's settings word: all the inputs given (low half) and on (high half)
constexpr u32 AllInputsBit = 1u << 6;

// A command's argument word (its ID's word: index -1)
u32 Word(const Command& command, s32 index)
{
    u32 word = 0;
    size_t at = static_cast<size_t>(4 + index * 4);
    if (at + 4 <= command.bytes.size())
    {
        std::memcpy(&word, command.bytes.data() + at, 4);
    }

    return word;
}

// What a run of commands does that a skip has to do too: the player's inputs given back or taken, an actor made to run the
// director's object's scripts and given its own back, the user messages sent, the cutscene started and ended, the warps and the
// playing character changed
struct Effects
{
    bool inputsBack = false;
    bool inputsTaken = false;
    bool sourceGiven = false;
    bool sourceBack = false;
    std::set<u32> messages;
    bool cutsceneStart = false;
    bool cutsceneEnd = false;
    bool cutsceneCamera = false;
    bool warps = false;
    bool agent = false;

    void Add(const std::vector<Command>& commands)
    {
        for (const Command& command : commands)
        {
            switch (Word(command, -1) & CommandIdMask)
            {
            case SetPlayerInput:
                if ((Word(command, 0) & AllInputsBit) != 0)
                {
                    bool on = ((Word(command, 0) >> 16) & AllInputsBit) != 0;
                    inputsBack = inputsBack || on;
                    inputsTaken = inputsTaken || !on;
                }
                break;
            case RunSlotBehaviourOnLinked:
                sourceGiven = sourceGiven || (Word(command, 1) & GivesSourceBit) != 0;
                break;
            case SetTargetOwnerToSelf:
                sourceGiven = true;
                break;
            case StopTargetBehaviour:
            case RestoreOwnObject:
                sourceBack = true;
                break;
            case SendUserMessage:
            case SendUserMessageToo:
                messages.insert(Word(command, 0) & 0xFFFF);
                break;
            case CutsceneStart:
                cutsceneStart = true;
                break;
            case CutsceneEnd:
                cutsceneEnd = true;
                break;
            case CutsceneCameraMove:
            case ToggleCutsceneCamera:
                cutsceneCamera = true;
                break;
            case PositionWarp:
            case PositionWarpToo:
                warps = true;
                break;
            case SetAgent:
            case SetFocusToGameActor:
                agent = true;
                break;
            default:
                break;
            }
        }
    }
};

// The script a state runs (-1 none, or a behaviour slot's)
s32 ChildOf(const State& state)
{
    s16 child = static_cast<s16>(state.bits >> 16);
    return child < 0 || (state.bits & StateChildSlot) != 0 ? -1 : child;
}

// A skip's rule: condition 572 with a player (the restored skips) or the game's own Triangle (575), not inverted
bool IsSkip(const Body& body)
{
    if ((body.bits & BodyCondition) == 0 || (body.condition.bits & 0x10000) != 0)
    {
        return false;
    }

    u32 id = body.condition.bits & 0xFFFF;
    return (id == SkipCondition && (body.condition.bits >> 17) != 0) || id == TriangleCondition;
}

// Every command of a script's reachable states and of the scripts they run (to a depth)
void SceneEffects(const std::map<u32, Script>& scripts, u32 id, Effects* effects, std::set<u32>* seen, s32 depth)
{
    auto found = scripts.find(id);
    if (found == scripts.end() || depth > 3 || !seen->insert(id).second)
    {
        return;
    }

    for (s32 index : Reachable(found->second))
    {
        const State& state = found->second.states[index];
        for (const Body& body : state.bodies)
        {
            effects->Add(body.commands);
        }

        if (ChildOf(state) >= 0)
        {
            SceneEffects(scripts, static_cast<u32>(ChildOf(state)), effects, seen, depth + 1);
        }
    }
}

// The skip's path: its body's commands, then the states its jump goes through while each one's first body is a Next (an
// instant step), and the scripts those run
void SkipEffects(const std::map<u32, Script>& scripts, const Script& script, const Body& skip, Effects* effects)
{
    effects->Add(skip.commands);
    std::set<s32> visited;
    s32 index = (skip.bits & BodyJump) != 0 ? skip.jump : -1;
    while (index >= 0 && index < static_cast<s32>(script.states.size()) && visited.insert(index).second)
    {
        const State& state = script.states[index];
        if (ChildOf(state) >= 0)
        {
            std::set<u32> seen;
            SceneEffects(scripts, static_cast<u32>(ChildOf(state)), effects, &seen, 1);
        }

        const Body* first = state.bodies.empty() ? nullptr : &state.bodies[0];
        if (first == nullptr || (first->bits & BodyCondition) == 0 || (first->condition.bits & 0xFFFF) != NextCondition)
        {
            break;
        }

        effects->Add(first->commands);
        index = (first->bits & BodyJump) != 0 ? first->jump : -1;
    }
}

std::string Name(const Script& script)
{
    return std::string(reinterpret_cast<const char*>(script.name.data()), script.name.size());
}
}

// $TWIN_FIXES_TEST=skips: every scene the cutscene skip can skip, with the option on (the disc's scripts as the recipes and the
// prompt edit them): a state that runs a scene (a script) and has a skip's rule (condition 572, or the game's own Triangle, 575),
// what the scene does that the skip's path doesn't (the player's inputs given back, an actor's own object given back, user
// messages sent, the cutscene ended, warps, the playing character), and the scenes no rule skips (a state that runs a script that
// starts a cutscene, without one)
int SurveySkips()
{
    std::vector<u8> header;
    if (!ReadDiscFile("CRASH6/CRASH.BH", &header))
    {
        Native::Log("skip survey: FAILED (no CRASH.BH)");
        return 1;
    }

    Native::Disc::Entry archive;
    Native::Disc::Find("CRASH6/CRASH.BD", &archive);
    u32 sites = 0;
    u32 flagged = 0;
    u32 unskippable = 0;
    for (const ArchiveFile& entry : ArchiveFiles(header))
    {
        std::string name = Lower(entry.name);
        if (name.size() < 4 || name.compare(name.size() - 4, 4, ".rm2") != 0)
        {
            continue;
        }

        std::vector<u8> file(entry.size);
        u32 base;
        u32 size;
        if (Native::Disc::Read(archive.offset + entry.offset, file.data(), entry.size) != entry.size ||
            !Locate(file, {10, 1}, &base, &size))
        {
            continue;
        }

        std::vector<Item> items = SectionItems(file, base);
        g_Kept.clear();
        for (const Item& item : items)
        {
            if (AllRecipes().sources.count(item.id) != 0)
            {
                g_Kept[item.id].bytes.assign(file.begin() + item.offset, file.begin() + item.offset + item.size);
            }
        }

        // The level's scripts as the game has them with the skip option on (the bug fixes off)
        std::map<u32, Script> scripts;
        std::set<u32> recipeEdited;
        for (const Item& item : items)
        {
            if ((item.id & 1) == 0)
            {
                continue;
            }

            const u8* data = file.data() + item.offset;
            std::vector<u8> result;
            std::string why;
            Script script;
            if (Edit(item.id, data, item.size, &result, true, false, true, &why) == Outcome::Edited &&
                Parse(result.data(), static_cast<u32>(result.size()), &script))
            {
                if (AllRecipes().targets.count(item.id) != 0)
                {
                    recipeEdited.insert(item.id);
                }
            }
            else if (!Parse(data, item.size, &script))
            {
                continue;
            }

            scripts[item.id] = std::move(script);
        }

        // The scripts a skip's rule is in (a reachable state's body), and the states that run each script with one there
        std::set<u32> withSkip;
        std::map<u32, bool> runBySkipping;
        for (const auto& [id, script] : scripts)
        {
            for (s32 index : Reachable(script))
            {
                for (const Body& body : script.states[index].bodies)
                {
                    if (IsSkip(body))
                    {
                        withSkip.insert(id);
                    }
                }
            }
        }

        for (const auto& [id, script] : scripts)
        {
            for (s32 index : Reachable(script))
            {
                const State& state = script.states[index];
                if (ChildOf(state) < 0)
                {
                    continue;
                }

                bool skips = false;
                for (const Body& body : state.bodies)
                {
                    skips = skips || IsSkip(body);
                }

                bool& run = runBySkipping[static_cast<u32>(ChildOf(state))];
                run = run || skips;
            }
        }

        for (const auto& [id, script] : scripts)
        {
            for (s32 index : Reachable(script))
            {
                const State& state = script.states[index];
                s32 child = ChildOf(state);
                if (child < 0)
                {
                    continue;
                }

                Effects scene;
                std::set<u32> seen;
                SceneEffects(scripts, static_cast<u32>(child), &scene, &seen, 1);
                const Body* skip = nullptr;
                for (const Body& body : state.bodies)
                {
                    skip = skip == nullptr && IsSkip(body) ? &body : skip;
                }

                auto childScript = scripts.find(static_cast<u32>(child));
                std::string childName = childScript != scripts.end() ? Name(childScript->second) : "?";
                if (skip == nullptr)
                {
                    // A scene (a cutscene camera or a cutscene started; not the generic begin and end every director runs) no
                    // rule skips: not its runner's state, not its own states, not a state that runs its runner
                    bool generic = childName.find("GENERIC_CUTSCENE") != std::string::npos;
                    bool ownRule = withSkip.count(static_cast<u32>(child)) != 0;
                    auto parent = runBySkipping.find(id);
                    bool parentRule = parent != runBySkipping.end() && parent->second;
                    if ((scene.cutsceneStart || scene.cutsceneCamera) && !generic && !ownRule && !parentRule)
                    {
                        unskippable++;
                        Native::Log("skip survey: unskippable: %s %u %s state %d runs %d %s", entry.name.c_str(), id,
                                    Name(script).c_str(), index, child, childName.c_str());
                    }
                    else if ((scene.cutsceneStart || scene.cutsceneCamera) && !generic)
                    {
                        Native::Log("skip survey: skippable by %s: %s %u %s state %d runs %d %s", ownRule ? "its own rule" : "its runner's runner",
                                    entry.name.c_str(), id, Name(script).c_str(), index, child, childName.c_str());
                    }

                    continue;
                }

                sites++;
                Effects path;
                SkipEffects(scripts, script, *skip, &path);
                std::string missing;
                if (scene.inputsBack && !path.inputsBack)
                {
                    missing += " inputs";
                }

                if (scene.sourceGiven && scene.sourceBack && !path.sourceBack)
                {
                    missing += " own-object";
                }

                for (u32 message : scene.messages)
                {
                    if (path.messages.count(message) == 0)
                    {
                        missing += " message:" + std::to_string(message);
                    }
                }

                if (scene.cutsceneEnd && !path.cutsceneEnd)
                {
                    missing += " cutscene-end";
                }

                if (scene.warps && !path.warps)
                {
                    missing += " warps";
                }

                if (scene.agent && !path.agent)
                {
                    missing += " character";
                }

                u32 condition = skip->condition.bits & 0xFFFF;
                flagged += !missing.empty();
                Native::Log("skip survey: %s %u %s state %d -> %d (%s%s), runs %d %s:%s", entry.name.c_str(), id, Name(script).c_str(),
                            index, (skip->bits & BodyJump) != 0 ? skip->jump : -1, condition == SkipCondition ? "572" : "triangle",
                            recipeEdited.count(id) != 0 ? ", a recipe's" : "", child, childName.c_str(),
                            missing.empty() ? " nothing missed" : missing.c_str());
            }
        }
    }

    g_Kept.clear();
    Native::Log("skip survey: %u skips, %u missing something the scene does; %u scenes with no skip", sites, flagged, unskippable);
    return sites > 0 ? 0 : 1;
}
}
