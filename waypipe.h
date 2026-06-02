#ifndef WAYPIPE_H
#define WAYPIPE_H
#ifdef __cplusplus
extern "C" { 
#endif
#include <stdint.h>

typedef struct Waypipe Waypipe;

Waypipe *waypipe_init(void);
int waypipe_start(Waypipe *ctx, unsigned int timeout_ms, int persistent);
int waypipe_get_frame(Waypipe *ctx, uint8_t **out_rgba, int *out_w, int *out_h, int *out_stride);
const char *waypipe_get_restore_token(Waypipe *ctx);
void waypipe_free(void *ptr);
void waypipe_exit(Waypipe *ctx);

#ifdef __cplusplus
}
#endif
#endif