#ifndef GTOS_QUALIFICATION_GIF_CODEC_H
#define GTOS_QUALIFICATION_GIF_CODEC_H
#include <stddef.h>
// Caller owns 8-byte-aligned decoder storage and BGRA output. No allocation,
// global state, display service, filesystem, threading, or Linux ABI is used.
size_t gtos_gif_context_size(void);
// Returns 0 only after a complete first frame. Nonzero means bad arguments,
// initialization, input, dimensions/capacity, pixel binding, or decode failure.
// Input and output must not overlap storage or each other. Decode failure can
// leave partial output; width/height are only published after success.
int gtos_gif_first_frame(void* storage, size_t storage_size,
                         const unsigned char* input, size_t input_size,
                         unsigned char* output, size_t output_size,
                         unsigned int* width, unsigned int* height);
#endif
