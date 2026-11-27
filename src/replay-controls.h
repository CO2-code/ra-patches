#ifndef RA_REPLAY_CONTROLS_H
#define RA_REPLAY_CONTROLS_H

#include <stdint.h>

extern int pb_paused;
extern int pb_seeking;
extern int pb_restoring_snap;
extern int pb_saving_snap;

void ReplayControls_OnInit(void);
void ReplayControls_OnClose(void);
void pb_on_main_loop_top(void);
int  pb_on_main_loop_timer(void);
int  pb_on_sync_delay_end(void);
void pb_on_render(void);
int  pb_on_gscreen_input(short *key, int *mouse_x, int *mouse_y);
int  pb_on_wndproc(void *hwnd, unsigned msg, unsigned wparam, unsigned lparam);

#endif
