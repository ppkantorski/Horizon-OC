#include <switch.h>
#include <sysclk.h>
#include "board/board.hpp"
#include "clock_manager.hpp"
#include <cstring>
#include "file_utils.hpp"
#include "board/board.hpp"
#include "errors.hpp"
#include "config.hpp"
#include "integrations.hpp"
#include <nxExt/cpp/lockable_mutex.h>

namespace governor {
    extern bool isCpuGovernorInBoostMode;
    extern bool isVRREnabled;
    extern bool isGpuGovernorEnabled;
    extern bool isCpuGovernorEnabled;
    extern bool lastGpuGovernorState;
    extern bool lastCpuGovernorState;
    extern bool lastVrrGovernorState;
    void startThreads();
    void exitThreads();
    void HandleGovernor(uint32_t targetHz);
    void CpuGovernorThread(void* arg);
    void GovernorThread(void* arg);
    void VRRThread(void* arg);
}