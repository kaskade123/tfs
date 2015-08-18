#include <vxWorks.h>
#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <assert.h>
#include <semLib.h>
#include <taskLib.h>
#include <lstLib.h>
#include <sysLib.h>
#include <errnoLib.h>
#include <isrDeferLib.h>

#include <arch/ppc/vxPpcLib.h>

#include "sacDev.h"

typedef struct hsb_recv_pkt
{
	UINT8	dstMac[6];
	UINT8 	srcMac[6];
	UINT32 	: 17;
	UINT32	SRC : 4;
	UINT32	DLC : 11;
}__attribute((packed)) HSB_RECV_HEADER;

typedef struct hsb_send_pkt
{
	UINT8	dstMac[6];
	UINT8	srcMac[6];
	UINT32	: 3;
	UINT32 	PRI : 2;
	UINT32	DST : 16;
	UINT32	DLC : 11;
}__attribute((packed)) HSB_SEND_HEADER;

extern void lib_init(void);
extern int ethdev_get(const char * name);
extern int canhcbdev_get(void);
extern UINT8 addr_get(void);
extern int light_get(void);
extern int timer_get(void);
extern void rand_range(UINT8 * ptr, UINT32 size);

#define BUS_DECLARE(name)	\
	extern void name##_sender_start(void);	\
	extern void name##_show(void);			\
	extern void name##_sender_suspend(void);\
	extern void name##_sender_resume(void);
