// Majistic Recomp - Unified Diagnostic & Save Tracing Hooks
#include "generated/default/majin_pch.h"
#include "edition_offsets.h"
#include <rex/logging.h>
#include <atomic>

#define MAJIN_CONCAT_IMPL(a, b) a##b
#define MAJIN_CONCAT(a, b) MAJIN_CONCAT_IMPL(a, b)

#if defined(MAJIN_ENABLE_DIAGNOSTICS)
#define MAJIN_TRACE_SAVE(address, operation) \
    REX_EXTERN(MAJIN_CONCAT(__imp__sub_, address)); \
    REX_EXTERN(MAJIN_CONCAT(sub_, address)) { \
        static std::atomic<unsigned> calls{0}; \
        const unsigned id = calls.fetch_add(1, std::memory_order_relaxed); \
        REXLOG_INFO("[Majin save " MAJIN_EDITION_NAME "] " operation " id={} enter LR={:08X} r3={:08X} r4={:08X} r5={:08X} r6={:08X} r7={:08X} r8={:08X} r9={:08X} r10={:08X}", \
                    id, uint32_t(ctx.lr), ctx.r3.u32, ctx.r4.u32, ctx.r5.u32, \
                    ctx.r6.u32, ctx.r7.u32, ctx.r8.u32, ctx.r9.u32, ctx.r10.u32); \
        MAJIN_CONCAT(__imp__sub_, address)(ctx, base); \
        REXLOG_INFO("[Majin save " MAJIN_EDITION_NAME "] " operation " id={} returned={:08X}", id, ctx.r3.u32); \
    }

MAJIN_TRACE_SAVE(MAJIN_ADDR_CONTENT_CREATE_EX, "ContentCreateEx")
MAJIN_TRACE_SAVE(MAJIN_ADDR_CONTENT_CLOSE, "ContentClose")
MAJIN_TRACE_SAVE(MAJIN_ADDR_CONTENT_SET_THUMBNAIL, "ContentSetThumbnail")
MAJIN_TRACE_SAVE(MAJIN_ADDR_GET_OVERLAPPED_RESULT, "GetOverlappedResult")
#undef MAJIN_TRACE_SAVE
#endif

// WriteFile & GetLastError
REX_EXTERN(MAJIN_IMP_WRITE_FILE);
REX_EXTERN(MAJIN_IMP_GET_LAST_ERROR);

REX_EXTERN(MAJIN_FN_WRITE_FILE) {
    const uint32_t caller = uint32_t(ctx.lr);
    const uint32_t requested = ctx.r5.u32;
    const uint32_t bytesOut = ctx.r6.u32;
    const uint32_t overlapped = ctx.r7.u32;
    MAJIN_IMP_WRITE_FILE(ctx, base);
    // Caller in save state machine is below 0x821C0000 (LR is 0x821B9DF4 in both editions)
    if (caller == 0x821B9DF4 && ctx.r3.u32 == 0 && overlapped && requested) {
        auto probe = ctx;
        MAJIN_IMP_GET_LAST_ERROR(probe, base);
        const uint32_t status = REX_LOAD_U32(overlapped);
        const uint32_t written = REX_LOAD_U32(overlapped + 4);
        const bool completed = probe.r3.u32 == 997 && status == 0 && written == requested;
#if defined(MAJIN_ENABLE_DIAGNOSTICS)
        REXLOG_INFO("[Majin save " MAJIN_EDITION_NAME "] write completion lastError={} status={:08X} bytes={}/{} immediate={}",
                    probe.r3.u32, status, written, requested, completed);
#endif
        if (completed) {
            if (bytesOut) REX_STORE_U32(bytesOut, written);
            ctx.r3.u64 = 1;
        }
    }
}

#if defined(MAJIN_ENABLE_DIAGNOSTICS)
REX_EXTERN(MAJIN_FN_GET_LAST_ERROR) {
    const uint32_t caller = uint32_t(ctx.lr);
    MAJIN_IMP_GET_LAST_ERROR(ctx, base);
    if (caller >= 0x821B9000 && caller < 0x821BB000) {
        REXLOG_INFO("[Majin save " MAJIN_EDITION_NAME "] GetLastError LR={:08X} result={:08X}", caller, ctx.r3.u32);
    }
}

// Diagnostic trace hooks:
REX_EXTERN(MAJIN_IMP_SAVE_FLOW_A);
REX_EXTERN(MAJIN_IMP_SAVE_FLOW_B);
REX_EXTERN(MAJIN_IMP_SAVE_FLOW_C);

REX_EXTERN(MAJIN_FN_SAVE_FLOW_A) {
    static std::atomic<unsigned> calls{0};
    const bool trace = calls.fetch_add(1, std::memory_order_relaxed) < 4;
    if (trace) {
        REXLOG_INFO("[Majin loading " MAJIN_EDITION_NAME "] flow_a entry LR={:08X} r3={:08X}",
                    uint32_t(ctx.lr), ctx.r3.u32);
    }
    MAJIN_IMP_SAVE_FLOW_A(ctx, base);
    if (trace) {
        REXLOG_INFO("[Majin loading " MAJIN_EDITION_NAME "] flow_a returned r3={:08X}", ctx.r3.u32);
    }
}

REX_EXTERN(MAJIN_FN_SAVE_FLOW_B) {
    static std::atomic<unsigned> calls{0};
    if (calls.fetch_add(1, std::memory_order_relaxed) < 4) {
        const uint32_t table = REX_LOAD_U32(ctx.r3.u32);
        REXLOG_INFO("[Majin thread " MAJIN_EDITION_NAME "] LR={:08X} object={:08X} table={:08X}",
                    uint32_t(ctx.lr), ctx.r3.u32, table);
    }
    MAJIN_IMP_SAVE_FLOW_B(ctx, base);
}

REX_EXTERN(MAJIN_FN_SAVE_FLOW_C) {
    static std::atomic<unsigned> calls{0};
    const unsigned call = calls.fetch_add(1, std::memory_order_relaxed);
    if (call < 8) {
        REXLOG_INFO("[Majin trace " MAJIN_EDITION_NAME "] flow_c entry: LR={:08X} r3={:08X} r1={:08X}",
                    uint32_t(ctx.lr), ctx.r3.u32, ctx.r1.u32);
    }
    MAJIN_IMP_SAVE_FLOW_C(ctx, base);
    if (call < 8) {
        REXLOG_INFO("[Majin trace " MAJIN_EDITION_NAME "] flow_c result: r3={:08X}", ctx.r3.u32);
    }
}
#endif
