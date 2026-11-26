/*
    SPDX-FileCopyrightText: 2020-2026 Jakub Stankowski <jakub.stankowski@put.poznan.pl>
    SPDX-License-Identifier: BSD-3-Clause
*/

#define DOCTEST_CONFIG_IMPLEMENT_WITH_MAIN
#include <doctest/doctest.h>
#include "xCommonDefJPEG.h"
#include "xJPEG_CodecCommon.h"

using namespace PMBB_NAMESPACE;
using namespace PMBB_NAMESPACE::JPEG;

//===============================================================================================================================================================================================================

TEST_CASE("xCodecCommon_calcNumMCUsInWidthAndHeight")
{
  CHECK(xCodecCommon::calcNumMCUsInWidth ({ 512, 512 }, eCrF::CF420) == 32);
  CHECK(xCodecCommon::calcNumMCUsInHeight({ 512, 512 }, eCrF::CF420) == 32);
  CHECK(xCodecCommon::calcNumMCUsInWidth ({ 512, 512 }, eCrF::CF422) == 32);
  CHECK(xCodecCommon::calcNumMCUsInHeight({ 512, 512 }, eCrF::CF422) == 64);
  CHECK(xCodecCommon::calcNumMCUsInWidth ({ 512, 512 }, eCrF::CF444) == 64);
  CHECK(xCodecCommon::calcNumMCUsInHeight({ 512, 512 }, eCrF::CF444) == 64);

  // Non-multiple dimensions round up to full MCUs
  CHECK(xCodecCommon::calcNumMCUsInWidth ({ 17, 17 }, eCrF::CF420) == 2);
  CHECK(xCodecCommon::calcNumMCUsInHeight({ 17, 17 }, eCrF::CF420) == 2);
  CHECK(xCodecCommon::calcNumMCUsInWidth ({  8,  8 }, eCrF::CF420) == 1);
  CHECK(xCodecCommon::calcNumMCUsInHeight({  8,  8 }, eCrF::CF420) == 1);
}

TEST_CASE("xCodecCommon_calcNumParallelWorkUnits")
{
  // MT mode (RestartIntervalUsed == 0): number of MCU rows
  CHECK(xCodecCommon::calcNumParallelWorkUnits({ 1920, 1080 }, eCrF::CF420, 0) == 68);
  CHECK(xCodecCommon::calcNumParallelWorkUnits({   64,   64 }, eCrF::CF420, 0) == 4);
  CHECK(xCodecCommon::calcNumParallelWorkUnits({   16,   16 }, eCrF::CF420, 0) == 1);

  // MT+R mode (RestartIntervalUsed > 0): number of restart slices
  // 64x64 4:2:0 -> 4x4 = 16 MCUs; ri=4 (1 MCU row) -> 4 slices
  CHECK(xCodecCommon::calcNumParallelWorkUnits({ 64, 64 }, eCrF::CF420, 4) == 4);
  // ri=10 -> ceil(16/10) = 2 slices
  CHECK(xCodecCommon::calcNumParallelWorkUnits({ 64, 64 }, eCrF::CF420, 10) == 2);
  // Large restart interval (>= total MCUs) -> 1 slice
  CHECK(xCodecCommon::calcNumParallelWorkUnits({ 64, 64 }, eCrF::CF420, 100) == 1);
}

TEST_CASE("xCodecCommon_selectAdaptiveThreadCount")
{
  // Normal cases: min(HardwareThreads, AvailableWorkUnits)
  CHECK(xCodecCommon::selectAdaptiveThreadCount(16, 68) == 16);
  CHECK(xCodecCommon::selectAdaptiveThreadCount(16,  4) ==  4);
  CHECK(xCodecCommon::selectAdaptiveThreadCount( 8,  2) ==  2);

  // Single or zero work units -> serial execution (0)
  CHECK(xCodecCommon::selectAdaptiveThreadCount(16, 1) == 0);
  CHECK(xCodecCommon::selectAdaptiveThreadCount(16, 0) == 0);

  // hardware_concurrency() == 0 fallback to 1 -> min(1, WorkUnits) == 1 -> serial (0)
  CHECK(xCodecCommon::selectAdaptiveThreadCount(0, 68) == 0);
  CHECK(xCodecCommon::selectAdaptiveThreadCount(0,  1) == 0);
}

//===============================================================================================================================================================================================================
