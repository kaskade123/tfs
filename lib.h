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
#include <jobQueueLib.h>

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

/* lib base function called by main module */
extern void lib_init(void);
extern void lib_delayed_init(void);
extern void lib_last_stage_init(void);
extern void lib_start(void);
extern void lib_show(char * buf);

/* Module register */
extern void moduleReg(void (*start)(void), void (*show)(char *));

/* Helper functions */
extern int ethdev_get(const char * name);
extern int canhcbdev_get(void);
extern UINT8 addr_get(void);
extern int light_get(char * color);
extern int timer_get(void);
extern int iondev_get(void);
extern void rand_range(UINT8 * ptr, UINT32 size);
extern int is_cpu(void);
extern int is_hmi(void);
extern STATUS queue_add(QJOB * pJob);
void calc_fletcher32(unsigned char *data, unsigned n_bytes, unsigned * cksum);

/* Module declare */
#define MODULE_DECLARE(name)	\
	extern void name##_register(void);

/* Module Register Function */
#define MODULE_REGISTER(name)	\
		void name##_register(void) { moduleReg(name##_start, name##_show);}
