#pragma once
// Made by native/tools/cutscene_skip_table.py (don't edit): the scripted cutscenes of the disc as TwinsanityEvolved's census found
// them (its cutscene skip judge's fixture, native/third_party/twinsanityevolved/cutscene_skip_pal.json: Copyright (c) 2026 the
// TwinsanityEvolved contributors, MIT, used with menaman123's permission, menaman123/TwinsanityEvolved#19), each by its site and
// the graphs only it runs, with this port's status for the waits' skip (cutsceneskip.h)
#include "common.h"

struct CutsceneSite
{
    enum Status : u8
    {
        // Not checked here yet: plays as watched
        Unchecked = 0,
        // Checked: watched and skipped leave the game alike
        Skippable = 1,
        // A level's transition or the title's flyby (TwinsanityEvolved's refusals): never skipped
        Refused = 2,
    };

    static constexpr u32 MostGraphs = 24;

    const char* name;
    const char* label;
    const char* chunk;
    u32 site;
    Status status;
    // Skipped by the waits even where a skip rule is polled (THE LIST 22)
    bool waitsFirst;
    u32 graphCount;
    u32 graphs[MostGraphs];
};

inline constexpr CutsceneSite g_CutsceneSites[] = {
    {"01_training", "the training on N. Sanity Island's high path", "levels\\earth\\hub\\highpath", 3483, CutsceneSite::Unchecked, false, 10, {3483, 3491, 3497, 3795, 3799, 3927, 3963, 4769, 4771, 4817}},
    {"02_miniboss_start", "the Mecha-Bandicoot fight's start", "levels\\earth\\hub\\bossarea", 3503, CutsceneSite::Unchecked, false, 12, {3503, 3507, 3519, 3525, 3527, 3529, 3531, 3533, 3591, 3593, 3595, 4807}},
    {"03_bear", "the bear on Totem Hokum's bear ride", "levels\\earth\\docamok\\docamok3", 3613, CutsceneSite::Skippable, false, 3, {3613, 3827, 6397}},
    {"04_cavern", "the cavern director's cutscenes (Cavern Catastrophe)", "levels\\earth\\cavern\\cavbridg", 3629, CutsceneSite::Skippable, false, 8, {3629, 3631, 3633, 3657, 3743, 4033, 6167, 6169}},
    {"05_earth_hub", "the emu farmer on N. Sanity Island", "levels\\earth\\hub\\hubd", 3953, CutsceneSite::Skippable, true, 5, {3953, 3955, 3957, 3965, 3989}},
    {"06_tiki_mon", "the Tikimon (N. Sanity Island)", "levels\\earth\\hub\\hubd", 4015, CutsceneSite::Skippable, false, 15, {4015, 4017, 4019, 4021, 4023, 4025, 4027, 4095, 4097, 4099, 4101, 4103, 4105, 4107, 4131}},
    {"07_totem", "the totem on Totem Hokum's bear ride", "levels\\earth\\docamok\\docamok1", 4191, CutsceneSite::Skippable, true, 8, {4191, 4193, 4241, 4255, 4267, 4369, 4777, 4953}},
    {"08_henchmania_bossfight", "the Henchmania boss fight's start", "levels\\ice\\highseas\\gpa12", 4509, CutsceneSite::Skippable, false, 7, {4509, 4553, 4555, 4561, 4617, 4623, 5005}},
    {"09_battleship_switchthrower", "the battleship's switch thrower (High Seas Hi-Jinks)", "levels\\ice\\highseas\\gpa04", 4549, CutsceneSite::Unchecked, false, 2, {4549, 4717}},
    {"10_dingodile_intro", "the Dingodile fight's intro", "levels\\school\\boiler\\boiler_6", 5229, CutsceneSite::Skippable, true, 6, {5227, 5229, 5231, 5233, 5489, 5837}},
    {"11_psychotron", "the Psychetron (Iceberg Lab)", "levels\\ice\\hub\\psycho", 5385, CutsceneSite::Skippable, true, 3, {5383, 5385, 6729}},
    {"12_ukafight", "the Uka Uka fight's start", "levels\\ice\\iceclimb\\ukafight", 5451, CutsceneSite::Skippable, true, 2, {5451, 5459}},
    {"13_icelab", "the Iceberg Lab's exit", "levels\\ice\\hub\\labext", 5495, CutsceneSite::Skippable, true, 3, {5495, 5497, 6697}},
    {"14_sentry", "the sentry's look (Totem Hokum, Classroom Chaos)", "levels\\school\\crash\\crgpa02", 5511, CutsceneSite::Skippable, true, 1, {5511}},
    {"15_slipslide", "Slip Slide Icecapades' start", "levels\\ice\\slipslide\\l05start", 5549, CutsceneSite::Skippable, false, 3, {5549, 5551, 6771}},
    {"16_school_hub", "the Academy of Evil's hub", "levels\\school\\sch_hub\\sch_hub", 5605, CutsceneSite::Skippable, true, 2, {5605, 5607}},
    {"17_dorm_room", "the dorm room (the Academy)", "levels\\school\\cortex\\cogpa08", 5655, CutsceneSite::Skippable, true, 2, {5655, 5657}},
    {"18_ukauka_defeated", "Uka Uka's defeat", "levels\\ice\\iceclimb\\ukafight", 5697, CutsceneSite::Skippable, false, 3, {5697, 5709, 6709}},
    {"19_final_boss", "the Evil Twins fight's start", "levels\\altearth\\core\\throne", 5721, CutsceneSite::Unchecked, false, 24, {5721, 5723, 5725, 5727, 5729, 5731, 5733, 5737, 5739, 5741, 5743, 5745, 5747, 5749, 5753, 5755, 5827, 6027, 6199, 6515, 6517, 6519, 6521, 6895}},
    {"20_henchmania", "Henchmania's start (High Seas Hi-Jinks)", "levels\\ice\\highseas\\gpa11", 5783, CutsceneSite::Unchecked, false, 3, {5783, 5785, 6813}},
    {"21_frontend_flyby", "the title's flyby over the beach", "levels\\earth\\hub\\beach", 5793, CutsceneSite::Refused, false, 2, {5793, 5807}},
    {"22_totem_tied", "the tied totem (Totem Hokum)", "levels\\earth\\totem\\l03stock", 5901, CutsceneSite::Skippable, true, 2, {5901, 5903}},
    {"23_totem_falling", "the falling totem (Totem Hokum)", "levels\\earth\\totem\\l03stock", 5913, CutsceneSite::Skippable, false, 2, {5913, 5915}},
    {"24_rockslide", "Rockslide Rumble's start", "levels\\altearth\\rockslid\\l10start", 5975, CutsceneSite::Skippable, true, 2, {5975, 5977}},
    {"25_walrus", "the walrus (High Seas Hi-Jinks)", "levels\\ice\\highseas\\gpa10", 6021, CutsceneSite::Skippable, true, 3, {6021, 6023, 6803}},
    {"26_lab_psychetron_room", "the Psychetron room after Madame Amberly", "levels\\ice\\hub\\psycho", 6077, CutsceneSite::Unchecked, false, 2, {6077, 6079}},
    {"27_slipslide_shed", "the shed (Slip Slide Icecapades)", "levels\\ice\\slipslide\\l05chasa", 6093, CutsceneSite::Unchecked, false, 3, {6093, 6095, 6097}},
    {"28_classroom_chaos", "Classroom Chaos' start", "levels\\school\\crash\\crgpa08", 6107, CutsceneSite::Skippable, true, 2, {6107, 6109}},
    {"29_angry_skunk", "the angry skunk (the high path)", "levels\\earth\\hub\\highpath", 6147, CutsceneSite::Skippable, true, 2, {6147, 6151}},
    {"30_bell_tower", "the bell tower (Madame Amberly)", "levels\\school\\madame\\amberly", 6185, CutsceneSite::Skippable, true, 2, {6185, 6187}},
    {"31_beehive", "the beehive on Totem Hokum's bear ride", "levels\\earth\\docamok\\docamok2", 6223, CutsceneSite::Unchecked, false, 2, {6221, 6223}},
    {"32_play_fight", "Madame Amberly's play fight", "levels\\school\\madame\\amberly", 6233, CutsceneSite::Unchecked, false, 7, {2421, 2423, 2461, 2587, 2619, 2661, 6233}},
    {"33_icelab_interior", "inside the Iceberg Lab", "levels\\ice\\hub\\labint", 6237, CutsceneSite::Skippable, false, 3, {6237, 6239, 6749}},
    {"34_dingodile_hut", "Dingodile's hut (High Seas Hi-Jinks)", "levels\\ice\\highseas\\gpa01", 6257, CutsceneSite::Skippable, true, 3, {6257, 6259, 6779}},
    {"35_cortex_pipe", "Cortex in the pipe (Boiler Room Doom)", "levels\\school\\boiler\\boiler_1", 6309, CutsceneSite::Unchecked, false, 2, {6309, 6311}},
    {"36_treasure_room", "the treasure room", "levels\\altearth\\core\\treasure", 6327, CutsceneSite::Unchecked, false, 2, {6327, 6329}},
    {"37_psychetron_room_h02j", "the Psychetron room's second visit", "levels\\ice\\hub\\psycho", 6347, CutsceneSite::Unchecked, false, 2, {6347, 6353}},
    {"38_cortex_follow", "the Cortex follow manager (Twinsanity Island)", "levels\\altearth\\hub\\slipjoin", 6361, CutsceneSite::Unchecked, false, 2, {6361, 6633}},
    {"39_beach_aku", "Aku Aku on the beach", "levels\\earth\\hub\\beach", 6365, CutsceneSite::Skippable, true, 2, {6365, 6367}},
    {"40_rooftop", "Rooftop Rampage's start", "levels\\school\\rooftop\\roof01", 6375, CutsceneSite::Skippable, true, 3, {6375, 6377, 6395}},
    {"41_evilcrash_cuddle", "Evil Crash's cuddle (the lab)", "levels\\altearth\\lab\\psycho", 6419, CutsceneSite::Skippable, false, 2, {6419, 6421}},
    {"42_core_intro", "the core's entrance", "levels\\altearth\\hub\\coreent", 6435, CutsceneSite::Skippable, true, 2, {6435, 6445}},
    {"43_penguin", "the penguin (Ice Climb)", "levels\\ice\\iceclimb\\bergext", 6467, CutsceneSite::Skippable, true, 3, {6467, 6469, 6705}},
    {"44_buschase", "the bus chase", "levels\\school\\rooftop\\buschase", 6527, CutsceneSite::Skippable, false, 2, {6527, 6529}},
    {"45_evilcrash_house", "Evil Crash's house", "levels\\altearth\\hub\\alta", 6643, CutsceneSite::Skippable, false, 2, {6643, 6645}},
    {"46_party_arena", "the party arena before the Mecha-Bandicoot fight", "levels\\earth\\hub\\bossarea", 6669, CutsceneSite::Skippable, false, 2, {6669, 6671}},
    {"47_blimp_depart", "the blimp leaving the Iceberg Lab", "levels\\ice\\hub\\labext", 6765, CutsceneSite::Refused, false, 2, {6759, 6765}},
    {"48_school_hub_to_ice_hub", "the airship from the Academy to the Iceberg Lab", "levels\\ice\\hub\\labext", 6767, CutsceneSite::Skippable, false, 1, {6767}},
    {"49_crows_nest", "the crow's nest (High Seas Hi-Jinks)", "levels\\ice\\highseas\\gpa09", 6799, CutsceneSite::Skippable, false, 1, {6799}},
    {"50_beach_training", "the training on the beach", "levels\\earth\\hub\\beach", 6821, CutsceneSite::Skippable, true, 2, {6821, 6823}},
    {"51_coreent_to_labint", "from the core's entrance to the lab", "levels\\altearth\\hub\\coreent", 6871, CutsceneSite::Refused, false, 1, {6871}},
};
