#include <windows.h>
#include <stdint.h>
#include "houseclass.h"

#define WS_OBJECT_LIMIT 4096u
#define WS_ADDR_GAME_ACTIVE 0x00669924u
#define WS_ADDR_FRAME 0x006680C4u
#define WS_ADDR_HOUSE_COUNT 0x0065D9A0u
#define WS_ADDR_HOUSE_PTRS 0x0065D9C8u
#define WS_ADDR_HOUSE_HEAP 0x006017F0u
#define WS_ADDR_BUILDING_CENTER_TABLE 0x005FEBB0u
#define WS_ADDR_HARVESTER_MAX_BAILS 0x00666884u

typedef HANDLE (WINAPI *WorldStateCreateFileA)(LPCSTR, DWORD, DWORD, LPSECURITY_ATTRIBUTES, DWORD, DWORD, HANDLE);
typedef BOOL (WINAPI *WorldStateWriteFile)(HANDLE, LPCVOID, DWORD, LPDWORD, LPOVERLAPPED);
typedef BOOL (WINAPI *WorldStateCloseHandle)(HANDLE);
typedef DWORD (WINAPI *WorldStateSetFilePointer)(HANDLE, LONG, PLONG, DWORD);

static char output[32768];
static unsigned output_size;
static int output_failed;
static int export_started;
static uint32_t last_frame;
static unsigned object_count;
static HANDLE active_file = INVALID_HANDLE_VALUE;

static void flush_buffer(void) {
    WorldStateWriteFile write_file = *(WorldStateWriteFile *)0x5E6684;
    DWORD written = 0;
    if (output_failed || !output_size) return;
    if (active_file == INVALID_HANDLE_VALUE ||
        !write_file(active_file, output, output_size, &written, 0) ||
        written != output_size) {
        output_failed = 1;
    }
    output_size = 0;
}

static void put_char(char c) {
    if (output_failed) return;
    if (output_size >= sizeof(output) - 2) {
        flush_buffer();
        if (output_failed) return;
    }
    output[output_size++] = c;
}

static void put_text(const char *s) {
    while (*s && !output_failed) put_char(*s++);
}

static void put_unsigned(uint32_t value) {
    char digits[12];
    unsigned count = 0;
    do {
        digits[count++] = (char)('0' + value % 10u);
        value /= 10u;
    } while (value && count < sizeof(digits));
    while (count) put_char(digits[--count]);
}

static void put_signed(int32_t value) {
    if (value < 0) {
        put_char('-');
        put_unsigned((uint32_t)(-(value + 1)) + 1u);
    } else {
        put_unsigned((uint32_t)value);
    }
}

static void put_string(const char *s) {
    put_char('"');
    if (s) {
        while (*s && !output_failed) {
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
    while (s && limit-- && *s && !output_failed) {
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

static const unsigned char *resolve_ccptr(uint32_t heap_global_addr, int32_t ccptr_id) {
    const unsigned char *heap;
    uint32_t stride;
    uint32_t total;
    const unsigned char *buffer;
    if (ccptr_id < 0) return 0;
    heap = *(const unsigned char * const *)heap_global_addr;
    if (!heap) return 0;
    stride = *(const uint32_t *)(heap + 0x04);
    total = *(const uint32_t *)(heap + 0x08);
    buffer = *(const unsigned char * const *)(heap + 0x10);
    if (!buffer || stride < 16u || stride > 8192u || (uint32_t)ccptr_id >= total) return 0;
    return buffer + (uint32_t)ccptr_id * stride;
}

static void emit_pool(
    const char *pool,
    uint8_t expected_rtti,
    uint32_t active_count_addr,
    uint32_t active_ptrs_addr,
    uint32_t class_offset,
    uint32_t type_heap_addr,
    uint32_t type_enum_offset,
    int32_t turret_offset
) {
    uint32_t active_count = *(const volatile uint32_t *)active_count_addr;
    const unsigned char * const *active_ptrs = *(const unsigned char * const * const *)active_ptrs_addr;
    uint32_t i;

    if (!active_ptrs || !active_count || active_count > WS_OBJECT_LIMIT) return;

    for (i = 0; i < active_count && !output_failed; i++) {
        const unsigned char *obj = active_ptrs[i];
        const unsigned char *type_ptr;
        const unsigned char *house_ptr;
        uint32_t coord;
        int32_t x_lepton;
        int32_t y_lepton;
        int32_t type_id = -1;
        int32_t owner_house_id = -1;
        int32_t hp;
        int32_t max_hp = -1;
        int32_t turret_facing = -1;
        int32_t mission;
        int has_turret = 0;

        if (!obj) continue;
        if (obj[0x00] != expected_rtti) continue;
        if (!(obj[0x0D] & 0x01)) continue; /* !IsActive */
        if (obj[0x15] & 0x08) continue;    /* IsInLimbo */

        coord = *(const uint32_t *)(obj + 0x05);
        x_lepton = (int32_t)(coord & 0xFFFFu);
        y_lepton = (int32_t)((coord >> 16) & 0xFFFFu);

        type_ptr = resolve_ccptr(type_heap_addr, *(const int32_t *)(obj + class_offset));
        if (type_ptr) {
            type_id = (int32_t)*(const int8_t *)(type_ptr + type_enum_offset);
            max_hp = (int32_t)*(const uint16_t *)(type_ptr + 0x12A);
            has_turret = (type_ptr[0x139] & 0x40) ? 1 : 0;
            if (expected_rtti == 5 && coord != 0u) {
                uint8_t bsize = type_ptr[0x1B2];
                if (bsize < 9u) {
                    uint32_t center_off = ((const uint32_t *)WS_ADDR_BUILDING_CENTER_TABLE)[bsize];
                    x_lepton += (int32_t)(int16_t)(center_off & 0xFFFFu);
                    y_lepton += (int32_t)(int16_t)(center_off >> 16);
                }
            }
        }

        house_ptr = resolve_ccptr(WS_ADDR_HOUSE_HEAP, *(const int32_t *)(obj + 0x93));
        if (house_ptr && house_ptr[HC_HousesType] < 20u) {
            owner_house_id = (int32_t)house_ptr[HC_HousesType];
        }

        hp = (int32_t)*(const int16_t *)(obj + 0x25);
        mission = (int32_t)*(const int8_t *)(obj + 0x27);
        if (has_turret && turret_offset >= 0) {
            turret_facing = (int32_t)obj[(uint32_t)turret_offset];
        }

        if (object_count++) put_char(',');
        put_text("{\"pool\":");
        put_string(pool);
        put_text(",\"rtti\":");
        put_unsigned((uint32_t)expected_rtti);
        put_text(",\"id\":");
        put_signed(*(const int32_t *)(obj + 0x01));
        put_text(",\"typeId\":");
        if (type_id >= 0) put_signed(type_id); else put_text("null");
        put_text(",\"ownerHouseId\":");
        if (owner_house_id >= 0) put_signed(owner_house_id); else put_text("null");
        put_text(",\"xLepton\":");
        put_signed(x_lepton);
        put_text(",\"yLepton\":");
        put_signed(y_lepton);
        put_text(",\"hp\":");
        put_signed(hp);
        put_text(",\"maxHp\":");
        if (max_hp > 0) put_signed(max_hp); else put_text("null");
        put_text(",\"facing\":");
        put_unsigned((uint32_t)obj[0xBA]);
        put_text(",\"turretFacing\":");
        if (turret_facing >= 0) put_signed(turret_facing); else put_text("null");
        put_text(",\"mission\":");
        put_signed(mission);
        put_text(",\"isInLimbo\":false");

        if (expected_rtti == 28 && type_id == 7) {
            int32_t bails = *(const int32_t *)(obj + 0x165);
            int32_t max_bails = *(const volatile int32_t *)WS_ADDR_HARVESTER_MAX_BAILS;
            if (max_bails > 0 && max_bails <= 1024 && bails >= 0 && bails <= max_bails * 2) {
                int32_t load_pct = (bails * 100) / max_bails;
                if (load_pct > 100) load_pct = 100;
                put_text(",\"harvestLoad\":");
                put_signed(load_pct);
            }
        }

        put_char('}');
    }
}

void WorldState_ExportSnapshot(void) {
    WorldStateCreateFileA create_file = *(WorldStateCreateFileA *)0x5E658C;
    WorldStateCloseHandle close_handle = *(WorldStateCloseHandle *)0x5E6580;
    WorldStateSetFilePointer set_file_pointer = *(WorldStateSetFilePointer *)0x5E6668;
    size_t i;
    unsigned count = 0;
    uint32_t frame = *(const volatile uint32_t *)WS_ADDR_FRAME;
    size_t house_count = *(const volatile size_t *)WS_ADDR_HOUSE_COUNT;
    void * const *houses = *(void * const * const *)WS_ADDR_HOUSE_PTRS;

    if (!*(const volatile uint32_t *)WS_ADDR_GAME_ACTIVE) return;
    if (frame < last_frame) export_started = 0;
    last_frame = frame;
    if ((frame % 60u) != 0u) return;

    active_file = create_file(
        "worldstate-export.jsonl",
        GENERIC_WRITE,
        FILE_SHARE_READ,
        0,
        export_started ? OPEN_ALWAYS : CREATE_ALWAYS,
        FILE_ATTRIBUTE_NORMAL,
        0
    );
    if (active_file == INVALID_HANDLE_VALUE) return;
    set_file_pointer(active_file, 0, 0, FILE_END);

    output_size = 0;
    output_failed = 0;
    object_count = 0;

    put_text("{\"schema\":\"RARPP.WorldStateExport\",\"version\":1,\"frame\":");
    put_unsigned(frame);
    put_text(",\"snapshotKind\":\"object-pool-full\",\"objectsComplete\":true,\"objects\":[");

    emit_pool("aircraft", 1u, 0x0065D824u, 0x0065D84Cu, 0x146u, 0x00601820u, 0x196u, -1);
    emit_pool("buildings", 5u, 0x0065D8BCu, 0x0065D8E4u, 0x0CDu, 0x0060181Cu, 0x1A4u, 0xBA);
    emit_pool("infantry", 13u, 0x0065D9ECu, 0x0065DA14u, 0x141u, 0x00601824u, 0x196u, -1);
    emit_pool("units", 28u, 0x0065DC4Cu, 0x0065DC74u, 0x15Cu, 0x00601830u, 0x196u, 0x178);
    emit_pool("vessels", 30u, 0x0065DC98u, 0x0065DCC0u, 0x15Cu, 0x00601834u, 0x196u, 0x176);

    put_text("],\"houses\":[");
    for (i = 0; houses && i < house_count && i < 20u && !output_failed; i++) {
        const unsigned char *house = (const unsigned char *)houses[i];
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
        put_string_limit((const char *)(house + HC_PLAYER_NAME), 12u);
        put_text(",\"credits\":");
        put_signed(*(const int32_t *)(house + HC_Credits));
        put_text(",\"powerGenerated\":");
        put_signed(*(const int32_t *)(house + HC_Power));
        put_text(",\"powerConsumed\":");
        put_signed(*(const int32_t *)(house + HC_Drain));
        put_text(",\"isDefeated\":");
        put_text((house[HC_FLAGS2] & HC_Defeated) ? "true" : "false");
        put_text(",\"isSpectator\":");
        put_text(*(const uint8_t *)(house + HC_IS_SPECTATOR) ? "true" : "false");
        put_text(",\"unitCount\":");
        put_signed(*(const int32_t *)(house + HC_CurrentUnitCount));
        put_text(",\"buildingCount\":");
        put_signed(*(const int32_t *)(house + HC_CurrentBuildingCount));
        put_text(",\"infantryCount\":");
        put_signed(*(const int32_t *)(house + HC_CurrentInfantryCount));
        put_text(",\"vesselCount\":");
        put_signed(*(const int32_t *)(house + HC_CurrentVesselCount));
        put_text(",\"aircraftCount\":");
        put_signed(*(const int32_t *)(house + HC_CurrentAircraftCount));
        put_char('}');
    }
    put_text("]}");

    if (!output_failed) {
        put_char('\n');
        flush_buffer();
    }
    close_handle(active_file);
    active_file = INVALID_HANDLE_VALUE;
    if (!output_failed) {
        export_started = 1;
    }
}
