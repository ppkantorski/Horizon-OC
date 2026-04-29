/*
 * kip.cpp - STUB
 *
 * KIP reading and writing has been removed from this sysmodule.
 * The KIP is managed externally by the user and must not be touched at runtime.
 * GetKipData() is a no-op; SetKipData() is never called by the IPC service.
 */

#include "kip.hpp"
#include "file_utils.hpp"

namespace kip {

    bool kipAvailable = false;

    // No-op: sysmodule never reads or writes the KIP.
    void GetKipData()
    {
        fileUtils::LogLine("[kip] KIP management disabled; using externally supplied KIP.");
        kipAvailable = false;
    }

    // SetKipData() is intentionally not implemented.
    // It is not exposed via IPC in this build.

} // namespace kip
