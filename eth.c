#include "lib.h"

#define ETH_DEV_COUNT	4
#define ETH_DEV_PREFIX	"eth"
#define ETH_BUFFER_LEN	1600

#define ETH_BW_LIMIT	10000000	/* BW limited to 10Mbps */
#define ETH_PKT_LEN		1500		/* Packet Length */
#define ETH_TIMER_FREQ	(ETH_BW_LIMIT / 8 / ETH_PKT_LEN)

typedef struct eth_status
{
	BOOL 	ethInited;
	INT32 	hdr[ETH_DEV_COUNT];		/* Ethernet device handler */
	UINT8 * pkt[ETH_DEV_COUNT]; 	/* Ethernet packet buffer */
	UINT32 	pktCksum[ETH_DEV_COUNT];/* Ethernet packet cksum */
	UINT32 	pktSent[ETH_DEV_COUNT]; /* Ethernet packet sent */
	UINT32 	pktFail[ETH_DEV_COUNT]; /* Ethernet packet send fail */
	UINT32 	pktRecv[ETH_DEV_COUNT];	/* Ethernet packet received */
	INT32 	timerFd;				/* Timer Handler */
	SEM_ID 	muxSem;					/* Semaphore */
	QJOB 	job;					/* job queue */
	UINT32 	in_process;
}ETH_STATUS_S;

static ETH_STATUS_S * pStatus = NULL;

static BOOL eth_counting_hook(void * pDev, UINT8 *pBuf, UINT32 bufLen)
{
	ETHERNET_DEV_S * p = pDev;
	UINT32 cksum, idx;
	
#if 0
	calc_fletcher32(pBuf, bufLen, &cksum);
#endif
	
	/* Hook for eth1 - eth4 */
	if ((strlen(p->name) == 4) && 
			(strncmp(p->name, ETH_DEV_PREFIX, strlen(ETH_DEV_PREFIX)) == 0))
	{
	    idx = p->name[strlen(ETH_DEV_PREFIX)] - '1';
#if 0
	    switch(idx)
	    {
	    /*
	     * eth1 <-> eth2
	     * eth3 <-> eth4
	     */
	    case 0:
	    case 2:
	        if (cksum == pStatus->pktCksum[idx + 1])
	            pStatus->pktRecv[idx] ++;
	        break;
	    case 1:
	    case 3:
	        if (cksum == pStatus->pktCksum[idx - 1])
	            pStatus->pktRecv[idx] ++;
	        break;
	    }
#else
        pStatus->pktRecv[idx] ++;
#endif
	}
    return TRUE;
}

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

static int eth_send_random(INT32 hdr, UINT8 * pkt, UINT32 pkt_len, UINT32 * cksum)
{
    int i;
    
    assert (pStatus);
    assert (pStatus->ethInited);
    assert (pkt_len > 14);

    /* broadcast */
    memset(pkt, 0xFF, 6);
    /* fillup src mac */
    eth_srcmac_fill(hdr, pkt);
    /* type */
    pkt[12] = 0x08;
    pkt[13] = 0x00;
    /* fillup random stuff */
    for (i = 14; i < pkt_len; i++)
        pkt[i] = rand();
    
    calc_fletcher32(pkt, pkt_len, cksum);

    return EthernetSendPkt(hdr, pkt, pkt_len);
}

static void eth_send_task(void * arg)
{
	int i;
	/* Send out one packet for each port */
	for (i = 0; i < ETH_DEV_COUNT; i++)
	{
		if (eth_send_random(pStatus->hdr[i], pStatus->pkt[i], ETH_PKT_LEN, &pStatus->pktCksum[i]))
			pStatus->pktFail[i]++;
		else
			pStatus->pktSent[i]++;
	}
	
	assert(semGive(pStatus->muxSem) == OK);
	
	pStatus->in_process = 0;
}

static int eth_poll_at_least(int idx, int num)
{
    UINT32 pktRecved = 0;
    UINT32 cnt = 0;
    int status;
    do
    {
        UINT32 pktLimit = 32;
        status = EthernetRecvPoll(pStatus->hdr[idx], &pktLimit);
        if (status == 0 || status == -EAGAIN)
            pktRecved += pktLimit;
        cnt ++;
    }while(pktRecved < num && cnt < 1000);
    
    return pktRecved;
}

static int eth_polling_task(void)
{
	int i;
	
	assert(pStatus->ethInited == TRUE);
	
	while(1)
	{
		/* Wait for send is done */
		assert(semTake(pStatus->muxSem, WAIT_FOREVER) == OK);
		
		/* Receive all packets pending */
		for (i = 0; i < ETH_DEV_COUNT; i++)
		    eth_poll_at_least(i, 1);
	}
}

static void eth_init(void)
{
	int i;
	
	if (pStatus && pStatus->ethInited)
		return;
	
	if (pStatus == NULL)
	{
		pStatus = malloc(sizeof(*pStatus));
		assert(pStatus);
	}
	
	memset(pStatus, 0, sizeof(*pStatus));
	
	pStatus->ethInited = FALSE;
	pStatus->timerFd = timer_get();
	assert(pStatus->timerFd >= 0);
	
	pStatus->muxSem = semBCreate(SEM_Q_FIFO, SEM_EMPTY);
	assert(pStatus->muxSem != NULL);

	for (i = 0; i < ETH_DEV_COUNT; i++)
	{
		/* Get eth device name */
		char ethName[5];
		sprintf(ethName, "eth%d", i+1);
		
		/* Request handler, if failed, this is CPU board */
		pStatus->hdr[i] = ethdev_get(ethName);
		if (pStatus->hdr[i] < 0)
			return;
		
		/* Initialize packet buffer */
		pStatus->pkt[i] = malloc(ETH_BUFFER_LEN);
		assert(pStatus->pkt[i] != NULL);
		
		/* Initialize packet counter */
		pStatus->pktSent[i] = 0;
		pStatus->pktRecv[i] = 0;
		pStatus->pktFail[i] = 0;
		
		/* Drop all current packets */
		assert(EthernetPktDrop(pStatus->hdr[i], 512) >= 0);
		
		/* Hook callbacks */
		assert(EthernetHookDisable(pStatus->hdr[i]) == 0);
		assert(EthernetRecvHook(pStatus->hdr[i], eth_counting_hook) == 0);
		assert(EthernetHookEnable(pStatus->hdr[i]) == 0);
	}
	
	pStatus->ethInited = TRUE;
	
	assert(taskSpawn("tEthPoll", 40, VX_SPE_TASK, 0x4000, eth_polling_task,
			0,0,0,0,0,0,0,0,0,0) != TASK_ID_ERROR);
}

static void eth_timer_hook(int arg)
{
	if (pStatus->in_process == 0)
	{
		pStatus->in_process = 1;
		pStatus->job.func = eth_send_task;
		QJOB_SET_PRI(&pStatus->job, 20);
		queue_add(&pStatus->job);
	}
}

static void eth_start(void)
{
	eth_init();
	
	if (pStatus->ethInited)
	{
		/* Initialize a timer */
		assert(TimerDisable(pStatus->timerFd) == 0);
		assert(TimerFreqSet(pStatus->timerFd, ETH_TIMER_FREQ) == 0);
		assert(TimerISRSet(pStatus->timerFd, eth_timer_hook, 0) == 0);
		assert(TimerEnable(pStatus->timerFd) == 0);
	}
}

static void eth_sender_suspend(void)
{
	/* check if inited */
	assert(pStatus->ethInited);
	
	/* Make sure all packets sent is received */
	assert(TimerDisable(pStatus->timerFd) == 0);
	taskDelay(1);
	assert(semGive(pStatus->muxSem) == OK);
}

static void eth_sender_resume(void)
{
	/* Re-enable timer */
	assert(TimerEnable(pStatus->timerFd) == 0);
}

static void eth_show(char * buf)
{
	int i;
	
	if (pStatus->ethInited)
	{
		eth_sender_suspend();
		
		sprintf(buf, "\n*********** ETH ***********\n");
		for (i = 0; i < ETH_DEV_COUNT; i++)
		{
			sprintf(buf + strlen(buf),
				"eth%d : Send %u Recv %u Fail %u\n",
				i+1, pStatus->pktSent[i], pStatus->pktRecv[i],
				pStatus->pktFail[i]);
		}
		
		eth_sender_resume();
	}
}

MODULE_REGISTER(eth);
