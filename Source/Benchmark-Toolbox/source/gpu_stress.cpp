extern "C" {
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <switch.h>

#include "gpu_stress.h"
}
#include <deko3d.hpp>

#include "compute_shader_bin.h"

static constexpr uint32_t kDispatchX = 96u;
static constexpr uint32_t kLocalX = 128u;
static constexpr uint32_t kThreads = kDispatchX * kLocalX;
static constexpr uint32_t kSeedBytes = kThreads * 4u;
static constexpr uint32_t kOutU32s = kThreads * 4u;
static constexpr uint32_t kOutBytes = kOutU32s * 4u;
static constexpr uint32_t kScrBytes = 0x400000u;
static constexpr uint32_t kUboAlloc = 0x1000u;
static constexpr uint32_t kUboBindSz = 0x100u;
static constexpr uint32_t kCmdbufSz = 0x20000u;

static constexpr uint32_t kDataFlags = DkMemBlockFlags_CpuUncached | DkMemBlockFlags_GpuCached;
static constexpr uint32_t kCodeFlags = DkMemBlockFlags_CpuUncached | DkMemBlockFlags_GpuCached | DkMemBlockFlags_Code;

static constexpr double kOpsPerIter = 259.0;

static constexpr uint32_t kTargetMs = 15u;

static constexpr uint32_t kWindowIters = 16u;
static constexpr bool kCpuDetect = true;
static constexpr bool kCvDetect = true;

static uint64_t read_cntpct(void) {
    uint64_t t;
    asm volatile("mrs %0, cntpct_el0" : "=r"(t));
    return t;
}
static uint64_t cntpct_to_ms(uint64_t ticks) {

    return (ticks * 625ULL) / 12000000ULL;
}
static uint32_t round_up_4k(uint32_t v) {
    return (v + 4095u) & ~4095u;
}

static void init_seed_block(uint32_t *out, uint32_t count) {
    for (uint32_t a = 0; a < count; a++) {
        uint32_t m = a * 0x9e3779b9u;
        uint8_t b0 = (uint8_t)(m >> 16) ^ (uint8_t)(a) ^ 0xa5u;
        uint8_t b1 = (uint8_t)(m >> 24) ^ (uint8_t)(a >> 8) ^ 0xa5u;
        uint32_t v = ((uint32_t)b1 << 24) | ((uint32_t)b0 << 16) | ((uint32_t)((uint8_t)(b1 ^ (uint8_t)(m >> 8) ^ 0x5au)) << 8) |
                     (uint32_t)((uint8_t)(b0 ^ (uint8_t)m ^ 0x5au));
        uint32_t u = v * 0x7feb352du;
        uint8_t c1 = (uint8_t)(u >> 24);
        uint32_t w = ((uint32_t)c1 << 24) | ((uint32_t)((uint8_t)((c1 >> 7) ^ (uint8_t)(u >> 16))) << 16) |
                     ((uint32_t)((uint8_t)((uint8_t)(u >> 23) ^ (uint8_t)(u >> 8))) << 8) | (uint32_t)((uint8_t)((uint8_t)(u >> 15) ^ (uint8_t)u));
        out[a] = (w * 0x846ca68bu) | 1u;
    }
}

static void init_scratch(uint32_t *out, uint32_t count) {
    uint32_t s1 = 0x6d2b79f5u;
    uint32_t s2 = 0xc2b2ae35u;
    uint32_t acc = 0u;
    for (uint32_t i = 0; i < count; i++) {
        s1 += acc;
        uint32_t os2 = s2;
        acc += 0x9e3779b9u;
        s2 = os2 + 0x85ebca6bu;
        uint32_t m1 = os2;
        m1 = (m1 ^ (m1 >> 16u)) * 0x7feb352du;
        m1 = (m1 ^ (m1 >> 15u)) * 0x846ca68bu;
        m1 ^= m1 >> 16u;
        uint32_t m2 = s1;
        m2 = (m2 ^ (m2 >> 16u)) * 0x7feb352du;
        m2 = (m2 ^ (m2 >> 15u)) * 0x846ca68bu;
        m2 ^= m2 >> 16u;
        out[i] = m1 ^ m2;
    }
}

struct StressState {
    dk::UniqueDevice device;
    dk::UniqueQueue queue;
    dk::UniqueMemBlock codeBlock;
    dk::UniqueMemBlock paramsBlock;
    dk::UniqueMemBlock seedBlock;
    dk::UniqueMemBlock scratchBlock;
    dk::UniqueMemBlock outABlock;
    dk::UniqueMemBlock outBBlock;
    dk::UniqueMemBlock cmdmemBlock;
    dk::UniqueCmdBuf cmdbuf;
    dk::Shader computeShader;
    DkCmdList listA = 0;
    DkCmdList listB = 0;
    uint32_t *params = nullptr;
    uint32_t *outA = nullptr;
    uint32_t *outB = nullptr;
    uint32_t *golden = nullptr;
    uint32_t batch_size = 1024u;
    uint64_t cum_dispatches = 0;
    uint64_t start_ms = 0;
    bool initialized = false;
    bool failed = false;
};
static StressState g;

static bool stress_init(void) {
    g.device = dk::DeviceMaker{}.create();
    if (!g.device) {
        printf("deko3d: device create failed\n");
        return false;
    }

    g.queue = dk::QueueMaker{ g.device }.setFlags(DkQueueFlags_Compute).create();
    if (!g.queue) {
        printf("deko3d: queue create failed\n");
        return false;
    }

    uint32_t codeSize = round_up_4k(compute_shader_bin_size);
    g.codeBlock = dk::MemBlockMaker{ g.device, codeSize }.setFlags(kCodeFlags).create();
    if (!g.codeBlock) {
        printf("deko3d: code memblock failed\n");
        return false;
    }
    memcpy(g.codeBlock.getCpuAddr(), compute_shader_bin, compute_shader_bin_size);
    dk::ShaderMaker{ g.codeBlock, 0 }.initialize(g.computeShader);

    g.paramsBlock = dk::MemBlockMaker{ g.device, kUboAlloc }.setFlags(kDataFlags).create();
    g.seedBlock = dk::MemBlockMaker{ g.device, kSeedBytes }.setFlags(kDataFlags).create();
    g.scratchBlock = dk::MemBlockMaker{ g.device, kScrBytes }.setFlags(kDataFlags).create();
    g.outABlock = dk::MemBlockMaker{ g.device, kOutBytes }.setFlags(kDataFlags).create();
    g.outBBlock = dk::MemBlockMaker{ g.device, kOutBytes }.setFlags(kDataFlags).create();
    g.cmdmemBlock = dk::MemBlockMaker{ g.device, kCmdbufSz }.setFlags(kDataFlags).create();
    if (!g.paramsBlock || !g.seedBlock || !g.scratchBlock || !g.outABlock || !g.outBBlock || !g.cmdmemBlock) {
        printf("deko3d: memblock alloc failed\n");
        return false;
    }

    g.params = (uint32_t *)g.paramsBlock.getCpuAddr();
    auto *seeds = (uint32_t *)g.seedBlock.getCpuAddr();
    auto *scratch = (uint32_t *)g.scratchBlock.getCpuAddr();
    g.outA = (uint32_t *)g.outABlock.getCpuAddr();
    g.outB = (uint32_t *)g.outBBlock.getCpuAddr();

    g.batch_size = 1024u;
    g.params[0] = g.batch_size;
    g.params[1] = 0;
    g.params[2] = 0;
    g.params[3] = 0;
    init_seed_block(seeds, kThreads);

    init_scratch(scratch, kScrBytes / 4u);
    for (uint32_t i = 0; i < kOutU32s; i++) {
        g.outA[i] = 0xcafebabeu;
        g.outB[i] = 0xcafebabeu;
    }

    g.cmdbuf = dk::CmdBufMaker{ g.device }.create();
    if (!g.cmdbuf) {
        printf("deko3d: cmdbuf create failed\n");
        return false;
    }

    auto record = [&](dk::MemBlock outputBlock, uint32_t memOffset) -> DkCmdList {
        g.cmdbuf.addMemory(g.cmdmemBlock, memOffset, kCmdbufSz / 2);
        g.cmdbuf.bindShaders(DkStageFlag_Compute, { &g.computeShader });
        DkBufExtents ubo = { g.paramsBlock.getGpuAddr(), kUboBindSz };
        g.cmdbuf.bindUniformBuffers(DkStage_Compute, 0, { ubo });
        DkBufExtents sb0 = { g.seedBlock.getGpuAddr(), kSeedBytes };
        DkBufExtents sb1 = { outputBlock.getGpuAddr(), kOutBytes };
        DkBufExtents sb2 = { g.scratchBlock.getGpuAddr(), kScrBytes };
        g.cmdbuf.bindStorageBuffers(DkStage_Compute, 0, { sb0, sb1, sb2 });
        g.cmdbuf.dispatchCompute(kDispatchX, 1, 1);
        return g.cmdbuf.finishList();
    };
    g.listA = record(g.outABlock, 0);
    g.listB = record(g.outBBlock, kCmdbufSz / 2);

    uint64_t t0 = read_cntpct();
    g.queue.submitCommands(g.listA);
    g.queue.waitIdle();
    uint64_t cal_ms = cntpct_to_ms(read_cntpct() - t0);
    if (cal_ms == 0) {
        g.batch_size = 1024u;
    } else {
        double scaled = ((double)kTargetMs / (double)cal_ms) * 1024.0;
        if (scaled < 256.0)
            g.batch_size = 256u;
        else if (scaled > 65536.0)
            g.batch_size = 65536u;
        else
            g.batch_size = (uint32_t)scaled;
    }
    g.params[0] = g.batch_size;

    for (uint32_t i = 0; i < kOutU32s; i++)
        g.outA[i] = 0xcafebabeu;
    g.queue.submitCommands(g.listA);
    g.queue.waitIdle();

    bool gpu_wrote = false;
    for (uint32_t i = 0; i < kOutU32s && !gpu_wrote; i++)
        if (g.outA[i] != 0xcafebabeu)
            gpu_wrote = true;
    if (!gpu_wrote) {
        printf("deko3d: golden dispatch produced no GPU writes\n");
        return false;
    }

    g.golden = (uint32_t *)malloc(kOutBytes);
    if (!g.golden) {
        printf("deko3d: OOM for golden\n");
        return false;
    }
    memcpy(g.golden, g.outA, kOutBytes);

    g.start_ms = cntpct_to_ms(read_cntpct());
    g.cum_dispatches = 0;
    return true;
}

extern "C" bool gpu_stress_run(double *gflops_out, uint64_t *dispatches_out, uint64_t *mismatches_out) {
    *gflops_out = 0.0;
    *dispatches_out = 0;
    *mismatches_out = 0;

    if (g.failed)
        return false;
    if (!g.initialized) {
        if (!stress_init()) {
            g.failed = true;
            return false;
        }
        g.initialized = true;
    }

    uint64_t window_dispatches = 0;
    uint64_t window_mismatches = 0;
    for (uint32_t n = 0; n < kWindowIters; n++) {
        g.queue.submitCommands(g.listA);
        window_dispatches++;
        if (kCvDetect) {
            g.queue.submitCommands(g.listB);
            window_dispatches++;
        }
        g.queue.waitIdle();

        if (kCpuDetect) {

            if (memcmp(g.outA, g.golden, kOutBytes) != 0)
                for (uint32_t i = 0; i < kOutU32s; i++)
                    if (g.outA[i] != g.golden[i])
                        window_mismatches++;
        }
        if (kCvDetect) {

            if (memcmp(g.outA, g.outB, kOutBytes) != 0)
                for (uint32_t i = 0; i < kOutU32s; i++)
                    if (g.outB[i] != g.outA[i])
                        window_mismatches++;
        }
    }
    g.cum_dispatches += window_dispatches;

    uint64_t now_ms = cntpct_to_ms(read_cntpct());
    double elapsed_s = (double)(now_ms - g.start_ms) / 1000.0;
    double gflops = (elapsed_s > 0.0) ? ((double)g.batch_size * kOpsPerIter * (double)kThreads * (double)g.cum_dispatches) / (elapsed_s * 1e9) : 0.0;

    *gflops_out = gflops;
    *dispatches_out = window_dispatches;
    *mismatches_out = window_mismatches;
    return true;
}

extern "C" void gpu_stress_shutdown(void) {
    if (g.golden) {
        free(g.golden);
        g.golden = nullptr;
    }
    if (g.queue)
        g.queue.waitIdle();

    g.cmdbuf = {};
    g.cmdmemBlock = {};
    g.outBBlock = {};
    g.outABlock = {};
    g.scratchBlock = {};
    g.seedBlock = {};
    g.paramsBlock = {};
    g.codeBlock = {};
    g.queue = {};
    g.device = {};

    g.listA = g.listB = 0;
    g.params = g.outA = g.outB = nullptr;
    g.batch_size = 1024u;
    g.cum_dispatches = 0;
    g.start_ms = 0;
    g.initialized = false;
    g.failed = false;
}
