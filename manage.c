#include "lib.h"

#define MANAGE_DEV_NAME     "manage"
#define MANAGE_BUFFER_LEN   1600
#define MANAGE_MAX_NODE     4

#define MANAGE_BW_LIMIT     2000000     /* BW limited to 2Mbps */
#define MANAGE_PKT_LEN      1000         /* Packet Length */
#define MANAGE_TIMER_FREQ   (MANAGE_BW_LIMIT / 8 / MANAGE_PKT_LEN * MANAGE_MAX_NODE)

typedef struct manage_node
{
    UINT8 src_mac[6];
    UINT32 idx;
    UINT32 recved;
    UINT32 missing;
} MANAGE_NODE_S;

typedef struct manage_status
{
    INT32           hdr;            /* Ethernet device handler */
    SEM_ID          txSem;          /* tx task control */
    SEM_ID          rxSem;          /* rx task control */
    UINT8 *         pkt;            /* Ethernet packet buffer */
    MANAGE_NODE_S   nodes[MANAGE_MAX_NODE];  /* manage node */
    INT32           timerFd;        /* Timer Handler */
}MANAGE_STATUS_S;

static MANAGE_STATUS_S * pStatus = NULL;

static void manage_pkt_gen(int hdr, uint8_t * pkt, unsigned long len, UINT32 idx)
{
    assert(pStatus);
    assert(len >= 60);

    /* Broadcast packet */
    memset(pkt, 0xFF, 6);
    eth_srcmac_fill(hdr, pkt);
    /* Fill in index */
    memcpy(pkt + 12, &idx, sizeof(idx));
    /* Fill in data with checksum */
    assert(cksum_buf_generate((char *)pkt + 12 + sizeof(idx), len - 12 - sizeof(idx)) == 0);
}

static int manage_pkt_verify(char * buf, unsigned long len, UINT32 * idx, UINT8 * mac)
{
    assert(buf != NULL);
    assert(len >= 60);
    assert(idx != NULL);
    assert(mac != NULL);
    memcpy(mac, buf + 6, 6);
    memcpy(idx, buf + 12, sizeof(*idx));
    return cksum_buf_verify(buf + 12 + sizeof(*idx), len - 12 - sizeof(*idx));
}

static int manage_send_entry(void)
{
    int ret;
    uint32_t idx = 0;

    FOREVER
    {
        semTake(pStatus->txSem, WAIT_FOREVER);
        /* Send one pkt */
        manage_pkt_gen(pStatus->hdr, pStatus->pkt, MANAGE_PKT_LEN, idx ++);
        do
        {
            ret = EthernetSendPkt(pStatus->hdr, pStatus->pkt, MANAGE_PKT_LEN);
        }while(ret != 0);
    }
}

static BOOL is_node_empty(uint32_t idx)
{
    static uint8_t empty_mac[6] = {0};

    return (memcmp(pStatus->nodes[idx].src_mac, empty_mac, 6) == 0);
}

static BOOL is_node_match(uint32_t idx, uint8_t src_mac[6])
{
    return (memcmp(pStatus->nodes[idx].src_mac, src_mac, 6) == 0);
}

static void set_node_mac(uint32_t idx, uint8_t src_mac[6])
{
    memcpy(pStatus->nodes[idx].src_mac, src_mac, 6);
}

static MANAGE_NODE_S * get_node_from_src_mac(uint8_t src_mac[6])
{
    int i;

    assert(src_mac);

    for (i = 0; i < MANAGE_MAX_NODE; i++)
    {
        if (is_node_match(i, src_mac))
            /* found */
            return &pStatus->nodes[i];
    }

    /* new comer, get a new one */
    for (i = 0; i < MANAGE_MAX_NODE; i++)
    {
        if (is_node_empty(i))
        {
            /* found free entry */
            set_node_mac(i, src_mac);
            return &pStatus->nodes[i];
        }
    }

    return NULL;
}

static BOOL manage_recv_hook(void * pDev, UINT8 * pBuf, UINT32 bufLen)
{
    MANAGE_NODE_S * pNode;
    uint8_t src_mac[6];
    uint32_t curr_idx;

    if(manage_pkt_verify((char *)pBuf, bufLen, &curr_idx, src_mac))
        return FALSE;

    pNode = get_node_from_src_mac(src_mac);
    if (pNode == NULL)
        return FALSE;

    pNode->recved ++;

    if (pNode->recved != 1)
        pNode->missing += curr_idx - pNode->idx - 1;

    pNode->idx = curr_idx;

    return TRUE;
}

static int manage_recv_entry(void)
{
    uint32_t cnt = 0;

    assert(EthernetPktDrop(pStatus->hdr, 1024) >= 0);

    EthernetHookDisable(pStatus->hdr);
    EthernetRecvHook(pStatus->hdr, manage_recv_hook);
    EthernetHookEnable(pStatus->hdr);

    FOREVER
    {
        int ret;
        semTake(pStatus->rxSem, WAIT_FOREVER);
        do
        {
            uint32_t pktlimit = 32;
            ret = EthernetRecvPoll(pStatus->hdr, &pktlimit);
        }while(ret == -EAGAIN);

        if (++cnt >= MANAGE_MAX_NODE)
        {
            cnt = 0;
            semGive(pStatus->txSem);
        }
    }

    return 0;
}

void manage_start(void)
{
    pStatus = malloc(sizeof(*pStatus));
    assert(pStatus);
    memset(pStatus, 0, sizeof(*pStatus));

    pStatus->pkt = malloc(MANAGE_BUFFER_LEN);
    assert(pStatus->pkt);

    pStatus->hdr = ethdev_get(MANAGE_DEV_NAME);
    assert(pStatus->hdr >= 0);

    pStatus->txSem = semBCreate(SEM_Q_PRIORITY, SEM_EMPTY);
    assert(pStatus->txSem);
    pStatus->rxSem = semBCreate(SEM_Q_PRIORITY, SEM_EMPTY);
    assert(pStatus->rxSem);

    pStatus->timerFd = timer_set(MANAGE_TIMER_FREQ, pStatus->rxSem);
    assert(pStatus->timerFd >= 0);

    taskSpawn("tManageSend", 100, VX_FP_TASK, 0x4000, manage_send_entry,
            1,2,3,4,5,6,7,8,9,10);

    taskSpawn("tManageRecv", 50, VX_FP_TASK, 0x4000, manage_recv_entry,
            1,2,3,4,5,6,7,8,9,10);
}

static void manage_show(char * buf)
{
    int i;

    TimerDisable(pStatus->timerFd);
    taskDelay(1);

    snprintf(buf + strlen(buf), PRINT_BUF_SIZE - strlen(buf),
            "\n*********** MANAGE ***********\n");
    for (i = 0; i < MANAGE_MAX_NODE; i++)
    {
        MANAGE_NODE_S * pNode = &pStatus->nodes[i];
        if (is_node_empty(i))
            continue;
        snprintf(buf + strlen(buf), PRINT_BUF_SIZE - strlen(buf),
                "%02X:%02X:%02X:%02X:%02X:%02X : Recv %10d; Missing %10d\n",
                pNode->src_mac[0], pNode->src_mac[1], pNode->src_mac[2],
                pNode->src_mac[3], pNode->src_mac[4], pNode->src_mac[5],
                pNode->recved, pNode->missing);
    }

    TimerEnable(pStatus->timerFd);
}

MODULE_REGISTER(manage)

