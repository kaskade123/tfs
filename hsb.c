#include "lib.h"

#define HSB_PKT_DLC_MAX     1600
#define HSB_SFP_DLC_PER_CHN 24
#define HSB_MAX_NODE        8

#define OPT_MAX_CHN         8

#define HSB_BANDWIDTH       10000000
#define HSB_SFP_CNT         24

typedef struct hsb_profiling
{
    int         hsbFd;
    int         rxTimerFd;
    SEM_ID      txSem;
    SEM_ID      rxSem;
    TASK_ID     txTask;
    TASK_ID     rxTask;
    TASK_ID     showTask;
    HSB_SEND_HEADER * txPkt;
    HSB_RECV_HEADER * rxPkt;
    uint32_t    cksumErr[4];
    uint32_t    codingErr[4];
    uint32_t    optTx[OPT_MAX_CHN];
    uint32_t    optRx[OPT_MAX_CHN];
    uint32_t    optMissing[OPT_MAX_CHN];
    uint32_t    bitErr;
    uint32_t    timingErr;
    uint32_t    arbErr;
    uint16_t    rxIdx[HSB_MAX_NODE];
    uint32_t    rxCount[HSB_MAX_NODE];
    uint32_t    rxMissing[HSB_MAX_NODE];
    uint32_t    maxRetry;
}HSB_PROFILING_S;

static HSB_PROFILING_S * pProfiling = NULL;

static void Update_Errs(void)
{
    uint32_t regVal = *(uint32_t *)0x40000300;
    int i;

    /* Write one clear */
    *(uint32_t *)0x40000300 = regVal;

    for (i = 0; i < 4; i++)
    {
        if (regVal & (0x01 << i))
            pProfiling->codingErr[i] ++;
        if (regVal & (0x10 << i))
            pProfiling->cksumErr[i] ++;
    }
    if (regVal & 0x100)
        pProfiling->arbErr ++;
    if (regVal & 0x200)
        pProfiling->timingErr ++;
    if (regVal & 0x400)
        pProfiling->bitErr ++;
}

static BOOL opt_decoder(uint8_t * data)
{
    uint8_t cnt = data[3];
    int i;

    for (i = 0; i < cnt; i++)
    {
        uint32_t t32, r32;
        t32 = *(uint32_t *)(data + (4 + 2 + i * 24));
        r32 = *(uint32_t *)(data + (4 + 6 + i * 24));
        pProfiling->optTx[i] = be32_to_cpu(t32);
        pProfiling->optRx[i] = be32_to_cpu(r32);
        pProfiling->optMissing[i] = pProfiling->optTx[i] - pProfiling->optRx[i];
    }

    return TRUE;
}

static BOOL hsb_Decoder(void * pDev, uint8_t * buf, uint32_t bufLen)
{
    HSB_RECV_HEADER * pPkt = pProfiling->rxPkt;
    uint8_t * pktData = (uint8_t *)pProfiling->rxPkt + sizeof(HSB_RECV_HEADER);
    uint16_t idx;
    uint16_t expect_idx;
    uint8_t src;

    Update_Errs();

    /* store packet locally */
    memcpy(pProfiling->rxPkt, buf, bufLen);

    pPkt->u.u32 = be32_to_cpu(pPkt->u.u32);

    /*
     * Only decode SFP Info packets
     */
    if (pktData[0] != 0x51 && pktData[0] != 0x11)
        return TRUE;

    if (pktData[0] == 0x11)
        return opt_decoder(pktData);

    /*
     * Check if SRC is a supported address
     */
    if (pPkt->u.s.SRC > HSB_MAX_NODE || pPkt->u.s.SRC == 0)
    {
        printf("pPkt->SRC = %d is invalid\n", pPkt->u.s.SRC);
        return TRUE;
    }
    src = pPkt->u.s.SRC - 1;

    /*
     * form idx from packet
     */
    idx = pktData[2];
    idx = idx << 8;
    idx |= pktData[1];

    /*
     * Validate the data
     */
    if(cksum_buf_verify((char *)&pktData[4], HSB_SFP_DLC_PER_CHN * pktData[3]))
    {
        /*
         * data validate failed, this is not our packet
         */
        return TRUE;
    }

    /*
     * Update rx counter
     */
    pProfiling->rxCount[src] ++;

    if (pProfiling->rxCount[src] == 1)
    {
        /*
         * This is the first packet, just update the idx and exit
         */
        pProfiling->rxIdx[src] = idx;
    }
    else
    {
        /*
         * Check the missing packets if there is
         */
        expect_idx = pProfiling->rxIdx[src] + 1;
        pProfiling->rxIdx[src] = idx;
        if (expect_idx != idx)
            logMsg("Exp %d, Recv %d, DLC = %d\n", expect_idx, idx, pPkt->u.s.DLC, 4,5,6);
        if (expect_idx < idx)
            pProfiling->rxMissing[src] += (idx - expect_idx);
        else if (expect_idx > idx)
            /*
             * there is a roll back in counting
             */
        pProfiling->rxMissing[src] += 0xFFFF - expect_idx + idx + 1;
    }

    return TRUE;
}

static int hsb_form_sfp_pkt(HSB_SEND_HEADER * pPkt, uint8_t priority, uint16_t dst, uint16_t idx, uint8_t sfp_count)
{
    uint8_t * pktData = (uint8_t *)pPkt + sizeof(HSB_SEND_HEADER);
    /*
     * Initialize packet header
     */
    memset(pPkt, 0, sizeof(*pPkt));

    /*
     * Prepare packet header
     */
    pPkt->dstMac[5] = 2;
    pPkt->srcMac[5] = 1;

    pPkt->u.s.PRI = priority;
    pPkt->u.s.DST = dst & 0xFFFF;

    /*
     * Calculate SFP packet DLC from sfp_count
     */
    pPkt->u.s.DLC = 4 + HSB_SFP_DLC_PER_CHN * sfp_count;

    pPkt->u.u32 = cpu_to_be32(pPkt->u.u32);

    /*
     * Form SFP packet
     */
    pktData[0] = 0x51;                    /* SFP */
    pktData[1] = idx & 0xFF;              /* INDEX(LSB) */
    pktData[2] = ((idx & 0xFF00) >> 8);   /* INDEX(MSB) */
    pktData[3] = sfp_count;               /* SFP_Count */

    /*
     * Stuff randomized data with cksum
     */
    return cksum_buf_generate((char *)&pktData[4], HSB_SFP_DLC_PER_CHN * sfp_count);
}

int hsb_display_sfp_pkt(uint32_t sfp_count)
{
    HSB_SEND_HEADER * pPkt = NULL;
    char * pBuf;
    char * display_buffer = NULL;
    int ret = 0, i;

    pPkt = malloc(1600);
    if (pPkt == NULL)
    {
        ret = -ENOMEM;
        goto exit;
    }
    memset(pPkt, 0, 1600);
    pBuf = (char *)pPkt;

    display_buffer = malloc(40960);
    if (display_buffer == NULL)
    {
        ret = -ENOMEM;
        goto exit;
    }
    memset(display_buffer, 0, 40960);

    ret = hsb_form_sfp_pkt(pPkt, 3, 0xFFFF, 0, sfp_count);
    if (ret)
        goto exit;

    for (i = 0; i < 4 + HSB_SFP_DLC_PER_CHN * sfp_count + sizeof(*pPkt); i++)
    {
        if ((i % 32) == 0)
            sprintf(display_buffer + strlen(display_buffer), "\n0x%08X : ", i);
        sprintf(display_buffer + strlen(display_buffer), "%02X ", pBuf[i]);
    }
    sprintf(display_buffer + strlen(display_buffer), "\n");
    logMsg(display_buffer, 1,2,3,4,5,6);

exit:
    if (pPkt)
        free(pPkt);
    if (display_buffer)
        free(display_buffer);
    return ret;
}

static int hsb_send_task(int fd, int priority, int dst, int sfp_count)
{
    HSB_SEND_HEADER * pPkt;
    uint16_t idx = 0;

    pPkt = pProfiling->txPkt;

    FOREVER
    {
        uint32_t retry = 0;
        semTake(pProfiling->txSem, WAIT_FOREVER);
        assert(hsb_form_sfp_pkt(pPkt, priority, dst, idx ++, sfp_count) == 0);
        while(EthernetSendPkt(fd, (uint8_t *)pPkt, 4 + HSB_SFP_DLC_PER_CHN * sfp_count + sizeof(*pPkt)))
        {
            retry ++;
            taskDelay(1);
        }
        if (retry > pProfiling->maxRetry)
            pProfiling->maxRetry = retry;
    }
}

static int hsb_recv_task(int fd)
{
    static uint32_t cnt = 0;

    /* Drop all the packets received */
    assert(EthernetPktDrop(fd, 512) >= 0);

    /* Hook the packet copier */
    assert(EthernetHookDisable(fd) == 0);
    assert(EthernetRecvHook(fd, hsb_Decoder) == 0);
    assert(EthernetHookEnable(fd) == 0);

    FOREVER
    {
        semTake(pProfiling->rxSem, WAIT_FOREVER);
        Update_Errs();
        /* Receive all pending packets */
		while (EthernetRecvPoll(fd, NULL) == -EAGAIN)
		    ;
		if (++cnt >= HSB_MAX_NODE)
		{
		    cnt = 0;
		    semGive(pProfiling->txSem);
		}
    }
}

static void hsb_start(void)
{
    pProfiling = (HSB_PROFILING_S *)malloc(sizeof(*pProfiling));
    assert(pProfiling != NULL);

    memset(pProfiling, 0, sizeof(*pProfiling));

    pProfiling->txSem = semBCreate(SEM_Q_PRIORITY, SEM_EMPTY);
    assert(pProfiling->txSem != NULL);

    pProfiling->rxSem = semBCreate(SEM_Q_PRIORITY, SEM_EMPTY);
    assert(pProfiling->rxSem != NULL);

    pProfiling->txPkt = (HSB_SEND_HEADER *)memalign(4, sizeof(*pProfiling->txPkt) + HSB_PKT_DLC_MAX);
    assert (pProfiling->txPkt != NULL);

    pProfiling->rxPkt = (HSB_RECV_HEADER *)memalign(4, sizeof(*pProfiling->rxPkt) + HSB_PKT_DLC_MAX);
    assert (pProfiling->rxPkt != NULL);

    pProfiling->hsbFd = ethdev_get("hsb");
    assert(pProfiling->hsbFd >= 0);

    /*
     * create tx and rx task
     */
    pProfiling->txTask = taskSpawn("tHsbSend", 50, VX_FP_TASK, 0x4000, hsb_send_task,
            pProfiling->hsbFd, 3, 0xFFFF, HSB_SFP_CNT,5,6,7,8,9,10);
    assert(pProfiling->txTask != TASK_ID_ERROR);
    pProfiling->rxTask = taskSpawn("tHsbRecv", 50, VX_FP_TASK, 0x4000, hsb_recv_task,
            pProfiling->hsbFd, 2,3,4,5,6,7,8,9,10);
    assert(pProfiling->rxTask != TASK_ID_ERROR);

    /*
     * calculate tx frequency and set the timer
     */
    do
    {
        uint32_t pkt_len, tx_freq, rx_freq;
        pkt_len = sizeof(HSB_SEND_HEADER) - 4 + 4 + HSB_SFP_DLC_PER_CHN * HSB_SFP_CNT + 4;
        tx_freq = HSB_BANDWIDTH / 8 / pkt_len;
        rx_freq = tx_freq * HSB_MAX_NODE;
        pProfiling->rxTimerFd = timer_set(rx_freq, pProfiling->rxSem);
        assert(pProfiling->rxTimerFd >= 0);
    }while(0);
}

static void hsb_suspend(void)
{
    if (pProfiling)
        TimerDisable(pProfiling->rxTimerFd);
}

static void hsb_resume(void)
{
    if (pProfiling)
        TimerEnable(pProfiling->rxTimerFd);
}

static void array_print_title(char * buf, const char * type, uint32_t len)
{
    uint32_t i;

    snprintf(buf + strlen(buf), PRINT_BUF_SIZE - strlen(buf),
             "%8s\t", type);
    for (i = 0; i < len; i++)
        snprintf(buf + strlen(buf), PRINT_BUF_SIZE - strlen(buf),
                "%10d\t", i + 1);
    snprintf(buf + strlen(buf), PRINT_BUF_SIZE - strlen(buf), "\n");
}

static void array_print_data(char * buf, const char * type, uint32_t * data, uint32_t len)
{
    uint32_t i;

    snprintf(buf + strlen(buf), PRINT_BUF_SIZE - strlen(buf),
            "%8s\t", type);
    for (i = 0; i < len; i++)
        snprintf(buf + strlen(buf), PRINT_BUF_SIZE - strlen(buf),
                "%10u\t", data[i]);
    snprintf(buf + strlen(buf), PRINT_BUF_SIZE - strlen(buf), "\n");
}

static void hsb_show(char * buf)
{
    assert(pProfiling != NULL);

    hsb_suspend();

    snprintf(buf + strlen(buf), PRINT_BUF_SIZE - strlen(buf),
            "\n*********** HSB ***********\n");

    /*
     * Up Time
     */
    snprintf(buf + strlen(buf), PRINT_BUF_SIZE - strlen(buf),
            "\nUpTime : %u Second(s), maxRetry = %d\n", (uint32_t) time(NULL),
            pProfiling->maxRetry);

    /*
     * FPGA statistics
     */
    snprintf(buf + strlen(buf), PRINT_BUF_SIZE - strlen(buf),
            "Send : %10d, Recv : %10d\n", *(uint32_t *)0x40000308, *(uint32_t *)0x40000304);

    /*
     * Error statistics
     */
    snprintf(buf + strlen(buf), PRINT_BUF_SIZE - strlen(buf),
            "bitErr : %10d, timingErr : %10d, arbErr : %10d\n",
            pProfiling->bitErr, pProfiling->timingErr, pProfiling->arbErr);
    array_print_title(buf, "ErrLine", 4);
    array_print_data(buf, "cksumErr", pProfiling->cksumErr, 4);
    array_print_data(buf, "codingErr", pProfiling->codingErr, 4);
    snprintf(buf + strlen(buf), PRINT_BUF_SIZE - strlen(buf), "\n");


    /*
     * Title
     */
    array_print_title(buf, "ADDRESS", HSB_MAX_NODE);
    array_print_data(buf, "RECVED", pProfiling->rxCount, HSB_MAX_NODE);
    array_print_data(buf, "MISSING", pProfiling->rxMissing, HSB_MAX_NODE);

    /*
     * OPT
     */
    snprintf(buf + strlen(buf), PRINT_BUF_SIZE - strlen(buf),
            "\n*********** OPT ***********\n");
    array_print_title(buf, "CHN", OPT_MAX_CHN);
    array_print_data(buf, "TX", pProfiling->optTx, OPT_MAX_CHN);
    array_print_data(buf, "RX", pProfiling->optRx, OPT_MAX_CHN);
    array_print_data(buf, "MISSING", pProfiling->optMissing, OPT_MAX_CHN);

    hsb_resume();
}

MODULE_REGISTER(hsb);
