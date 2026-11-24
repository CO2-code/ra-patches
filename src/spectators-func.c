#include <windows.h>
#include <stdio.h>
#include "macros/patch.h"
#include "ra95.h"
#include "patch.h"
#include "houseclass.h"

int ReplayControlsVisible = 1;

static int GetTotals(int *typePointer);
static int *GetPlayerRemapControlType(int *housePointer);

void DrawLiveStats()
{
    int y = 177;
    int xLeftAligned = SideBarPanelLeftPosX - 16;
    int xRightAligned = SideBarPanelLeftPosX + 143;
    int *remapControlType = (void *)0x00666E8A;
    int textPrintTypeLeftAligned = 0x4046;
    int textPrintTypeRightAligned = 0x4216;
    int backColor = 0x0C; //black
    int rowHeight = 12;

    int row = 0;
    
    Fancy_Text_Print("Vehic/Inf/Build/Air Owned", xLeftAligned, y, remapControlType, backColor, 0x16);
    for (int i = 0; i < HumanPlayers; i++)
    {
        int *house = HouseClass__As_Pointer(12 + i); // 12 = Multi1 aka the first multiplayer house
        int *isSpectator = (void *)house + HC_IS_SPECTATOR;
        if (*isSpectator) continue;
        int *playerRemapControlType = GetPlayerRemapControlType(house);
        row++;
        
        char *playerName = (void *)house + HC_PLAYER_NAME;
        Fancy_Text_Print(playerName, xLeftAligned, (rowHeight * row) + y, playerRemapControlType, backColor, textPrintTypeLeftAligned);
        
        int vehiclesOwned = GetTotals((void *)house + HC_UNITS_BOUGHT);
        int infantryOwned = GetTotals((void *)house + HC_INFANTRY_BOUGHT);
        int buildingsOwned = GetTotals((void *)house + HC_BUILDINGS_BOUGHT);
        int planesOwned = GetTotals((void *)house + HC_PLANES_BOUGHT);
        Fancy_Text_Print(" %d/%d/%d/%d", xRightAligned, (rowHeight * row) + y, playerRemapControlType, backColor, textPrintTypeRightAligned, vehiclesOwned, infantryOwned, buildingsOwned, planesOwned);
    }
    
    Fancy_Text_Print("Vehic/Inf/Build/Air Killed", xLeftAligned, (rowHeight * ++row) + y, remapControlType, backColor, 0x16);
    for (int i = 0; i < HumanPlayers; i++)
    {
        int *house = HouseClass__As_Pointer(12 + i); // 12 = Multi1 aka the first multiplayer house
        int *isSpectator = (void *)house + HC_IS_SPECTATOR;
        if (*isSpectator) continue;
        int *playerRemapControlType = GetPlayerRemapControlType(house);
        row++;
        
        char *playerName = (void *)house + HC_PLAYER_NAME;
        Fancy_Text_Print(playerName, xLeftAligned, (rowHeight * row) + y, playerRemapControlType, backColor, textPrintTypeLeftAligned);
        
        int vehiclesKilled = GetTotals((void *)house + HC_UNITS_KILLED);
        int infantryKilled = GetTotals((void *)house + HC_INFANTRY_KILLED);
        int buildingsKilled = GetTotals((void *)house + HC_BUILDINGS_KILLED);
        int planesKilled = GetTotals((void *)house + HC_PLANES_KILLED);
        Fancy_Text_Print(" %d/%d/%d/%d", xRightAligned, (rowHeight * row) + y, playerRemapControlType, backColor, textPrintTypeRightAligned, vehiclesKilled, infantryKilled, buildingsKilled, planesKilled);
    }
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
