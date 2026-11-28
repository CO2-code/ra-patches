#include <windows.h>
#include <stdio.h>
#include "macros/patch.h"
#include "ra95.h"
#include "patch.h"
#include "houseclass.h"

// Spectator / replay live stats, drawn inside SidebarClass::Draw_It (hook in
// src/spectator.asm). Called with all registers saved, once per sidebar redraw.
//
// Layout: one block per player, a legend row on top. The text colour of a block
// is the player's own colour so the eye can follow a player down the panel.
//
//   Units                 Veh/Inf/Bld/Air      <- legend (grey)
//   SoulTaker                       $ 4120     <- name (player colour) + money
//   Own                      6/25/10/0
//   Kill                       0/3/0/0
//   [1]SuperWar                     $ 98
//   Own                       7/27/10/0
//   Kill                       0/8/0/0
//   ...
//
// Money is Credits + Tiberium (ore in silos/refineries), the same sum the engine
// uses in HouseClass::Available_Money (EA source: HOUSE.CPP). Players that are
// defeated are drawn in grey so a finished player is obvious at a glance.
//
// With more than 4 players the 3-row blocks no longer fit between the radar and
// the (empty) strips, so the panel falls back to 2-row blocks: name + money, then
// owned and killed side by side. Row pitch shrinks to keep everything on screen.

#define STATS_TOP_Y         177     // first row, just below the radar/buttons
#define STATS_MAX_HEIGHT    120     // up to where the sidebar strips start

#define PRINT_LEFT          0x4046  // TPF_USE_GRAD_PAL | TPF_FULLSHADOW | TPF_6PT_GRAD
#define PRINT_RIGHT         0x4216  // TPF_USE_GRAD_PAL | TPF_RIGHT | TPF_NOSHADOW | TPF_6PT_GRAD
#define PRINT_LABEL         0x0016  // TPF_NOSHADOW | TPF_6PT_GRAD (colour from remap only)
#define PRINT_LABEL_RIGHT   0x0216  // same, right aligned

#define BACK_COLOR          0x0C    // black

#define DEAD_FLAG           0x01    // HC_FLAGS2 bit 0 (HC_Defeated)

typedef struct
{
    int vehicles;
    int infantry;
    int buildings;
    int planes;
} StatCounts;

static int GetTotals(int *typePointer);
static int *GetPlayerRemapControlType(int *housePointer);
static void ReadCounts(int *house, int ownedOffset, int infantryOffset, int buildingOffset, int planeOffset, StatCounts *out);
static int CountActivePlayers(void);

void DrawLiveStats()
{
    int *greyRemap = (void *)0x00666E8A; // the grey colour scheme (ColorRemaps[5])
    int xLeft = SideBarPanelLeftPosX - 16;
    int xRight = SideBarPanelLeftPosX + 143;

    int players = CountActivePlayers();
    if (players == 0) return;

    int compact = players > 4;
    int rowHeight = compact ? 7 : 8;
    int blockGap = compact ? 0 : 2;
    int rowsPerPlayer = compact ? 2 : 3;

    // Shrink the row pitch if even the compact layout would run into the strips.
    int totalHeight = rowHeight + players * (rowsPerPlayer * rowHeight + blockGap);
    if (totalHeight > STATS_MAX_HEIGHT)
    {
        rowHeight = (STATS_MAX_HEIGHT - players * blockGap) / (1 + players * rowsPerPlayer);
        if (rowHeight < 6) rowHeight = 6;
    }

    int y = STATS_TOP_Y;

    // Legend
    if (compact)
    {
        Fancy_Text_Print("Owned", xLeft, y, greyRemap, BACK_COLOR, PRINT_LABEL);
        Fancy_Text_Print("Killed", xRight, y, greyRemap, BACK_COLOR, PRINT_LABEL_RIGHT);
    }
    else
    {
        Fancy_Text_Print("Units", xLeft, y, greyRemap, BACK_COLOR, PRINT_LABEL);
        Fancy_Text_Print("Veh/Inf/Bld/Air", xRight, y, greyRemap, BACK_COLOR, PRINT_LABEL_RIGHT);
    }
    y += rowHeight;

    for (int i = 0; i < HumanPlayers; i++)
    {
        int *house = HouseClass__As_Pointer(12 + i); // 12 = Multi1 aka the first multiplayer house
        int *isSpectator = (void *)house + HC_IS_SPECTATOR;
        if (*isSpectator) continue;

        // Defeated players keep their row but lose their colour.
        uint8_t flags = *((uint8_t *)house + HC_FLAGS2);
        int *remap = (flags & DEAD_FLAG) ? greyRemap : GetPlayerRemapControlType(house);

        char *playerName = (void *)house + HC_PLAYER_NAME;
        int money = *(int *)((void *)house + HC_Credits) + *(int *)((void *)house + HC_Tiberium);

        StatCounts owned, killed;
        ReadCounts(house, HC_UNITS_BOUGHT, HC_INFANTRY_BOUGHT, HC_BUILDINGS_BOUGHT, HC_PLANES_BOUGHT, &owned);
        ReadCounts(house, HC_UNITS_KILLED, HC_INFANTRY_KILLED, HC_BUILDINGS_KILLED, HC_PLANES_KILLED, &killed);

        // Name (left) and money (right)
        Fancy_Text_Print(playerName, xLeft, y, remap, BACK_COLOR, PRINT_LEFT);
        Fancy_Text_Print("$%d", xRight, y, remap, BACK_COLOR, PRINT_RIGHT, money);
        y += rowHeight;

        if (compact)
        {
            Fancy_Text_Print("%d/%d/%d/%d", xLeft, y, remap, BACK_COLOR, PRINT_LEFT,
                             owned.vehicles, owned.infantry, owned.buildings, owned.planes);
            Fancy_Text_Print("%d/%d/%d/%d", xRight, y, remap, BACK_COLOR, PRINT_RIGHT,
                             killed.vehicles, killed.infantry, killed.buildings, killed.planes);
            y += rowHeight;
        }
        else
        {
            Fancy_Text_Print("Own", xLeft, y, greyRemap, BACK_COLOR, PRINT_LABEL);
            Fancy_Text_Print("%d/%d/%d/%d", xRight, y, remap, BACK_COLOR, PRINT_RIGHT,
                             owned.vehicles, owned.infantry, owned.buildings, owned.planes);
            y += rowHeight;

            Fancy_Text_Print("Kill", xLeft, y, greyRemap, BACK_COLOR, PRINT_LABEL);
            Fancy_Text_Print("%d/%d/%d/%d", xRight, y, remap, BACK_COLOR, PRINT_RIGHT,
                             killed.vehicles, killed.infantry, killed.buildings, killed.planes);
            y += rowHeight;
        }

        y += blockGap;
    }
}

static int CountActivePlayers(void)
{
    int count = 0;
    for (int i = 0; i < HumanPlayers; i++)
    {
        int *house = HouseClass__As_Pointer(12 + i);
        int *isSpectator = (void *)house + HC_IS_SPECTATOR;
        if (!*isSpectator) count++;
    }
    return count;
}

static void ReadCounts(int *house, int vehicleOffset, int infantryOffset, int buildingOffset, int planeOffset, StatCounts *out)
{
    out->vehicles = GetTotals((void *)house + vehicleOffset);
    out->infantry = GetTotals((void *)house + infantryOffset);
    out->buildings = GetTotals((void *)house + buildingOffset);
    out->planes = GetTotals((void *)house + planeOffset);
}

static int GetTotals(int *typePointer)
{
    int *arrayLength = (void *)*typePointer + 4;
    int *typeArray = UnitTrackerClass__Get_All_Totals((void *)*typePointer);
    int result = 0;
    for (int i = 0; i < *arrayLength; i++) result += typeArray[i];
    return result;
}

static int *GetPlayerRemapControlType(int *housePointer)
{
    uint8_t *color = (void *)housePointer + HC_PLAYER_COLOR;
    int *result = (void *)0x00666908 + (*color * 282);
    return result;
}
