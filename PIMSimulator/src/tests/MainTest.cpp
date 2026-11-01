/***************************************************************************************************
 * Copyright (C) 2021 Samsung Electronics Co. LTD
 *
 * This software is a property of Samsung Electronics.
 * No part of this software, either material or conceptual may be copied or distributed,
 * transmitted, transcribed, stored in a retrieval system, or translated into any human
 * or computer language in any form by any means,electronic, mechanical, manual or otherwise,
 * or disclosed to third parties without the express written permission of Samsung Electronics.
 * (Use of the Software is restricted to non-commercial, personal or academic, research purpose
 * only)
 **************************************************************************************************/

#include <bitset>
#include <cstdint>
#include <iostream>
#include <limits>
#include <memory>
#include <random>

#include "Burst.h"
#include "MultiChannelMemorySystem.h"
#include "gtest/gtest.h"
#include "tests/PIMKernel.h"
#include "tests/TestCases.h"

using namespace std;

// A2: AAO_ENABLED is defined in PrintMacros.cpp. Expose it here so the
// runtime env-var switch can override the compiled default (true).
extern bool AAO_ENABLED;

int main(int argc, char* argv[])
{
    // A2: Allow scripted evaluation of all three paper modes without recompiling:
    //   AAO=0  -> PIM-ACT w/o AAO
    //   AAO=1  -> PIM-ACT w/ AAO  (compiled default)
    //   (unset) -> compiled default (currently true)
    if (const char* v = getenv("AAO"))
    {
        AAO_ENABLED = (atoi(v) != 0);
        std::cerr << "== [ComPASS Config] AAO_ENABLED set to " << AAO_ENABLED << std::endl;
    }

    if (argc > 1)
    {
        ::testing::InitGoogleTest(&argc, argv);
    }
    else
    {
        ::testing::InitGoogleTest(&argc, argv);
        testing::GTEST_FLAG(filter) = "-*bw*.*";
    }

    return RUN_ALL_TESTS();
}
