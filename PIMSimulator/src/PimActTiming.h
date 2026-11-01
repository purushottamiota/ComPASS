#ifndef PIM_ACT_TIMING_H
#define PIM_ACT_TIMING_H

#include <algorithm>
#include <cstdint>
#include "Configuration.h"
#include "PIMCmd.h"
#include "SystemConfiguration.h"

namespace DRAMSim
{

struct PimActTiming
{
    uint64_t tRCD;
    uint64_t tRCDW;
    uint64_t tRAS;
    uint64_t tRC;
};

inline PimActTiming pimActTiming(const Configuration& config, uint8_t optype, bool aaoEnabled)
{
    // B1: tRAS and tRC are standard (no spurious +1)
    // B3: tRCDRD applies to reads, tRCDWR applies to writes (+1 for PIM-ACT)
    uint64_t tRCD_val  = (config.tRCDRD - config.AL) + 1;
    uint64_t tRCDW_val = (config.tRCDWR - config.AL) + 1;
    uint64_t tRAS_val  = config.tRAS;
    uint64_t tRC_val   = config.tRC;

    // B8: Parameterized scaling factors (default: tRCD=0.5, tRAS=0.6, tRC=0.6, tRP=0.5)
    if (aaoEnabled && isAAORegisterOp(optype))
    {
        tRCD_val  = std::max((uint64_t)2, (uint64_t)(config.tRCDRD * AAO_TRCD_FACTOR));
        tRCDW_val = std::max((uint64_t)2, (uint64_t)(config.tRCDWR * AAO_TRCD_FACTOR));
        tRAS_val  = std::max((uint64_t)4, (uint64_t)(config.tRAS * AAO_TRAS_FACTOR));
        tRC_val   = std::max((uint64_t)6, (uint64_t)(config.tRC * AAO_TRC_FACTOR));
    }

    return {tRCD_val, tRCDW_val, tRAS_val, tRC_val};
}

inline uint64_t pimPrechargeTiming(const Configuration& config, uint8_t activeOptype, bool aaoEnabled)
{
    // B2: AAO precharge halving uses activeOptype
    // B8: Parameterized scaling factor (default: 0.5)
    uint64_t tRP_val = config.tRP;
    if (aaoEnabled && isAAORegisterOp(activeOptype))
    {
        tRP_val = std::max((uint64_t)2, (uint64_t)(config.tRP * AAO_TRP_FACTOR));
    }
    return tRP_val;
}

} // namespace DRAMSim

#endif // PIM_ACT_TIMING_H
