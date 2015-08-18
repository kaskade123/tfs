#include "lib.h"

typedef struct canhcb_status
{
	UINT32 len_crc_error;
	UINT32 bit_error;
	UINT32 timing_error;
	UINT32 arbitration_error;
	UINT32 coding_error;
	UINT64 send_pkts;
	UINT64 recv_pkts;
}CANHCB_STATUS_S;

static int canhcbFd;
static int LightFd;
static int timerFd;
static CANHCB_PKT_S SEND_PKT;
static CANHCB_PKT_S RECV_PKT;
static BOOL INITED = FALSE;
static SEM_ID muxSem;
static char print_buf[512];
static CANHCB_STATUS_S STAT;

#define CANHCB_BUF_LEN			500		/* Packet DLC limit */
#define CANHCB_PKT_LEN			300		/* 300 Bytes pkt */
#define CANHCB_BW_LIMIT			1000000	/* 1Mbps */
#define CANHCB_TIMER_FREQ		(CANHCB_BW_LIMIT / 8 / CANHCB_PKT_LEN)

#define CANHCB_POLLING_TASK_PRIORITY	40

static void canhcb_stat_update(void)
{
	INT32 regVal;
	
	assert(INITED);
	
	regVal = CANHCBStatusGet(canhcbFd);
	
	assert(regVal >= 0);
	if (regVal != 0)
	{
		if (regVal & SAC_CANHCB_STATUS_BIT_ERR)
			STAT.bit_error++;
		if (regVal & SAC_CANHCB_STATUS_TIMING_ERR)
			STAT.timing_error++;
		if (regVal & SAC_CANHCB_STATUS_ARBITRATION_FAIL)
			STAT.arbitration_error++;
		if (regVal & SAC_CANHCB_STATUS_CODE_ERR)
			STAT.coding_error++;
		if (regVal & SAC_CANHCB_STATUS_LEN_CRC_ERR)
			STAT.len_crc_error++;
	}
}

static CANHCB_PKT_S * canhcb_hook(UINT32 src)
{
	STAT.recv_pkts++;
	return &RECV_PKT;
}

static int polling_task(void)
{
	assert(INITED == TRUE);
	
	while(1)
	{
		INT32 ret;
		
		/* Wait the send is done */
		assert(semTake(muxSem, WAIT_FOREVER) == OK);
		
		/* Receive All the packets */
		do
		{
			ret = CANHCBPktPoll(canhcbFd);
		}while(ret != -EAGAIN);
		
		/* Update status */
		canhcb_stat_update();
		
		/* Trigger LED blink */
		if (!(STAT.send_pkts % CANHCB_TIMER_FREQ))
			LightOn(LightFd);
		else if (!(STAT.send_pkts % (CANHCB_TIMER_FREQ * 2 + 1)))
			LightOff(LightFd);
	}
}

static void canhcb_init(void)
{
	/* Only initialize once */
	if (INITED)
		return;
	
	/* Get canhcb device handler */
	canhcbFd = canhcbdev_get();
	assert(canhcbFd >= 0);
	
	/* Request Light */
	LightFd = light_get();
	assert(LightFd >= 0);
	
	/* Light initial to off */
	assert(LightOff(LightFd) == 0);
	
	/* Timer Get */
	timerFd = timer_get();
	assert(timerFd >= 0);
	
	/* Initialize packet structure */
	SEND_PKT.pkt_buf = malloc(CANHCB_BUF_LEN);
	assert(SEND_PKT.pkt_buf != NULL);
	
	RECV_PKT.pkt_buf = malloc(CANHCB_BUF_LEN);
	assert(RECV_PKT.pkt_buf != NULL);
	
	/* Initialize stat struct */
	memset(&STAT, 0, sizeof(STAT));
	
	/* Initialize semaphore */
	muxSem = semBCreate(SEM_Q_FIFO, SEM_EMPTY);
	assert(muxSem != NULL);
	
	/* Register Hook */
	assert(CANHCBHookRegister(canhcbFd, canhcb_hook) == 0);
	
	/* All done */
	INITED = TRUE;
	
	/* Start polling task */
	taskSpawn("tCANHCBPoll", CANHCB_POLLING_TASK_PRIORITY, 0, 0x40000, polling_task, 0,0,0,0,0,0,0,0,0,0);
}

static void canhcb_send(INT32 len)
{
	INT32 ret;
	
	/* validate the packet length */
	assert (len <= CANHCB_BUF_LEN);
	
	/* Randomize the packet data */
	rand_range(SEND_PKT.pkt_buf, len);
	
	/* Send one packet to ourselves */
	SEND_PKT.DLC = len;
	SEND_PKT.DST = 0x0001 << addr_get();
	do
	{
		ret = CANHCBPktSend(canhcbFd, &SEND_PKT);
	}while(ret == -EBUSY);
	
	/* Do statics recording */
	STAT.send_pkts++;
	
	/* Trigger packet polling task
	 *
	 * Due to the packet loop back time, there is always one packet missing. 
	 */
	assert(semGive(muxSem) == OK);
}

void canhcb_sender_start(void)
{
	/* Initialize canhcb */
	canhcb_init();

	/* Initialize timer, the throughput limited to 1Mbps. We send out a 100
	 * bytes packet, which is 800 bits. To reach 1Mbps, we need to send out 1250
	 * pkts one second. */
	assert(TimerDisable(timerFd) == 0);
	assert(TimerFreqSet(timerFd, CANHCB_TIMER_FREQ) == 0);
	assert(TimerISRSet(timerFd, canhcb_send, CANHCB_PKT_LEN) == 0);
	assert(TimerEnable(timerFd) == 0);
}

void canhcb_sender_suspend(void)
{
	/* check if HCB inited */
	assert(INITED);
	
	/* Make sure all packets sent is received */
	assert(TimerDisable(timerFd) == 0);
	taskDelay(1);
	assert(semGive(muxSem) == OK);
}

void canhcb_sender_resume(void)
{
	/* Re-enable timer */
	assert(TimerEnable(timerFd) == 0);
}

void canhcb_show(void)
{
	/* construct information content */
	sprintf(print_buf, "\n"
			"*********** HCB ***********\n"
			"LEN CRC Error          : %u\n"
			"Bit Error              : %u\n"
			"Timing Error           : %u\n"
			"Arbitration Error      : %u\n"
			"4B5B Coding Error      : %u\n"
			"Total Send Pkts        : %llu\n"
			"Total Recv Pkts        : %llu\n"
			"Total Missing Pkts     : %llu\n",
			STAT.len_crc_error,
			STAT.bit_error,
			STAT.timing_error,
			STAT.arbitration_error,
			STAT.coding_error,
			STAT.send_pkts,
			STAT.recv_pkts,
			STAT.send_pkts - STAT.recv_pkts
	);

	logMsg(print_buf, 0,0,0,0,0,0);
}
