#include "lib.h"

#define TEMP_ADDR		0x15	/* Only get the temp of IO that near PWR */
#define IO_ADDR1		10
#define IO_ADDR2		14

typedef struct iom_status
{
	int ionFd;
	ION_PKT_S RECV_PKT, SEND_PKT;
	BOOL ionInited;
	UINT32 RESETS;
	UINT32 TEMPERATURE;
	UINT32 pktSent;
	UINT32 pktRecv;
	ION_COUNTER_S counter;
}IOM_STATUS_S;

static IOM_STATUS_S * pStatus = NULL;

static ION_PKT_S * ionHook(UINT8 type)
{
	return &pStatus->RECV_PKT;
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
	assert(pStatus->ionInited);
	
	/* 0x02 0x40 CRC */
	pStatus->SEND_PKT.PRI = 6;
	pStatus->SEND_PKT.RP = 0;
	pStatus->SEND_PKT.DST = TEMP_ADDR;
	pStatus->SEND_PKT.DLC = 4;
	pStatus->SEND_PKT.pkt_buf[0] = 3;
	pStatus->SEND_PKT.pkt_buf[1] = 0x49;
	pStatus->SEND_PKT.pkt_buf[2] = 1;
	pStatus->SEND_PKT.pkt_buf[3] = 0x00; /* CRC ignored if packet can degration */
	
	ion_pkt_display(&pStatus->SEND_PKT, "Send");
	
	assert(IONPktSend(pStatus->ionFd, &pStatus->SEND_PKT) == 0);
	
	pStatus->pktSent++;
}

static void ion_decode_statistics_check(void)
{
	if (pStatus->RECV_PKT.DLC < 61)
		return;
	
	pStatus->pktRecv++;
	
	memcpy(&pStatus->RESETS, pStatus->RECV_PKT.pkt_buf + 56, 4);
	
	/* little endian to big endian convert */
	pStatus->RESETS = (pStatus->RESETS & 0x000000FF) << 24 ||
					  (pStatus->RESETS & 0x0000FF00) << 8  ||
					  (pStatus->RESETS & 0x00FF0000) >> 8  ||
					  (pStatus->RESETS & 0xFF000000) >> 24;
}

static void ion_send_temp_check(void)
{
	assert(pStatus->ionInited);
	
	/* 0x02 0x40 CRC */
	pStatus->SEND_PKT.PRI = 6;
	pStatus->SEND_PKT.RP = 0;
	pStatus->SEND_PKT.DST = TEMP_ADDR;
	pStatus->SEND_PKT.DLC = 3;
	pStatus->SEND_PKT.pkt_buf[0] = 2;
	pStatus->SEND_PKT.pkt_buf[1] = 0x63;
	pStatus->SEND_PKT.pkt_buf[2] = 0x00; /* CRC is ignored by driver due to degration */
	
	ion_pkt_display(&pStatus->SEND_PKT, "Send");
	
	assert(IONPktSend(pStatus->ionFd, &pStatus->SEND_PKT) == 0);
	
	pStatus->pktSent++;
}

static void ion_decode_temp_check(void)
{
	if (pStatus->RECV_PKT.DLC < 4)
		return;
	pStatus->pktRecv++;
	pStatus->TEMPERATURE = pStatus->RECV_PKT.pkt_buf[3];
}

static void ion_send_do_active(UINT8 addr)
{
	assert(pStatus->ionInited);
	
	/* 0x02 0x40 CRC */
	pStatus->SEND_PKT.PRI = 6;
	pStatus->SEND_PKT.RP = 0;
	pStatus->SEND_PKT.DST = addr;
	pStatus->SEND_PKT.DLC = 9;
	pStatus->SEND_PKT.pkt_buf[0] = 8;		/* LEN */
	pStatus->SEND_PKT.pkt_buf[1] = 0x5A;	/* TYPE */
	pStatus->SEND_PKT.pkt_buf[2] = 0xFF;	/* MSK */
	pStatus->SEND_PKT.pkt_buf[3] = 0xFF;	/* DO */
	pStatus->SEND_PKT.pkt_buf[4] = 0xFF;	/* MSK */
	pStatus->SEND_PKT.pkt_buf[5] = 0xFF;	/* DO */
	pStatus->SEND_PKT.pkt_buf[6] = 0xFF;	/* MSK */
	pStatus->SEND_PKT.pkt_buf[7] = 0xFF;	/* DO */
	pStatus->SEND_PKT.pkt_buf[8] = 0x00;	/* CRC */
	
	ion_pkt_display(&pStatus->SEND_PKT, "Send");
	
	assert(IONPktSend(pStatus->ionFd, &pStatus->SEND_PKT) == 0);
	
	pStatus->pktSent++;
}

static void ion_send_do_deactive(UINT8 addr)
{
	assert(pStatus->ionInited);
	
	/* 0x02 0x40 CRC */
	pStatus->SEND_PKT.PRI = 6;
	pStatus->SEND_PKT.RP = 0;
	pStatus->SEND_PKT.DST = addr;
	pStatus->SEND_PKT.DLC = 9;
	pStatus->SEND_PKT.pkt_buf[0] = 8;		/* LEN */
	pStatus->SEND_PKT.pkt_buf[1] = 0x5A;	/* TYPE */
	pStatus->SEND_PKT.pkt_buf[2] = 0xFF;	/* MSK */
	pStatus->SEND_PKT.pkt_buf[3] = 0x00;	/* DO */
	pStatus->SEND_PKT.pkt_buf[4] = 0xFF;	/* MSK */
	pStatus->SEND_PKT.pkt_buf[5] = 0x00;	/* DO */
	pStatus->SEND_PKT.pkt_buf[6] = 0xFF;	/* MSK */
	pStatus->SEND_PKT.pkt_buf[7] = 0x00;	/* DO */
	pStatus->SEND_PKT.pkt_buf[8] = 0x00;	/* CRC */
	
	ion_pkt_display(&pStatus->SEND_PKT, "Send");
	
	assert(IONPktSend(pStatus->ionFd, &pStatus->SEND_PKT) == 0);
	
	pStatus->pktSent++;
}

static int polling_task(void)
{
	INT32 ret;
	assert(pStatus->ionInited);
	
	FOREVER
	{
		do
		{
			/* Receive the packet that sent to us */
			ret = IONPktPoll(pStatus->ionFd);
			if (((ret == 0) || (ret == -ENOSPC)) &&
					(pStatus->RECV_PKT.DST == addr_get()))
			{
				switch(pStatus->RECV_PKT.pkt_buf[1])
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
				ion_pkt_display(&pStatus->RECV_PKT, "Recv");
			}
		}while(ret != -EAGAIN);
		taskDelay(1);
	}
}

static void ion_init(void)
{
	/* Only init once */
	if (pStatus && pStatus->ionInited)
		return;
	
	/* Malloc status struct */
	pStatus = malloc(sizeof(*pStatus));
	assert (pStatus);
	memset(pStatus, 0, sizeof(*pStatus));
	
    /* Request handler */
	pStatus->ionFd = iondev_get();
	assert (pStatus->ionFd >= 0);
	
	/* Malloc paket buffer */
	pStatus->SEND_PKT.pkt_buf = malloc(256);
	assert(pStatus->SEND_PKT.pkt_buf != NULL);

	pStatus->RECV_PKT.pkt_buf = malloc(256);
	assert(pStatus->RECV_PKT.pkt_buf != NULL);
	
	/* Hook the recevice function */
	assert(IONHookRegister(pStatus->ionFd, ionHook) == 0);
	
	/* Init done */
	pStatus->ionInited = TRUE;
	
	/* Start polling task */
	taskSpawn("tIONPoll", 253, 0, 0x40000, polling_task, 0,0,0,0,0,0,0,0,0,0);
}


static int ion_check_task(void)
{
	static unsigned int counter = 0;
	FOREVER
	{
		ion_send_temp_check();
		if (addr_get() == 2)
		{
			if (counter % 2)
			{
				ion_send_do_active(IO_ADDR1);
				ion_send_do_active(IO_ADDR2);
			}
			else
			{
				ion_send_do_deactive(IO_ADDR1);
				ion_send_do_deactive(IO_ADDR2);
			}
		}
		taskDelay(sysClkRateGet());
		if (counter ++ >= 30)
		{
			ion_send_statistics_check();
			counter = 0;
		}
	}
}

static void ion_start(void)
{
	/* Basic ION initialization */
	ion_init();
	
	/* Spawn a task for ion send */
	taskSpawn("tIONChecker", 253, 0, 0x40000, ion_check_task, 0,0,0,0,0,0,0,0,0,0);
}

static void ion_show(char * buf)
{
	sprintf(buf, "\n"
			"*********** IOM ***********\n"
			"Resets After Power Up  : %u\n"
			"Temperature            : %u\n"
			"ION Ack Error          : %u\n"
			"ION Bit Error          : %u\n"
			"ION CRC Error          : %u\n"
			"ION Format Error       : %u\n"
			"ION In-Continuty Error : %u\n"
			"ION Send Error         : %u\n"
			"ION Stuff Error        : %u\n"
			"Packet Sent            : %u\n"
			"Packet Recv            : %u\n"
			"Packet Missing         : %u\n",
			pStatus->RESETS,
			pStatus->TEMPERATURE,
			pStatus->counter.ACK_ERROR,
			pStatus->counter.BIT_ERROR,
			pStatus->counter.CRC_ERROR,
			pStatus->counter.FORMAT_ERROR,
			pStatus->counter.INCON_ERROR,
			pStatus->counter.SEND_ERROR,
			pStatus->counter.STUFF_ERROR,
			pStatus->pktSent,
			pStatus->pktRecv,
			pStatus->pktSent - pStatus->pktRecv
			);
}

MODULE_REGISTER(ion);

