#include "lib.h"

static int hsbFd;
static int LightFd;
static int timerFd;
static BOOL hsbInited = FALSE;
static UINT8 * pktBuf;
static UINT64 pktSend;
static UINT64 pktRecv;
static SEM_ID muxSem;

#define PKT_BUF_SIZE	2048					/* HSB packet buffer limit */
#define HSB_BW_LIMIT	50000000				/* BW limited to 50Mbps */
#define HSB_SFP_COUNT	25						/* SFP info count */
#define HSB_PKT_LEN		(20+4+24*HSB_SFP_COUNT)	/* HSB Packet Length */
#define HSB_TIMER_FREQ	(HSB_BW_LIMIT / 8 / HSB_PKT_LEN)

#define HSB_POLLING_TASK_PRIORITY	40

#ifdef DISPLAY
static void hsb_rpkt_display(void * arg)
{
	HSB_RECV_HEADER * pHdr = arg;
	UINT8 * buf;
	int i;
	
	printf("HSB Packet Received : SRC = %d, DLC = %d, DATA = ",
			pHdr->SRC, pHdr->DLC);
	
	buf = (UINT8 *)arg + sizeof(*pHdr);
	
    for (i = 0; i < pHdr->DLC; i++)
        printf("%02X ", buf[i]);
    printf("\n");
}

static void hsb_spkt_display(void * arg)
{
	HSB_SEND_HEADER * pHdr = arg;
	UINT8 * buf;
    int i;
    
    printf("HSB Packet Sent : DST = 0x%X, DLC = %d, DATA = ",
            pHdr->DST, pHdr->DLC);
    
    buf = (UINT8 *)arg + sizeof(*pHdr);

    for (i = 0; i < pHdr->DLC; i++)
        printf("%02X ", buf[i]);
    printf("\n");
}
#else
#define hsb_rpkt_display(arg)
#define hsb_spkt_display(arg)
#endif

static UINT32 pkt_len(void * arg)
{
	HSB_SEND_HEADER * pHdr = arg;
	
	return pHdr->DLC + sizeof(*pHdr);
}

static UINT8 * hsb_send_prepare(UINT8 pri, UINT8 dst, UINT8 dlc)
{
	HSB_SEND_HEADER * pHdr;
	
	memset(pktBuf, 0, PKT_BUF_SIZE);
	
	pHdr = (HSB_SEND_HEADER *)pktBuf;
	
    pHdr->dstMac[5] = 2;
    pHdr->srcMac[5] = 1;
	
	pHdr->PRI = pri;
	if (dst)
		pHdr->DST = 0x0001 << dst;
	else
		pHdr->DST = 0xFFFF;
	pHdr->DLC = dlc;
	
	return pktBuf + sizeof(*pHdr);
}

static void hsb_send_pkt(void)
{
	hsb_spkt_display(pktBuf);

    assert(EthernetSendPkt(hsbFd, pktBuf, pkt_len(pktBuf)) == 0);
    
    pktSend++;
    
    /* Due to packet loop time, there is always one packet missing */
    assert(semGive(muxSem) == OK);
}

static BOOL hsb_recv_hook(void * pDev, UINT8 *buf, UINT32 bufLen)
{
#if 1
	HSB_RECV_HEADER * pHdr = (HSB_RECV_HEADER *)buf;
	
	/* Only acknowledge the packets sent from ourselves */
	if (pHdr->SRC == addr_get() && buf[sizeof(*pHdr)] == 0x51)
	{
		++pktRecv;
	}
#else
	++pktRecv;
#endif
	
	return TRUE;
}

static void hsb_cfg_ends(UINT8 addr)
{
	UINT8 * dp;
	
	dp = hsb_send_prepare(3, addr, 4);
	assert(dp != NULL);
	
    dp[0] = 0x02;
    dp[1] = 0x00;
    dp[2] = 0x00;
    dp[3] = 0x00;

    hsb_send_pkt();
}

static int polling_task(void)
{
	assert(hsbInited == TRUE);
	while(1)
	{
		/* Wait for send is done */
		assert(semTake(muxSem, WAIT_FOREVER) == OK);
		
		/* Receive all packets pending */
		while (EthernetRecvPoll(hsbFd) != -EAGAIN);
		
		/* Trigger LED blink */
		if (!(pktSend % HSB_TIMER_FREQ))
			LightOn(LightFd);
		else if (!(pktSend % (HSB_TIMER_FREQ*2+1)))
			LightOff(LightFd);
	}
}

static void hsb_init(void)
{
	/* Only init once */
	if (hsbInited == TRUE)
		return;
	
    /* Request handler */
	hsbFd = ethdev_get("hsb");
	assert (hsbFd >= 0);
	
	/* Light Request */
	LightFd = light_get();
	assert(LightFd >= 0);
	
	/* Default to OFF */
	LightOff(LightFd);
	
	/* Timer Request */
	timerFd = timer_get();
	assert(timerFd >= 0);
	
	/* Initialize global buffer */
	pktBuf = malloc(PKT_BUF_SIZE);
	assert(pktBuf != NULL);
	
	/* Initialize semaphore */
	muxSem = semBCreate(SEM_Q_FIFO, SEM_EMPTY);
	assert(muxSem != NULL);
	
	/* Make HSB quiet */
	hsb_cfg_ends(addr_get());

	/* Drop all the packets received */
	assert(EthernetPktDrop(hsbFd, 512) >= 0);
	
	/* Hook the packet copier */
	assert(EthernetHookDisable(hsbFd) == 0);
	assert(EthernetRecvHook(hsbFd, hsb_recv_hook) == 0);
	assert(EthernetHookEnable(hsbFd) == 0);
	
	/* Clear send and recv counter */
	pktSend = 0;
	pktRecv = 0;
	
	/* Init done */
	hsbInited = TRUE;
	
	/* Start polling task */
	taskSpawn("tHSBPoll", HSB_POLLING_TASK_PRIORITY, 0, 0x40000, polling_task, 0,0,0,0,0,0,0,0,0,0);
}

static void hsb_send(UINT32 num)
{
	UINT8 * dp;
	
	assert(hsbInited == TRUE);
	
	dp = hsb_send_prepare(3, addr_get(), 4 + 24 * num);
	assert(dp != NULL);
	dp[0] = 0x51;	    		/* SFP */
	dp[1] = rand();    			/* idx */
	dp[2] = rand();    			/* idx */
	dp[3] = num;				/* count */
	rand_range(dp+4, 24 * num);	/* status */

	hsb_send_pkt();
}

static void hsb_timer_hook(int arg)
{
	hsb_send(arg);
}

void hsb_sender_start(void)
{
	/* Basic HSB initialize */
	hsb_init();
	
	/* Initialize a timer */
	assert(TimerDisable(timerFd) == 0);
	assert(TimerFreqSet(timerFd, HSB_TIMER_FREQ) == 0);
	assert(TimerISRSet(timerFd,hsb_timer_hook, HSB_SFP_COUNT) == 0);
	assert(TimerEnable(timerFd) == 0);
}

static void hsb_sender_suspend(void)
{
	/* check if HSB inited */
	assert(hsbInited);
	
	/* Make sure all packets sent is received */
	assert(TimerDisable(timerFd) == 0);
	taskDelay(1);
	assert(semGive(muxSem) == OK);
}

static void hsb_sender_resume(void)
{
	/* Re-enable timer */
	assert(TimerEnable(timerFd) == 0);
}

void hsb_show(char * buf)
{
	hsb_sender_suspend();
	
	sprintf(buf, "\n"
			"*********** HSB ***********\n"
			"Total Send Pkts        : %llu\n"
			"Total Recv Pkts        : %llu\n"
			"Total Missing Pkts     : %llu\n",
			pktSend, pktRecv, pktSend - pktRecv);
	
	hsb_sender_resume();
}

