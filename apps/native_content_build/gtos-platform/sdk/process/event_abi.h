#ifndef GTOS_PROCESS_EVENT_ABI_H
#define GTOS_PROCESS_EVENT_ABI_H
// Native IA32: EAX=operation, EBX=request VA, ECX=exact bytes; signed EAX result.
// Only EAX changes. Handles belong to the calling process root, including workers.
#define GTOS_EVENT_ABI_VERSION 1U
#define GTOS_SYS_EVENT_CREATE 0x4741U
#define GTOS_SYS_EVENT_DUP 0x4742U
#define GTOS_SYS_EVENT_CLOSE 0x4743U
#define GTOS_SYS_EVENT_SET 0x4744U
#define GTOS_SYS_EVENT_RESET 0x4745U
#define GTOS_SYS_EVENT_WAIT 0x4746U
#define GTOS_EVENT_MANUAL_RESET 1U
#define GTOS_EVENT_INITIAL_SIGNAL 2U
#define GTOS_EVENT_RIGHT_WAIT 1U
#define GTOS_EVENT_RIGHT_SET 2U
#define GTOS_EVENT_RIGHT_RESET 4U
#define GTOS_EVENT_RIGHT_DUP 8U
#define GTOS_EVENT_RIGHT_ALL 15U
#define GTOS_EVENT_WAIT_LIMIT 16U
#define GTOS_EVENT_TIMEOUT_LIMIT 0x7fffffffU
#define GTOS_EVENT_ERR_BAD_HANDLE (-9)
#define GTOS_EVENT_ERR_PENDING (-11)
#define GTOS_EVENT_ERR_PERMISSION (-13)
#define GTOS_EVENT_ERR_BAD_ADDRESS (-14)
#define GTOS_EVENT_ERR_INVALID (-22)
#define GTOS_EVENT_ERR_LIMIT (-24)
#define GTOS_EVENT_ERR_UNAVAILABLE (-38)
#define GTOS_EVENT_ERR_OVERFLOW (-75)
#define GTOS_EVENT_ERR_TIMEOUT (-110)
typedef struct GtosEventCreateRequest { unsigned version,flags,result_va; } GtosEventCreateRequest;
typedef struct GtosEventDupRequest { unsigned version,handle,rights; } GtosEventDupRequest;
typedef struct GtosEventControlRequest { unsigned version,handle; } GtosEventControlRequest;
typedef struct GtosEventWaitRequest { unsigned version,handles_va,count,timeout_ticks; } GtosEventWaitRequest;
// CREATE writes one positive handle and returns zero. DUP returns a positive
// alias; rights=0 retains rights, otherwise only a nonempty subset is allowed.
// WAIT snapshots 1..16 handles and validates ALL before consuming any signal.
// It returns the lowest ready array index, or a negative error. timeout_ticks
// uses delivered scheduler PIT ticks, not milliseconds; zero never sleeps.
// Manual signals persist until RESET; automatic signals coalesce and exactly
// one successful wait consumes them. CLOSE cancels waits on that alias even
// when another alias still owns the event. No user-supplied cross-owner grant.
// Invalid requests do not modify handles, signal state or output. CREATE's
// descriptor and output may overlap after a complete descriptor snapshot.
#if defined(__cplusplus)
static_assert(sizeof(unsigned)==4,"Event ABI unsigned");
static_assert(sizeof(GtosEventCreateRequest)==12,"Event create ABI");
static_assert(sizeof(GtosEventDupRequest)==12,"Event dup ABI");
static_assert(sizeof(GtosEventControlRequest)==8,"Event control ABI");
static_assert(sizeof(GtosEventWaitRequest)==16,"Event wait ABI");
#elif defined(__STDC_VERSION__) && __STDC_VERSION__ >= 201112L
_Static_assert(sizeof(unsigned)==4,"Event ABI unsigned");
_Static_assert(sizeof(GtosEventWaitRequest)==16,"Event wait ABI");
#endif
#endif
