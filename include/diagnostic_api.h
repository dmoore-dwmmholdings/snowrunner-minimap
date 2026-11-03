#ifndef SRMM_DIAGNOSTIC_API_H
#define SRMM_DIAGNOSTIC_API_H
#include <stdint.h>
#if !defined(_WIN32) || (!defined(_M_X64) && !defined(__x86_64__))
#error The diagnostic ABI is currently restricted to x64 Windows.
#endif

/* Original host-only ABI. All pointers must refer to valid caller-owned memory.
 * x64 Windows, default packing, single-threaded. No C++ objects cross the ABI.
 * Destroy sessions before unloading the DLL; never reuse a destroyed handle.
 */
#define SRMM_DIAGNOSTIC_ABI 1u
#define SRMM_CAP_SYNTHETIC_ONLY 1u
#if defined(_WIN32)
#define SRMM_CALL __cdecl
#else
#define SRMM_CALL
#endif

#ifdef __cplusplus
extern "C" {
#endif
typedef void* SrmmSession;
enum SrmmResult {
    SRMM_OK = 0, SRMM_BAD_ARGUMENT = 1, SRMM_ABI_MISMATCH = 2,
    SRMM_INTERNAL_ERROR = 3, SRMM_SESSION_POISONED = 4
};
enum SrmmSource { SRMM_SYNTHETIC = 0, SRMM_NATIVE_CANDIDATE = 1 };
enum SrmmReadState {
    SRMM_ACTIVE = 0, SRMM_MENU = 1, SRMM_GARAGE = 2,
    SRMM_LOADING = 3, SRMM_DISCONNECTED = 4, SRMM_READ_ERROR = 5
};
enum SrmmFrameState {
    SRMM_READY = 0, SRMM_NO_POSE = 1, SRMM_STALE = 2, SRMM_CLOCK_ERROR = 3,
    SRMM_INVALID_CONTEXT = 4, SRMM_UNSUPPORTED_SOURCE = 5, SRMM_WRONG_BUILD = 6,
    SRMM_WRONG_CONTEXT = 7, SRMM_OUT_OF_ORDER = 8, SRMM_INVALID_POSE = 9,
    SRMM_FRAME_MENU = 10, SRMM_FRAME_GARAGE = 11, SRMM_FRAME_LOADING = 12,
    SRMM_FRAME_DISCONNECTED = 13, SRMM_FRAME_READ_ERROR = 14
};
typedef struct SrmmSample {
    uint32_t size;
    uint32_t source;
    uint32_t has_event; /* 0: poll with no new sample; 1: submit event. */
    uint32_t state;
    uint64_t sequence;
    double sampled_ms;
    double now_ms;
    double east;
    double north;
    double heading_radians; /* Clockwise from synthetic north. */
} SrmmSample;
typedef struct SrmmFrame {
    uint32_t size;
    uint32_t state;
    uint32_t has_geometry;
    uint32_t reserved;
    double image_center_x;
    double image_center_y;
    double player_x;
    double player_y;
    double view_heading;
    double marker_heading;
} SrmmFrame;
typedef struct SrmmApi {
    uint32_t size;
    uint32_t version;
    uint32_t capabilities;
    uint32_t reserved;
    uint32_t (SRMM_CALL *create)(SrmmSession* out);
    uint32_t (SRMM_CALL *update)(SrmmSession session, const SrmmSample* sample, SrmmFrame* out);
    void (SRMM_CALL *destroy)(SrmmSession session);
} SrmmApi;
typedef uint32_t (SRMM_CALL *SrmmGetApi)(uint32_t version, uint32_t size, SrmmApi* out);
/* Resolve "srmm_get_api" dynamically. Only exact v1 structure sizes accepted.
 * update errors hide valid output and poison a valid session until destruction.
 * Output with an invalid size is not written; callers must not render it.
 */
#ifdef __cplusplus
}
static_assert(sizeof(void*) == 8, "Diagnostic ABI requires x64.");
static_assert(sizeof(SrmmSample) == 64, "Unexpected sample layout.");
static_assert(sizeof(SrmmFrame) == 64, "Unexpected frame layout.");
static_assert(sizeof(SrmmApi) == 40, "Unexpected API layout.");
#endif
#endif
