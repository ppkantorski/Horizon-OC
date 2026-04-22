#include "bq24193.hpp"

namespace bq24193 {
    static u8 bq24193_get_reg(u8 reg)
    {
        u8 out;
        I2cRead_OutU8(I2cDevice_Bq24193, reg, &out);
        return out;
    }
    u8 getBQTemp() {
        u8 regVal = bq24193_get_reg(BQ24193_FaultReg);
        return regVal & BQ24193_FAULT_THERM_MASK;
    }
}
