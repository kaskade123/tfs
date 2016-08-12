#include "lib.h"

typedef struct sv_status
{
	int svFd;
	int ethFd;
	int timerFd;
	BOOL svInited;
	SEM_ID muxSem;
} SV_STATUS_S;

static SV_STATUS_S * pStatus = NULL;

#define PKT_BUF_SIZE	2048		/* SV packet buffer limit */
#define SV_TIMER_FREQ	(96 * 50)   /* 96 samples per cycle */

#define SV_POLLING_TASK_PRIORITY	40

static void eth_srcmac_fill(INT32 hdr, UINT8 * pkt)
{
    UINT32 mac32[6];
    char strMAC[20] = {0};
    int i;

    assert (EthernetMACGet(hdr, strMAC) == 0);
    sscanf(strMAC, "%x.%x.%x.%x.%x.%x", mac32, mac32 + 1, mac32 + 2, mac32 + 3,
            mac32 + 4, mac32 + 5);

    for (i = 0; i < 6; i++)
        pkt[6+i] = mac32[i];
}

static BOOL sv_recv_hook(void * pDev, UINT8 *buf, UINT32 bufLen)
{
    /* Update MAC */
    eth_srcmac_fill(pStatus->ethFd, buf);
    /* Send out */
    assert(EthernetSendPkt(pStatus->ethFd, buf, bufLen) == 0);

	return TRUE;
}

static int polling_task(void)
{
	assert(pStatus->svInited == TRUE);
	while(1)
	{
		/* Wait for send is done */
		assert(semTake(pStatus->muxSem, WAIT_FOREVER) == OK);
		
		/* Receive all packets pending */
		while (EthernetRecvPoll(pStatus->svFd, NULL) == -EAGAIN);
	}
}

static void sv_init(void)
{
	/* Only init once */
	if (pStatus && pStatus->svInited)
		return;
	
	if (pStatus == NULL)
	{
		pStatus = malloc(sizeof(*pStatus));
		assert(pStatus);
	}
	
	memset(pStatus, 0, sizeof(*pStatus));
	
    /* Request handler */
	pStatus->svFd = ethdev_get("sv");
	assert (pStatus->svFd >= 0);
	
	pStatus->ethFd = ethdev_get("backplane");
	assert (pStatus->ethFd >= 0);
	
	/* Timer Request */
	pStatus->timerFd = timer_get();
	assert(pStatus->timerFd >= 0);
	
	/* Initialize semaphore */
	pStatus->muxSem = semBCreate(SEM_Q_FIFO, SEM_EMPTY);
	assert(pStatus->muxSem != NULL);
	
	/* Drop all the packets received */
	assert(EthernetPktDrop(pStatus->svFd, 512) >= 0);
	
	/* Hook the packet copier */
	assert(EthernetHookDisable(pStatus->svFd) == 0);
	assert(EthernetRecvHook(pStatus->svFd, sv_recv_hook) == 0);
	assert(EthernetHookEnable(pStatus->svFd) == 0);
	
	/* Init done */
	pStatus->svInited = TRUE;
	
	/* Start polling task */
	taskSpawn("tSVPoll", SV_POLLING_TASK_PRIORITY, 0, 0x40000, polling_task, 0,0,0,0,0,0,0,0,0,0);
}

static void sv_timer_hook(int arg)
{
	assert(semGive(pStatus->muxSem) == OK);
}

static void sv_start(void)
{
	/* Basic SV initialize */
	sv_init();
	
	/* Initialize a timer */
	assert(TimerDisable(pStatus->timerFd) == 0);
	assert(TimerFreqSet(pStatus->timerFd, SV_TIMER_FREQ) == 0);
	assert(TimerISRSet(pStatus->timerFd,sv_timer_hook, 0) == 0);
	assert(TimerEnable(pStatus->timerFd) == 0);
}

static void sv_show(char * buf)
{
    return;
}

MODULE_REGISTER(sv);
