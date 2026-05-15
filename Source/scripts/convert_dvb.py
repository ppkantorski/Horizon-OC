from dataclasses import dataclass
from typing import List


u32 = int
s32 = int


@dataclass
class DvbEntry:
    freq: u32
    volts: List[u32]


oldDvbTable = [
    DvbEntry(204000,  [637, 637, 637]),
    DvbEntry(1331200, [650, 637, 637]),
    DvbEntry(1600000, [675, 650, 637]),
    DvbEntry(1866000, [700, 675, 650]),
    DvbEntry(2133000, [725, 700, 675]),
    DvbEntry(2400000, [750, 725, 700]),
    DvbEntry(2666000, [775, 750, 725]),
    DvbEntry(2933000, [800, 775, 750]),
    DvbEntry(3200000, [800, 800, 775]),
    DvbEntry(0xFFFFFFFF,      []),
]

newDvbTable = [
    DvbEntry(204000,  [637, 637, 637]),
    DvbEntry(1331200, [650, 637, 637]),
    DvbEntry(1600000, [675, 650, 637]),
    DvbEntry(1866000, [700, 675, 650]),
    DvbEntry(2133000, [725, 700, 675]),
    DvbEntry(2400000, [750, 725, 700]),
    DvbEntry(2666000, [850, 825, 800]),
    DvbEntry(2933000, [950, 925, 900]),
    DvbEntry(3200000, [1050, 1025, 1000]),
    DvbEntry(0xFFFFFFFF,      []),
]

DVB_TABLE_SIZE = len(oldDvbTable)
INVALID_TABLE_INDEX = 32


def print_and_scan(message: str) -> u32:
    return int(input(f"{message}: "))


def get_process_id(speedo: u32) -> u32:
    if speedo <= 1597:
        return 0

    if speedo <= 1708:
        return 1

    # >= 1709
    return 2


def get_voltage_and_index(
    dvb_shift: u32,
    emc: u32,
    process_id: u32,
    dvb_table: List[DvbEntry],
):
    for i in range(DVB_TABLE_SIZE - 1):
        if emc < dvb_table[i].freq or emc >= dvb_table[i + 1].freq:
            continue

        voltage = dvb_table[i].volts[process_id] + (25 * dvb_shift)
        return voltage, i

    return 0, INVALID_TABLE_INDEX


def get_shift(
    old_voltage: u32,
    process_id: u32,
    dvb_table: List[DvbEntry],
    index: u32,
) -> s32:
    return (
        int(old_voltage)
        - int(dvb_table[index].volts[process_id])
    ) // 25


def main():
    old_dvb = print_and_scan("Enter old dvb shift")
    emc_max_mhz = print_and_scan("Enter max ram freq (MHz)")
    speedo = print_and_scan("Enter soc speedo")

    emc_max_khz = emc_max_mhz * 1000
    process_id = get_process_id(speedo)

    table_index = INVALID_TABLE_INDEX

    old_voltage, table_index = get_voltage_and_index(
        old_dvb,
        emc_max_khz,
        process_id,
        oldDvbTable,
    )

    if old_voltage == 0 or table_index == INVALID_TABLE_INDEX:
        print("Invalid values!")
        return -1

    new_shift = get_shift(
        old_voltage,
        process_id,
        newDvbTable,
        table_index,
    )

    print(f"New dvb table shift: {new_shift}")


if __name__ == "__main__":
    main()
