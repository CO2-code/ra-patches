#ifndef RA_REPLAY_NATIVE_H
#define RA_REPLAY_NATIVE_H
#include <stddef.h>
int NativeReplay_RecordChat(const char *,const char *,unsigned,unsigned);
void NativeReplay_ChatFrame(unsigned);
void Replay_Chat_Display(const char *,const char *,unsigned);
/* Private CRT replacements: the game uses Watcom's register calling convention. */
void *nr_memcpy(void *, const void *, size_t);
void *nr_memmove(void *, const void *, size_t);
void *nr_memset(void *, int, size_t);
int nr_memcmp(const void *, const void *, size_t);
size_t nr_strlen(const char *);
#endif
