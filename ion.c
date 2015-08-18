#include "lib.h"

#define IO_ADDR		12

typedef struct iom_status
{
	struct {
		UINT32 WDOG;
		UINT32 PWRUP;
		UINT32 BROWNOUT;
		UINT32 MCU;
		UINT32 SOFTWARE;
	}RESET;
	UINT32 TEMPERATURE;
}IOM_STATUS_S;

static int ionFd;
static ION_PKT_S RECV_PKT, SEND_PKT;
static BOOL ionInited = FALSE;
static SEM_ID muxSem;
static IOM_STATUS_S status;

static ION_PKT_S * ionHook(UINT8 type)
{
	return &RECV_PKT;
}
#ifdef DISPLAY
static void ion_pkt_display(void * arg, char * desc)
{
	ION_PKT_S * pPkt = arg;
    UINT32 i;
    printf("ION Packet %s : SRC = 0x%X, DST = 0x%X, PRI = %d, RP = %d, DLC = %d, DATA = ",
            desc, pPkt->SRC, pPkt->DST, pPkt->PRI, pPkt->RP, pPkt->DLC);

    for (i = 0; i < pPkt->DLC; i++)
        printf("%02X ", pPkt->pkt_buf[i]);
    printf("\n");
}
#else
static void ion_pkt_display(void * arg, char * desc) {}
#endif

static void ion_send_pwrup_check(void)
{
	assert(ionInited);
	
	memset(&SEND_PKT, 0, sizeof(SEND_PKT));

	/* 0x02 0x40 CRC */
	SEND_PKT.PRI = 6;
	SEND_PKT.DST = IO_ADDR;
	SEND_PKT.DLC = 3;
	SEND_PKT.pkt_buf[0] = 2;
	SEND_PKT.pkt_buf[1] = 0x40;
	SEND_PKT.pkt_buf[2] = 0x00; /* CRC is ignored by driver due to degration */
	
	ion_pkt_display(&SEND_PKT, "Send");
	
	assert(IONPktSend(ionFd, &SEND_PKT) == 0);
	
	assert(semGive(muxSem) == OK);
}

static void ion_decode_pwrup_check(void)
{
	if (RECV_PKT.DLC != 49)
		return;
	switch(RECV_PKT.pkt_buf[47])
	{
	case 0:
		status.RESET.WDOG++;
		break;
	case 1:
		status.RESET.PWRUP++;
		break;
	case 2:
		status.RESET.BROWNOUT++;
		break;
	case 3:
		status.RESET.MCU++;
		break;
	case 4:
		status.RESET.SOFTWARE++;
		break;
	default:
		break;
	}
}

static void ion_send_temp_check(void)
{
	assert(ionInited);
	
	memset(&SEND_PKT, 0, sizeof(SEND_PKT));

	/* 0x02 0x40 CRC */
	SEND_PKT.PRI = 6;
	SEND_PKT.DST = IO_ADDR;
	SEND_PKT.DLC = 3;
	SEND_PKT.pkt_buf[0] = 2;
	SEND_PKT.pkt_buf[1] = 0x63;
	SEND_PKT.pkt_buf[2] = 0x00; /* CRC is ignored by driver due to degration */
	
	ion_pkt_display(&SEND_PKT, "Send");
	
	assert(IONPktSend(ionFd, &SEND_PKT) == 0);
	
	assert(semGive(muxSem) == OK);
}

static void ion_decode_temp_check(void)
{
	if (RECV_PKT.DLC < 4)
		return;
	status.TEMPERATURE = RECV_PKT.pkt_buf[3];
}

static int polling_task(void)
{
	INT32 ret;
	assert(ionInited);
	
	FOREVER
	{
		/* Wait for one packet sent */
		assert(semTake(muxSem, WAIT_FOREVER) == OK);
		
		do
		{
			/* Receive the packet that sent to us */
			ret = IONPktPoll(ionFd);
			if (((ret == 0) || (ret == -ENOSPC)) && (RECV_PKT.DST == addr_get()))
			{
				switch(RECV_PKT.pkt_buf[1])
				{
				case 0x00:
					ion_decode_pwrup_check();
					break;
				case 0x16:
					ion_decode_temp_check();
					break;
				default:
					break;
				}
				ion_pkt_display(&RECV_PKT, "Recv");
			}
		}while(ret != -EAGAIN);
	}
}

static void ion_init(void)
{
	/* Only init once */
	if (ionInited)
		return;
	
    /* Request handler */
	ionFd = iondev_get();
	assert (ionFd >= 0);
	
	/* Malloc paket buffer */
	SEND_PKT.pkt_buf = malloc(256);
	assert(SEND_PKT.pkt_buf != NULL);

	RECV_PKT.pkt_buf = malloc(256);
	assert(RECV_PKT.pkt_buf != NULL);
	
	/* Initialize semaphore */
	muxSem = semBCreate(SEM_Q_PRIORITY, SEM_EMPTY);
	assert(muxSem != NULL);
	
	/* Hook the recevice function */
	assert(IONHookRegister(ionFd, ionHook) == 0);
	
	/* Init done */
	ionInited = TRUE;
	
	/* Start polling task */
	taskSpawn("tIONPoll", 255, 0, 0x40000, polling_task, 0,0,0,0,0,0,0,0,0,0);
}


static int ion_check_task(void)
{
	FOREVER
	{
		ion_send_pwrup_check();
		taskDelay(1);
		ion_send_temp_check();
		taskDelay(1);
	}
}

void ion_start(void)
{
	/* Basic ION initialization */
	ion_init();
	
	/* Spawn a task for ion send */
	taskSpawn("tIONChecker", 255, 0, 0x40000, ion_check_task, 0,0,0,0,0,0,0,0,0,0);
}

void ion_show(char * buf)
{
	sprintf(buf, "\n"
			"*********** IOM ***********\n"
			"Watchdog Reset         : %u\n"
			"Power Up Reset         : %u\n"
			"Brownout Reset         : %u\n"
			"MCU Watchdog Reset     : %u\n"
			"Software Upgrade Reset : %u\n"
			"Temperature            : %u\n",
			status.RESET.WDOG,
			status.RESET.PWRUP,
			status.RESET.BROWNOUT,
			status.RESET.MCU,
			status.RESET.SOFTWARE,
			status.TEMPERATURE
			);
}
