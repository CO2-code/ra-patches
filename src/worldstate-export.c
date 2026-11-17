#include <windows.h>
#include <stdint.h>
#include <stddef.h>
#include "ra95.h"
#include "houseclass.h"

typedef HANDLE (WINAPI *WorldStateCreateFileA)(LPCSTR, DWORD, DWORD, LPSECURITY_ATTRIBUTES, DWORD, DWORD, HANDLE);
typedef BOOL (WINAPI *WorldStateWriteFile)(HANDLE, LPCVOID, DWORD, LPDWORD, LPOVERLAPPED);
typedef BOOL (WINAPI *WorldStateCloseHandle)(HANDLE);
typedef DWORD (WINAPI *WorldStateSetFilePointer)(HANDLE, LONG, PLONG, DWORD);

static char output[131072];
static unsigned output_size;
static int output_failed;
static int export_started;
static uint32_t last_frame;

static void put_char(char c) {
    if (output_size < sizeof(output) - 2) output[output_size++] = c;
    else output_failed = 1;
}

static void put_text(const char *s) {
    while (*s) put_char(*s++);
}

static void put_unsigned(uint32_t value) {
    char digits[12];
    unsigned count = 0;
    do {
        digits[count++] = (char)('0' + value % 10);
        value /= 10;
    } while (value && count < sizeof(digits));
    while (count) put_char(digits[--count]);
}

static void put_signed(int32_t value) {
    if (value < 0) {
        put_char('-');
        put_unsigned((uint32_t)(-(value + 1)) + 1);
    } else {
        put_unsigned((uint32_t)value);
    }
}

static void put_string(const char *s) {
    put_char('"');
    if (s) {
        while (*s) {
            unsigned char c = (unsigned char)*s++;
            if (c == '"' || c == '\\') {
                put_char('\\');
                put_char((char)c);
            } else if (c >= 32) {
                put_char((char)c);
            } else {
                put_char('?');
            }
        }
    }
    put_char('"');
}



static void put_string_limit(const char *s, unsigned limit) {
    put_char('"');
    while (s && limit-- && *s) {
        unsigned char c = (unsigned char)*s++;
        if (c == '"' || c == '\\') {
            put_char('\\');
            put_char((char)c);
        } else if (c >= 32) {
            put_char((char)c);
        } else {
            put_char('?');
        }
    }
    put_char('"');
}

static int write_output(void) {
    WorldStateCreateFileA create_file = *(WorldStateCreateFileA *)0x5E658C;
    WorldStateWriteFile write_file = *(WorldStateWriteFile *)0x5E6684;
    WorldStateCloseHandle close_handle = *(WorldStateCloseHandle *)0x5E6580;
    WorldStateSetFilePointer set_file_pointer = *(WorldStateSetFilePointer *)0x5E6668;
    HANDLE file;
    DWORD written = 0;
    file = create_file("worldstate-export.jsonl", GENERIC_WRITE, FILE_SHARE_READ, 0, export_started ? OPEN_ALWAYS : CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, 0);
    if (file == INVALID_HANDLE_VALUE) return 0;
    set_file_pointer(file, 0, 0, FILE_END);
    output[output_size++] = '\n';
    if (!write_file(file, output, output_size, &written, 0) || written != output_size) {
        close_handle(file);
        return 0;
    }
    close_handle(file);
    export_started = 1;
    return 1;
}

void WorldState_ExportSnapshot(void) {
    size_t i;
    unsigned count = 0;
    uint32_t frame = *(uint32_t *)0x006680C4;
    size_t house_count = *(size_t *)0x0065D9A0;
    void **houses = *(void ***)0x0065D9C8;
    if (!*(uint32_t *)0x00669924) return;
    if (frame < last_frame) export_started = 0;
    last_frame = frame;
    if ((frame % 60) != 0) return;
    output_size = 0;
    output_failed = 0;
    put_text("{\"schema\":\"RARPP.WorldStateExport\",\"version\":1,\"frame\":");
    put_unsigned(frame);
    put_text(",\"snapshotKind\":\"partial-house-state\",\"objectsComplete\":false,\"objects\":[],\"houses\":[");
    for (i = 0; i < house_count && i < 20; i++) {
        unsigned char *house = (unsigned char *)houses[i];
        unsigned char flags;
        if (!house) continue;
        flags = house[HC_FLAGS1];
        if (!(flags & HC_IsActive)) continue;
        if (count++) put_char(',');
        put_text("{\"id\":");
        put_unsigned((uint32_t)i);
        put_text(",\"houseType\":");
        put_unsigned(house[HC_HousesType]);
        put_text(",\"color\":");
        put_unsigned(house[HC_PLAYER_COLOR]);
        put_text(",\"name\":");
        put_string_limit((const char *)(house + HC_PLAYER_NAME), 12);
        put_text(",\"credits\":");
        put_signed(*(int32_t *)(house + HC_Credits));
        put_text(",\"powerGenerated\":");
        put_signed(*(int32_t *)(house + HC_Power));
        put_text(",\"powerConsumed\":");
        put_signed(*(int32_t *)(house + HC_Drain));
        put_text(",\"isDefeated\":");
        put_text((house[HC_FLAGS2] & HC_Defeated) ? "true" : "false");
        put_text(",\"isSpectator\":");
        put_text(*(uint8_t *)(house + HC_IS_SPECTATOR) ? "true" : "false");
        put_text(",\"unitCount\":");
        put_signed(*(int32_t *)(house + HC_CurrentUnitCount));
        put_text(",\"buildingCount\":");
        put_signed(*(int32_t *)(house + HC_CurrentBuildingCount));
        put_text(",\"infantryCount\":");
        put_signed(*(int32_t *)(house + HC_CurrentInfantryCount));
        put_text(",\"vesselCount\":");
        put_signed(*(int32_t *)(house + HC_CurrentVesselCount));
        put_text(",\"aircraftCount\":");
        put_signed(*(int32_t *)(house + HC_CurrentAircraftCount));
        put_char('}');
    }
    put_text("]}");
    if (!output_failed) write_output();
}
