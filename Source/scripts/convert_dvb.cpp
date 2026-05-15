#include <cstdio>
#include <cstdint>
#include <iterator>

typedef uint32_t u32;
typedef int32_t s32;

struct DvbEntry {
    u32 freq;
    u32 volts[3];
};

DvbEntry oldDvbTable[] = {
    {  204000, { 637, 637, 637, }, },
    { 1331200, { 650, 637, 637, }, },
    { 1600000, { 675, 650, 637, }, },
    { 1866000, { 700, 675, 650, }, },
    { 2133000, { 725, 700, 675, }, },
    { 2400000, { 750, 725, 700, }, },
    { 2666000, { 775, 750, 725, }, },
    { 2933000, { 800, 775, 750, }, },
    { 3200000, { 800, 800, 775, }, },
    {     ~0u, {                }, },
};

DvbEntry newDvbTable[] = {
    {  204000, {  637,  637,  637, }, },
    { 1331200, {  650,  637,  637, }, },
    { 1600000, {  675,  650,  637, }, },
    { 1866000, {  700,  675,  650, }, },
    { 2133000, {  725,  700,  675, }, },
    { 2400000, {  750,  725,  700, }, },
    { 2666000, {  850,  825,  800, }, },
    { 2933000, {  950,  925,  900, }, },
    { 3200000, { 1050, 1025, 1000, }, },
    {     ~0u, {                   }, },
};

constexpr u32 DvbTableSize = std::size(oldDvbTable);

u32 PrintAndScan(const char *message) {
    u32 scanV;
    printf("%s: ", message);
    scanf("%i", &scanV);
    return scanV;
}

u32 GetProcessId(u32 speedo) {
    if (speedo <= 1597) {
        return 0;
    }

    if (speedo <= 1708) {
        return 1;
    }

    /* >= 1709. */
    return 2;
}

u32 GetVoltageAndIndex(u32 dvbShift, u32 emc, u32 processId, DvbEntry *dvbTable, u32 &index) {
    for (u32 i = 0; i < DvbTableSize - 1; ++i) {
        if (emc < dvbTable[i].freq || emc >= dvbTable[i + 1].freq) {
            continue;
        }

        index = i;
        return dvbTable[i].volts[processId] + (25 * dvbShift);
    }

    return 0;
}

s32 GetShift(u32 oldVoltage, u32 processId, DvbEntry *dvbTable, u32 index) {
    return (static_cast<s32>(oldVoltage) - static_cast<s32>(dvbTable[index].volts[processId])) / 25;
}

int main() {
    u32 oldDvb    = PrintAndScan("Enter old dvb shift");
    u32 emcMaxMhz = PrintAndScan("Enter max ram freq (MHz)");
    u32 speedo    = PrintAndScan("Enter soc speedo");

    u32 emcMaxKhz = emcMaxMhz * 1000;
    u32 processId = GetProcessId(speedo);

    #define INVALID_TABLE_INDEX 32
    u32 tableIndex = INVALID_TABLE_INDEX;
    u32 oldVoltage = GetVoltageAndIndex(oldDvb, emcMaxKhz, processId, oldDvbTable, tableIndex);

    if (oldVoltage == 0 || tableIndex == INVALID_TABLE_INDEX) {
        printf("Invalid values!\n");
        return -1;
    }

    s32 newShift = GetShift(oldVoltage, processId, newDvbTable, tableIndex);

    printf("New dvb table shift: %d", newShift);
}
