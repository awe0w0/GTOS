/* C11 wire check: the same header must compile and assert as C, not only C++. */
#include <process/window_abi.h>
#include <stdio.h>

int main(void) {
    printf("c11 create=%u submit=%u control=%u info=%u read=%u response=%u event=%u\n",
           (unsigned)sizeof(GtosWindowCreateRequest),
           (unsigned)sizeof(GtosWindowSubmitRequest),
           (unsigned)sizeof(GtosWindowControlRequest),
           (unsigned)sizeof(GtosWindowInfoRequest),
           (unsigned)sizeof(GtosInputReadRequest),
           (unsigned)sizeof(GtosWindowInfoResponse),
           (unsigned)sizeof(GtosInputEvent));
    if (sizeof(GtosWindowInfoResponse) != GTOS_WINDOW_INFO_RESPONSE_BYTES) return 1;
    if (sizeof(GtosInputEvent) != GTOS_INPUT_EVENT_BYTES) return 1;
    printf("NATIVE WINDOW C11 WIRE PASS\n");
    return 0;
}
