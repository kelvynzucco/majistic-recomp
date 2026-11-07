// Adapted for Majin and the Forsaken Kingdom (USA 0.0.0.2)
#include "generated/default/majin_pch.h"
#include <rex/logging.h>
#include <atomic>

#define MAJIN_TRACE_SAVE(address, operation) \
    REX_EXTERN(__imp__sub_##address); \
    REX_EXTERN(sub_##address) { \
        static std::atomic<unsigned> calls{0}; \
        const unsigned id = calls.fetch_add(1, std::memory_order_relaxed); \
        REXLOG_INFO("[Majin save USA] " operation " id={} enter LR={:08X} r3={:08X} r4={:08X} r5={:08X} r6={:08X} r7={:08X} r8={:08X} r9={:08X} r10={:08X}", \
                    id, uint32_t(ctx.lr), ctx.r3.u32, ctx.r4.u32, ctx.r5.u32, \
                    ctx.r6.u32, ctx.r7.u32, ctx.r8.u32, ctx.r9.u32, ctx.r10.u32); \
        __imp__sub_##address(ctx, base); \
        REXLOG_INFO("[Majin save USA] " operation " id={} returned={:08X}", id, ctx.r3.u32); \
    }

// USA addresses (PAL - 0x70):
MAJIN_TRACE_SAVE(8267AF70, "ContentCreateEx")
MAJIN_TRACE_SAVE(8267B000, "ContentClose")
MAJIN_TRACE_SAVE(8267B008, "ContentSetThumbnail")
MAJIN_TRACE_SAVE(8267B4A0, "GetOverlappedResult")
#undef MAJIN_TRACE_SAVE

// WriteFile (PAL 0x8267B6C8 -> USA 0x8267B658) & GetLastError (PAL 0x8267B190 -> USA 0x8267B120)
REX_EXTERN(__imp__sub_8267B658);
REX_EXTERN(__imp__sub_8267B120);
REX_EXTERN(sub_8267B658) {
    const uint32_t caller = uint32_t(ctx.lr);
    const uint32_t requested = ctx.r5.u32;
    const uint32_t bytesOut = ctx.r6.u32;
    const uint32_t overlapped = ctx.r7.u32;
    __imp__sub_8267B658(ctx, base);
    // Caller in save state machine is below 0x821C0000, so LR is 0x821B9DF4 (verified at 0x821B9DF0: bl 0x8267B658)
    if (caller == 0x821B9DF4 && ctx.r3.u32 == 0 && overlapped && requested) {
        auto probe = ctx;
        __imp__sub_8267B120(probe, base);
        const uint32_t status = REX_LOAD_U32(overlapped);
        const uint32_t written = REX_LOAD_U32(overlapped + 4);
        const bool completed = probe.r3.u32 == 997 && status == 0 && written == requested;
        REXLOG_INFO("[Majin save USA] write completion lastError={} status={:08X} bytes={}/{} immediate={}",
                    probe.r3.u32, status, written, requested, completed);
        if (completed) {
            if (bytesOut) REX_STORE_U32(bytesOut, written);
            ctx.r3.u64 = 1;
        }
    }
}

REX_EXTERN(__imp__sub_8267B120);
REX_EXTERN(sub_8267B120) {
    const uint32_t caller = uint32_t(ctx.lr);
    __imp__sub_8267B120(ctx, base);
    if (caller >= 0x821B9000 && caller < 0x821BB000) {
        REXLOG_INFO("[Majin save USA] GetLastError LR={:08X} result={:08X}", caller, ctx.r3.u32);
    }
}

// USA equivalents of diagnostic trace hooks (PAL - 0x70):
REX_EXTERN(__imp__sub_826D3BB0);
REX_EXTERN(__imp__sub_822E9188);
REX_EXTERN(__imp__sub_8236FBF0);

REX_EXTERN(sub_8236FBF0) {
    static std::atomic<unsigned> calls{0};
    const bool trace = calls.fetch_add(1, std::memory_order_relaxed) < 4;
    if (trace) {
        REXLOG_INFO("[Majin loading USA] 8236FBF0 entry LR={:08X} r3={:08X}",
                    uint32_t(ctx.lr), ctx.r3.u32);
    }
    __imp__sub_8236FBF0(ctx, base);
    if (trace) {
        REXLOG_INFO("[Majin loading USA] 8236FBF0 returned r3={:08X}", ctx.r3.u32);
    }
}

REX_EXTERN(sub_822E9188) {
    static std::atomic<unsigned> calls{0};
    if (calls.fetch_add(1, std::memory_order_relaxed) < 4) {
        const uint32_t table = REX_LOAD_U32(ctx.r3.u32);
        REXLOG_INFO("[Majin thread USA] LR={:08X} object={:08X} table={:08X}",
                    uint32_t(ctx.lr), ctx.r3.u32, table);
    }
    __imp__sub_822E9188(ctx, base);
}

REX_EXTERN(sub_826D3BB0) {
    static std::atomic<unsigned> calls{0};
    const unsigned call = calls.fetch_add(1, std::memory_order_relaxed);
    if (call < 8) {
        REXLOG_INFO("[Majin trace USA] 826D3BB0 entry: LR={:08X} r3={:08X} r1={:08X}",
                    uint32_t(ctx.lr), ctx.r3.u32, ctx.r1.u32);
    }
    __imp__sub_826D3BB0(ctx, base);
    if (call < 8) {
        REXLOG_INFO("[Majin trace USA] 826D3BB0 result: r3={:08X}", ctx.r3.u32);
    }
}
