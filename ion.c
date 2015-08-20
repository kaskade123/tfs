#include "lib.h"

#define IO_ADDR		0x3E

typedef struct iom_status
{
	UINT32 RESETS;
	UINT32 TEMPERATURE;
	UINT32 pktSent;
	UINT32 pktRecv;
}IOM_STATUS_S;

static int ionFd;
static ION_PKT_S RECV_PKT, SEND_PKT;
static BOOL ionInited = FALSE;
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

static void ion_send_statistics_check(void)
{
	assert(ionInited);
	
	/* 0x02 0x40 CRC */
	SEND_PKT.PRI = 6;
	SEND_PKT.RP = 0;
	SEND_PKT.DST = IO_ADDR;
	SEND_PKT.DLC = 4;
	SEND_PKT.pkt_buf[0] = 3;
	SEND_PKT.pkt_buf[1] = 0x49;
	SEND_PKT.pkt_buf[2] = 1;
	SEND_PKT.pkt_buf[3] = 0x00; /* CRC ignored if packet can degration */
	
	ion_pkt_display(&SEND_PKT, "Send");
	
	assert(IONPktSend(ionFd, &SEND_PKT) == 0);
	
	status.pktSent++;
}

static void ion_decode_statistics_check(void)
{
	if (RECV_PKT.DLC < 61)
		return;
	
	status.pktRecv++;
	
	memcpy(&status.RESETS, RECV_PKT.pkt_buf + 56, 4);
	
	/* little endian to big endian convert */
	status.RESETS = (status.RESETS & 0x000000FF) << 24 ||
					(status.RESETS & 0x0000FF00) << 8  ||
					(status.RESETS & 0x00FF0000) >> 8  ||
					(status.RESETS & 0xFF000000) >> 24;
}

static void ion_send_temp_check(void)
{
	assert(ionInited);
	
	/* 0x02 0x40 CRC */
	SEND_PKT.PRI = 6;
	SEND_PKT.RP = 0;
	SEND_PKT.DST = IO_ADDR;
	SEND_PKT.DLC = 3;
	SEND_PKT.pkt_buf[0] = 2;
	SEND_PKT.pkt_buf[1] = 0x63;
	SEND_PKT.pkt_buf[2] = 0x00; /* CRC is ignored by driver due to degration */
	
	ion_pkt_display(&SEND_PKT, "Send");
	
	assert(IONPktSend(ionFd, &SEND_PKT) == 0);
	
	status.pktSent++;
}

static void ion_decode_temp_check(void)
{
	if (RECV_PKT.DLC < 4)
		return;
	status.pktRecv++;
	status.TEMPERATURE = RECV_PKT.pkt_buf[3];
}

static int polling_task(void)
{
	INT32 ret;
	assert(ionInited);
	
	FOREVER
	{
		do
		{
			/* Receive the packet that sent to us */
			ret = IONPktPoll(ionFd);
			if (((ret == 0) || (ret == -ENOSPC)) && (RECV_PKT.DST == addr_get()))
			{
				switch(RECV_PKT.pkt_buf[1])
				{
				case 0x05:
					ion_decode_statistics_check();
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
		taskDelay(1);
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
	
	/* Initialize status struct */
	memset(&status, 0, sizeof(status));
	
	/* Hook the recevice function */
	assert(IONHookRegister(ionFd, ionHook) == 0);
	
	/* Init done */
	ionInited = TRUE;
	
	/* Start polling task */
	taskSpawn("tIONPoll", 255, 0, 0x40000, polling_task, 0,0,0,0,0,0,0,0,0,0);
}


static int ion_check_task(void)
{
	static unsigned int counter = 0;
	FOREVER
	{
		ion_send_temp_check();
		taskDelay(sysClkRateGet());
		if (counter ++ >= 30)
		{
			ion_send_statistics_check();
			counter = 0;
		}
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
			"Resets After Power Up  : %u\n"
			"Temperature            : %u\n"
			"Packet Sent            : %u\n"
			"Packet Recv            : %u\n"
			"Packet Missing         : %u\n",
			status.RESETS,
			status.TEMPERATURE,
			status.pktSent,
			status.pktRecv,
			status.pktSent - status.pktRecv
			);
}
