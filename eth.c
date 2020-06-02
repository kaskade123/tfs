#include "lib.h"
/*add some words*/
#define ETH_DEV_COUNT	4
#define ETH_DEV_PREFIX	"MMS"
#define ETH_BUFFER_LEN	1600

#define ETH_BW_LIMIT	10000000    /* BW limited to 10Mbps */
#define ETH_PKT_LEN		1500		/* Packet Length */
#define ETH_TIMER_FREQ	(ETH_BW_LIMIT / 8 / ETH_PKT_LEN * 2)

typedef struct eth_status
{
	BOOL 	ethInited;
	INT32 	hdr[ETH_DEV_COUNT];		/* Ethernet device handler */
	UINT8 * pkt; 	                /* Ethernet packet buffer */
	SEM_ID  rxSem;
	UINT32 	pktCksum[ETH_DEV_COUNT];/* Ethernet packet cksum */
	UINT32 	pktSent[ETH_DEV_COUNT]; /* Ethernet packet sent */
	UINT32 	pktRecv[ETH_DEV_COUNT];	/* Ethernet packet received */
	UINT32 	pktSendFail[ETH_DEV_COUNT]; /* Ethernet packet send fail */
	UINT32  pktRecvFail[ETH_DEV_COUNT]; /* Ethernet packet recv fail */
	INT32 	timerFd;				/* Timer Handler */
}ETH_STATUS_S;

static ETH_STATUS_S * pStatus = NULL;

static BOOL eth_counting_hook(void * pDev, UINT8 *pBuf, UINT32 bufLen)
{
	ETHERNET_DEV_S * p = pDev;
	UINT32 cksum, idx;

	/* Hook for eth1 - eth4 */
	if ((strlen(p->name) == 4) &&
			(strncmp(p->name, ETH_DEV_PREFIX, strlen(ETH_DEV_PREFIX)) == 0))
	{
	    idx = p->name[strlen(ETH_DEV_PREFIX)] - '1';
	    calc_fletcher32(pBuf, bufLen, &cksum);
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
#endif
	    if (cksum == pStatus->pktCksum[idx])
	        pStatus->pktRecv[idx] ++;
	    else
	        pStatus->pktRecvFail[idx] ++;
	}
    return TRUE;
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

static int eth_task_entry(void)
{
    uint32_t cnt = 0;
    FOREVER
    {
        int i;

        semTake(pStatus->rxSem, WAIT_FOREVER);
        cnt ++;
        for (i = 0; i < ETH_DEV_COUNT; i++)
        {
            uint32_t pktLimit = 32;
            if (pStatus->hdr[i] < 0)
                continue;
            EthernetRecvPoll(pStatus->hdr[i], &pktLimit);
            if (cnt >= 2)
            {
                if (eth_send_random(pStatus->hdr[i], pStatus->pkt, ETH_PKT_LEN, &pStatus->pktCksum[i]))
                    pStatus->pktSendFail[i]++;
                else
                    pStatus->pktSent[i]++;
            }
        }

        if (cnt >= 2)
            cnt = 0;
    }

    return 0;
}

static void eth_start(void)
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
	pStatus->rxSem = semBCreate(SEM_Q_PRIORITY, SEM_EMPTY);
	assert(pStatus->rxSem);

	pStatus->ethInited = FALSE;

	for (i = 0; i < ETH_DEV_COUNT; i++)
	{
		/* Get eth device name */
		char ethName[5];
		sprintf(ethName, "%s%d", ETH_DEV_PREFIX, i+1);

		/* Request handler, if failed, this is CPU board */
		pStatus->hdr[i] = ethdev_get(ethName);
		if (pStatus->hdr[i] < 0)
			continue;

		/* Initialize packet buffer */
		pStatus->pkt = malloc(ETH_BUFFER_LEN);
		assert(pStatus->pkt != NULL);

		/* Initialize packet counter */
		pStatus->pktSent[i] = 0;
		pStatus->pktRecv[i] = 0;
		pStatus->pktSendFail[i] = 0;

		/* Drop all current packets */
		assert(EthernetPktDrop(pStatus->hdr[i], 512) >= 0);

		/* Hook callbacks */
		assert(EthernetHookDisable(pStatus->hdr[i]) == 0);
		assert(EthernetRecvHook(pStatus->hdr[i], eth_counting_hook) == 0);
		assert(EthernetHookEnable(pStatus->hdr[i]) == 0);
	}

    taskSpawn("tEthLoopback", 50, VX_FP_TASK, 0x4000, eth_task_entry,
            1,2,3,4,5,6,7,8,9,10);

	pStatus->timerFd = timer_set(ETH_TIMER_FREQ, pStatus->rxSem);
	assert(pStatus->timerFd >= 0);

	pStatus->ethInited = TRUE;
}

static void eth_sender_suspend(void)
{
	/* check if inited */
	assert(pStatus->ethInited);

	/* Make sure all packets sent is received */
	assert(TimerDisable(pStatus->timerFd) == 0);
	taskDelay(1);
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
		    if (pStatus->hdr[i] >= 0)
                sprintf(buf + strlen(buf),
                        "eth%d : Send %u Recv %u Send Fail %u Recv Fail %u Missing %u\n", i + 1,
                        pStatus->pktSent[i], pStatus->pktRecv[i],
                        pStatus->pktSendFail[i], pStatus->pktRecvFail[i],
                        pStatus->pktSent[i] - pStatus->pktRecv[i] - pStatus->pktRecvFail[i]);
		}

		eth_sender_resume();
	}
}

MODULE_REGISTER(eth);
