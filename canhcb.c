#include "lib.h"

typedef struct canhcb_status
{
	int canhcbFd;
	int timerFd;
	CANHCB_PKT_S SEND_PKT;
	CANHCB_PKT_S RECV_PKT;
	BOOL INITED;
	SEM_ID muxSem;
	UINT32 len_crc_error;
	UINT32 bit_error;
	UINT32 timing_error;
	UINT32 arbitration_error;
	UINT32 coding_error;
	UINT64 send_pkts;
	UINT64 recv_pkts;
	QJOB job;
	UINT32 in_process;
}CANHCB_STATUS_S;

static CANHCB_STATUS_S * pStatus = NULL;

#define CANHCB_BUF_LEN			500		/* Packet DLC limit */
#define CANHCB_PKT_LEN			300		/* 300 Bytes pkt */
#define CANHCB_BW_LIMIT			1000000	/* 1Mbps */
#define CANHCB_TIMER_FREQ		(CANHCB_BW_LIMIT / 8 / CANHCB_PKT_LEN)

#define CANHCB_POLLING_TASK_PRIORITY	40

static void canhcb_stat_update(void)
{
	INT32 regVal;
	
	assert(pStatus->INITED);
	
	regVal = CANHCBStatusGet(pStatus->canhcbFd);
	
	assert(regVal >= 0);
	if (regVal != 0)
	{
		if (regVal & SAC_CANHCB_STATUS_BIT_ERR)
			pStatus->bit_error++;
		if (regVal & SAC_CANHCB_STATUS_TIMING_ERR)
			pStatus->timing_error++;
		if (regVal & SAC_CANHCB_STATUS_ARBITRATION_FAIL)
			pStatus->arbitration_error++;
		if (regVal & SAC_CANHCB_STATUS_CODE_ERR)
			pStatus->coding_error++;
		if (regVal & SAC_CANHCB_STATUS_LEN_CRC_ERR)
			pStatus->len_crc_error++;
	}
}

static CANHCB_PKT_S * canhcb_hook(UINT32 src)
{
	pStatus->recv_pkts++;
	return &pStatus->RECV_PKT;
}

static CANHCB_PKT_S * canhcb_hook_null(UINT32 src)
{
	return NULL;
}

static int polling_task(void)
{
	assert(pStatus->INITED == TRUE);
	
	while(1)
	{
		INT32 ret;
		
		/* Wait the send is done */
		assert(semTake(pStatus->muxSem, WAIT_FOREVER) == OK);
		
		/* Receive All the packets */
		do
		{
			ret = CANHCBPktPoll(pStatus->canhcbFd);
		}while(ret != -EAGAIN);
		
		/* Update status */
		canhcb_stat_update();
	}
}

static void canhcb_init(void)
{
	/* Only initialize once */
	if (pStatus && pStatus->INITED)
		return;
	
	/* Malloc status struct */
	pStatus = malloc(sizeof(*pStatus));
	assert (pStatus);
	memset(pStatus, 0, sizeof(*pStatus));
	
	/* Get canhcb device handler */
	pStatus->canhcbFd = canhcbdev_get();
	if (pStatus->canhcbFd < 0)
	{
	    free(pStatus);
	    pStatus = NULL;
	    return;
	}

	/* Timer Get */
	pStatus->timerFd = timer_get();
	assert(pStatus->timerFd >= 0);
	
	/* Initialize packet structure */
	pStatus->SEND_PKT.pkt_buf = malloc(CANHCB_BUF_LEN);
	assert(pStatus->SEND_PKT.pkt_buf != NULL);
	
	pStatus->RECV_PKT.pkt_buf = malloc(CANHCB_BUF_LEN);
	assert(pStatus->RECV_PKT.pkt_buf != NULL);
	
	/* Initialize semaphore */
	pStatus->muxSem = semBCreate(SEM_Q_FIFO, SEM_EMPTY);
	assert(pStatus->muxSem != NULL);
	
	/* Register the NULL Hook */
	assert(CANHCBHookRegister(pStatus->canhcbFd, canhcb_hook_null) == 0);
	
	/* Drop all the packets */
	while (CANHCBPktPoll(pStatus->canhcbFd) != -EAGAIN);
	
	/* Register the real hook */
	assert(CANHCBHookRegister(pStatus->canhcbFd, canhcb_hook) == 0);
	
	/* All done */
	pStatus->INITED = TRUE;
	
	/* Start polling task */
	taskSpawn("tCANHCBPoll", CANHCB_POLLING_TASK_PRIORITY, 0, 0x40000, polling_task, 0,0,0,0,0,0,0,0,0,0);
}

static void canhcb_send_task(void * arg)
{
	INT32 ret;
	
	/* Randomize the packet data */
	rand_range(pStatus->SEND_PKT.pkt_buf, CANHCB_PKT_LEN);
	
	/* Send one packet to ourselves */
	pStatus->SEND_PKT.DLC = CANHCB_PKT_LEN;
	pStatus->SEND_PKT.DST = 0x0001 << addr_get();
	ret = CANHCBPktSend(pStatus->canhcbFd, &pStatus->SEND_PKT);
	if (ret == 0)
	{
		/* Do statics recording */
		pStatus->send_pkts++;
		
		/* Trigger packet polling task
		 *
		 * Due to the packet loop back time, there is always one packet missing. 
		 */
		assert(semGive(pStatus->muxSem) == OK);
	}
	pStatus->in_process = 0;
}

static void canhcb_send(INT32 len)
{
	if (pStatus->in_process == 0)
	{
		pStatus->in_process = 1;
		pStatus->job.func = canhcb_send_task;
		QJOB_SET_PRI(&pStatus->job, 20);
		queue_add(&pStatus->job);
	}
}

static void canhcb_start(void)
{
	/* Initialize canhcb */
	canhcb_init();

	if (pStatus)
	{
        /* Initialize timer, the throughput limited to 1Mbps. We send out a 100
         * bytes packet, which is 800 bits. To reach 1Mbps, we need to send out 1250
         * pkts one second. */
        assert(TimerDisable(pStatus->timerFd) == 0);
        assert(TimerFreqSet(pStatus->timerFd, CANHCB_TIMER_FREQ) == 0);
        assert(TimerISRSet(pStatus->timerFd, canhcb_send, 0) == 0);
        assert(TimerEnable(pStatus->timerFd) == 0);
	}
}

static void canhcb_sender_suspend(void)
{
	/* check if HCB inited */
	assert(pStatus->INITED);
	
	/* Make sure all packets sent is received */
	assert(TimerDisable(pStatus->timerFd) == 0);
	taskDelay(1);
	assert(semGive(pStatus->muxSem) == OK);
}

static void canhcb_sender_resume(void)
{
	/* Re-enable timer */
	assert(TimerEnable(pStatus->timerFd) == 0);
}

static void canhcb_show(char * buf)
{
    if (!pStatus)
        return;
	canhcb_sender_suspend();
	
	/* construct information content */
	sprintf(buf, "\n"
			"*********** HCB ***********\n"
			"LEN CRC Error          : %u\n"
			"Bit Error              : %u\n"
			"Timing Error           : %u\n"
			"Arbitration Error      : %u\n"
			"4B5B Coding Error      : %u\n"
			"Total Send Pkts        : %llu\n"
			"Total Recv Pkts        : %llu\n"
			"Total Missing Pkts     : %llu\n",
			pStatus->len_crc_error,
			pStatus->bit_error,
			pStatus->timing_error,
			pStatus->arbitration_error,
			pStatus->coding_error,
			pStatus->send_pkts,
			pStatus->recv_pkts,
			pStatus->send_pkts - pStatus->recv_pkts
	);
	
	canhcb_sender_resume();
}

MODULE_REGISTER(canhcb);
