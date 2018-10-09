#include "lib.h"
#include <drv/wdb/wdbEndPktDrv.h>
#include <inetLib.h>
#include <lstLib.h>

static LIST * pModules;
static JOB_QUEUE_ID pQueue;

struct testModule
{
	NODE node;
	void (*start)(void);
	void (*show)(char *);
};

static void list_init(void)
{
	pModules = malloc(sizeof(*pModules));
	assert(pModules);

	lstInit(pModules);
}

static void ip_setup(void)
{
	int fd = ethdev_get("debug");
	char ip_addr[16];

	assert(fd >= 0);

	sprintf(ip_addr, "100.100.100.%d", 100 + addr_get());
	assert(EthernetIPSet(fd, ip_addr) == 0);
}

/* Blink in 1Hz */
static int blink_task(int fd)
{
	/* status == 0 : Off
	 * status == 1 : On
	 */
	static int status = 0;

	FOREVER
	{

		if (status)
		{
			status = 0;
			LightOff(fd);
		}
		else
		{
			status = 1;
			LightOn(fd);
		}
		taskDelay(sysClkRateGet() / 2 + 1);
	}
}

/*
 * Red light should be on
 */
static void light_start(void)
{
	int redFd;

	/* Get two light handler */
	redFd = light_get("LED1");
	assert(redFd >= 0);

	/* Turn On Red Light */
	assert(LightOn(redFd) == 0);
}

/*
 * Light blink start
 */
static void light_blink(void)
{
	int greenFd;

	greenFd = light_get("LED2");
	assert(greenFd >= 0);

	/* Start blink task */
	assert(taskSpawn("tLight", 30, VX_FP_TASK, 0x4000, blink_task, greenFd,
			0,0,0,0,0,0,0,0,0) != TASK_ID_ERROR);
}

/*
 * Add logging for all information displayed.
 */
static void info_record(void)
{
	INT32 fd = DeviceRequest(DescriptionGetByType(SAC_DEVICE_TYPE_RTC, NULL));
	INT32 rtc_time = 0;
	struct tm tm;
	INT32 logFd;
	char buf[100] = {0};

	/* Add /tffs/log into logMsg fd list */
	logFd = open("/tffs/log", O_WRONLY | O_CREAT | O_APPEND, 0666);
	if (logFd < 0)
		return;

	logFdAdd(logFd);

	/* See if we can display the time */
	if (fd > 0)
	{
		TimeGet(fd, &rtc_time);
		DeviceRelease(fd);

		gmtime_r((time_t *)&rtc_time, &tm);

		strftime(buf, 100, "%Y-%m-%d %H:%M:%S", &tm);
	}
	else
		sprintf(buf, "NO-RTC-NO-TIME");


	/* Record the bootup */
	logMsg("%s bootup\n", (int)buf, 0,0,0,0,0);
}

void time_setup(void)
{
	int fd;
	struct timeval tv;

	/* CPU board do not have RTC */
	if (is_cpu())
		return;

	/* get rtc time */
	fd = DeviceRequest(DescriptionGetByType(SAC_DEVICE_TYPE_RTC, NULL));
	if (fd < 0)
		return;

	assert (TimeGet(fd, (int *)&tv.tv_sec) == 0);
	tv.tv_usec = 0;

	assert (settimeofday(&tv, NULL) == OK);
}

void queue_init(void)
{
	pQueue = jobQueueCreate(NULL);
	assert(pQueue != NULL);
	assert(taskSpawn("tQueue", 40, VX_FP_TASK, 0x80000, jobQueueProcess,
			(int)pQueue, 0,0,0,0,0,0,0,0,0) != TASK_ID_ERROR);
}

STATUS queue_add(QJOB * pJob)
{
	return jobQueuePost(pQueue, pJob);
}

void lib_init(void)
{
	UINT32 tb, tl;
	vxTimeBaseGet(&tb, &tl);
	srand(tl);
	list_init();
	info_record();
	light_start();
	time_setup();
	queue_init();
	return;
}

void lib_delayed_init(void)
{
	ip_setup();
}

void lib_last_stage_init(void)
{
	light_blink();
}

int ethdev_get(const char * name)
{
	SAC_DEV_HEADER_ID pDev = NULL;

	do
	{
		pDev = DescriptionGetByType(SAC_DEVICE_TYPE_ETHERNET, pDev);
		if (pDev)
		{
			ETHERNET_DEV_S *pEthDev = (ETHERNET_DEV_S *)pDev;
			if (strcmp(pEthDev->name, name) == 0)
				return DeviceRequest(pDev);
		}
	}while(pDev);

	return -ENOENT;
}

int status_get(UINT32 status_type)
{
    SAC_DEV_HEADER_ID pDev = NULL;

    do
    {
        pDev = DescriptionGetByType(SAC_DEVICE_TYPE_STATUS, pDev);
        if (pDev)
        {
            STATUS_DEV_S *pSDev = (STATUS_DEV_S *)pDev;
            if (pSDev->type == status_type)
                return DeviceRequest(pDev);
        }
    }while(pDev);

    return -ENOENT;
}

int status_chg(UINT32 status_type, UINT32 assert)
{
    int fd = status_get(status_type);
    int ret;

    if (fd < 0)
        return fd;

    if (assert)
        ret = StatusAssert(fd);
    else
        ret = StatusDessert(fd);

    if (ret)
        return ret;

    ret = DeviceRelease(fd);
    if (ret)
        return ret;

    return 0;
}

int status_sget(UINT32 status_type)
{
    int fd = status_get(status_type);
    int ret;
    int status;

    if (fd < 0)
        return fd;

    ret = StatusGet(fd);
    if (ret < 0)
        return ret;

    status = ret;

    ret = DeviceRelease(fd);
    if (ret)
        return ret;

    return status;
}

int status_chg_verify(UINT32 status_type, UINT32 status_ret_type, UINT32 assert)
{
    int ret = status_chg(status_type, assert);

    if (ret)
        return ret;

    ret = status_sget(status_ret_type);
    if (assert)
    {
        if (ret != SAC_STATUS_ASSERT)
            return -EFAULT;
    }
    else
    {
        if (ret != SAC_STATUS_DESSERT)
            return -EFAULT;
    }

    return 0;
}

int canhcbdev_get(void)
{
	void * pDev;
	do
	{
		pDev = DescriptionGetByType(SAC_DEVICE_TYPE_CANHCB, NULL);
		if (pDev != NULL)
			return DeviceRequest(pDev);
	}while(pDev != NULL);

	return -ENOENT;
}

UINT8 addr_get(void)
{
    FPGA_DEV_S * pDev;

    pDev = DescriptionGetByType(SAC_DEVICE_TYPE_FPGA, NULL);

    return pDev->addr;
}

int light_get(char * color)
{
	INDICATOR_DEV_S * pDev = NULL;

	do
	{
		pDev = DescriptionGetByType(SAC_DEVICE_TYPE_INDICATOR, pDev);
		if (pDev != NULL)
		{
			if ((strlen(pDev->color) == strlen(color)) &&
				(strcmp(pDev->color, color) == 0))
				return DeviceRequest(pDev);
		}
	}while(pDev != NULL);

	return -ENOENT;
}

int timer_get(void)
{
	static SAC_DEV_HEADER_ID pDev = NULL;
	int fd;

	do
	{
		pDev = DescriptionGetByType(SAC_DEVICE_TYPE_TIMER, pDev);
		if (pDev != NULL)
		{
			fd = DeviceRequest(pDev);
			if (fd >= 0)
				return fd;
		}
	}while(pDev != NULL);

	return -ENOENT;
}

int iondev_get(void)
{
	SAC_DEV_HEADER_ID pDev = NULL;

	do
	{
		pDev = DescriptionGetByType(SAC_DEVICE_TYPE_ION, pDev);
		if (pDev)
			return DeviceRequest(pDev);
	}while(pDev);

	return -ENOENT;
}

void rand_range(UINT8 * ptr, UINT32 size)
{
	UINT32 i;
	for (i = 0; i < size; i++)
		ptr[i] = rand();
}

void moduleReg(void (*start)(void), void (*show)(char *))
{
	struct testModule * p;

	p = malloc(sizeof(*p));
	assert(p);
	memset(p, 0, sizeof(*p));

	p->start = start;
	p->show = show;

	lstAdd(pModules, &p->node);
}

void lib_start()
{
	struct testModule * p = (struct testModule *)lstFirst(pModules);

	while (p != NULL)
	{
		p->start();
		p = (struct testModule *)lstNext((NODE *)p);
	}
}

void lib_show(char * buf)
{
	struct testModule * p = (struct testModule *)lstFirst(pModules);
	struct timeval tv;

	gettimeofday(&tv, NULL);

	sprintf(buf, "\n%s\n", ctime((const time_t *)&tv.tv_sec));

	while (p != NULL)
	{
		p->show(buf + strlen(buf));
		p = (struct testModule *)lstNext((NODE *)p);
	}
}

int is_cpu(void)
{
	extern char * get_env(char *);

	if (strncmp(get_env("board"), "NPS-CPU", strlen("NPS-CPU")) == 0)
		return 1;
	else
		return 0;
}

int is_hmi(void)
{
	return !is_cpu();
}

void calc_fletcher32(unsigned char *data, unsigned n_bytes,
        unsigned * cksum)
{
    unsigned short * d = (unsigned short *)data;
    unsigned n_words = n_bytes / 2;
    unsigned sum1 = 0xffff, sum2 = 0xffff;
    unsigned tlen;

    if (n_bytes % 2)
        return;

    while(n_words)
    {
        tlen = n_words >= 359 ? 359 : n_words;
        n_words -= tlen;
        do
        {
            sum2 += sum1 += *d++;
        }while(--tlen);
        sum1 = (sum1 & 0xffff) + (sum1 >> 16);
        sum2 = (sum2 & 0xffff) + (sum2 >> 16);
    }
    sum1 = (sum1 & 0xffff) + (sum1 >> 16);
    sum2 = (sum2 & 0xffff) + (sum2 >> 16);

    *cksum = sum2 << 16 | sum1;
}

int hsb_remote_reg_config(UINT16 addr, UINT32 regAddr, UINT32 regVal)
{
    HSB_SEND_HEADER * pHdr;
    UINT8 * pPointer;
    UINT32 *pUINT32;
    UINT8 * pPkt;
    INT32 hdr = ethdev_get("hsb");
    int ret;

    assert(hdr >= 0);

    pPkt = malloc(1600);
    assert(pPkt != NULL);
    memset(pPkt, 0, 1600);

    pHdr = (HSB_SEND_HEADER *)pPkt;

    pHdr->dstMac[5] = 2;
    pHdr->srcMac[5] = 1;
    pHdr->u.s.PRI = 3;
    pHdr->u.s.DST = 0x01 << addr;
    pPointer = pPkt + sizeof(*pHdr);
    *pPointer++ = 0x02;     /* Config */
    *pPointer++ = rand();   /* Index */
    *pPointer++ = rand();
    *pPointer++ = 1;        /* Number */
    pUINT32 = (UINT32 *)pPointer;
    *pUINT32++ = cpu_to_be32(regAddr | 0x0F000000);
    *pUINT32++ = cpu_to_be32(regVal);
    pPointer = (UINT8 *)pUINT32;
    pHdr->u.s.DLC = pPointer - pPkt - sizeof(*pHdr);

    pHdr->u.u32 = cpu_to_be32(pHdr->u.u32);

    ret = EthernetSendPkt(hdr, pPkt, pPointer - pPkt);
    free(pPkt);
    DeviceRelease(hdr);
    return ret;
}

static const uint32_t crc_table[256] = {
tole(0x00000000L), tole(0x77073096L), tole(0xee0e612cL), tole(0x990951baL),
tole(0x076dc419L), tole(0x706af48fL), tole(0xe963a535L), tole(0x9e6495a3L),
tole(0x0edb8832L), tole(0x79dcb8a4L), tole(0xe0d5e91eL), tole(0x97d2d988L),
tole(0x09b64c2bL), tole(0x7eb17cbdL), tole(0xe7b82d07L), tole(0x90bf1d91L),
tole(0x1db71064L), tole(0x6ab020f2L), tole(0xf3b97148L), tole(0x84be41deL),
tole(0x1adad47dL), tole(0x6ddde4ebL), tole(0xf4d4b551L), tole(0x83d385c7L),
tole(0x136c9856L), tole(0x646ba8c0L), tole(0xfd62f97aL), tole(0x8a65c9ecL),
tole(0x14015c4fL), tole(0x63066cd9L), tole(0xfa0f3d63L), tole(0x8d080df5L),
tole(0x3b6e20c8L), tole(0x4c69105eL), tole(0xd56041e4L), tole(0xa2677172L),
tole(0x3c03e4d1L), tole(0x4b04d447L), tole(0xd20d85fdL), tole(0xa50ab56bL),
tole(0x35b5a8faL), tole(0x42b2986cL), tole(0xdbbbc9d6L), tole(0xacbcf940L),
tole(0x32d86ce3L), tole(0x45df5c75L), tole(0xdcd60dcfL), tole(0xabd13d59L),
tole(0x26d930acL), tole(0x51de003aL), tole(0xc8d75180L), tole(0xbfd06116L),
tole(0x21b4f4b5L), tole(0x56b3c423L), tole(0xcfba9599L), tole(0xb8bda50fL),
tole(0x2802b89eL), tole(0x5f058808L), tole(0xc60cd9b2L), tole(0xb10be924L),
tole(0x2f6f7c87L), tole(0x58684c11L), tole(0xc1611dabL), tole(0xb6662d3dL),
tole(0x76dc4190L), tole(0x01db7106L), tole(0x98d220bcL), tole(0xefd5102aL),
tole(0x71b18589L), tole(0x06b6b51fL), tole(0x9fbfe4a5L), tole(0xe8b8d433L),
tole(0x7807c9a2L), tole(0x0f00f934L), tole(0x9609a88eL), tole(0xe10e9818L),
tole(0x7f6a0dbbL), tole(0x086d3d2dL), tole(0x91646c97L), tole(0xe6635c01L),
tole(0x6b6b51f4L), tole(0x1c6c6162L), tole(0x856530d8L), tole(0xf262004eL),
tole(0x6c0695edL), tole(0x1b01a57bL), tole(0x8208f4c1L), tole(0xf50fc457L),
tole(0x65b0d9c6L), tole(0x12b7e950L), tole(0x8bbeb8eaL), tole(0xfcb9887cL),
tole(0x62dd1ddfL), tole(0x15da2d49L), tole(0x8cd37cf3L), tole(0xfbd44c65L),
tole(0x4db26158L), tole(0x3ab551ceL), tole(0xa3bc0074L), tole(0xd4bb30e2L),
tole(0x4adfa541L), tole(0x3dd895d7L), tole(0xa4d1c46dL), tole(0xd3d6f4fbL),
tole(0x4369e96aL), tole(0x346ed9fcL), tole(0xad678846L), tole(0xda60b8d0L),
tole(0x44042d73L), tole(0x33031de5L), tole(0xaa0a4c5fL), tole(0xdd0d7cc9L),
tole(0x5005713cL), tole(0x270241aaL), tole(0xbe0b1010L), tole(0xc90c2086L),
tole(0x5768b525L), tole(0x206f85b3L), tole(0xb966d409L), tole(0xce61e49fL),
tole(0x5edef90eL), tole(0x29d9c998L), tole(0xb0d09822L), tole(0xc7d7a8b4L),
tole(0x59b33d17L), tole(0x2eb40d81L), tole(0xb7bd5c3bL), tole(0xc0ba6cadL),
tole(0xedb88320L), tole(0x9abfb3b6L), tole(0x03b6e20cL), tole(0x74b1d29aL),
tole(0xead54739L), tole(0x9dd277afL), tole(0x04db2615L), tole(0x73dc1683L),
tole(0xe3630b12L), tole(0x94643b84L), tole(0x0d6d6a3eL), tole(0x7a6a5aa8L),
tole(0xe40ecf0bL), tole(0x9309ff9dL), tole(0x0a00ae27L), tole(0x7d079eb1L),
tole(0xf00f9344L), tole(0x8708a3d2L), tole(0x1e01f268L), tole(0x6906c2feL),
tole(0xf762575dL), tole(0x806567cbL), tole(0x196c3671L), tole(0x6e6b06e7L),
tole(0xfed41b76L), tole(0x89d32be0L), tole(0x10da7a5aL), tole(0x67dd4accL),
tole(0xf9b9df6fL), tole(0x8ebeeff9L), tole(0x17b7be43L), tole(0x60b08ed5L),
tole(0xd6d6a3e8L), tole(0xa1d1937eL), tole(0x38d8c2c4L), tole(0x4fdff252L),
tole(0xd1bb67f1L), tole(0xa6bc5767L), tole(0x3fb506ddL), tole(0x48b2364bL),
tole(0xd80d2bdaL), tole(0xaf0a1b4cL), tole(0x36034af6L), tole(0x41047a60L),
tole(0xdf60efc3L), tole(0xa867df55L), tole(0x316e8eefL), tole(0x4669be79L),
tole(0xcb61b38cL), tole(0xbc66831aL), tole(0x256fd2a0L), tole(0x5268e236L),
tole(0xcc0c7795L), tole(0xbb0b4703L), tole(0x220216b9L), tole(0x5505262fL),
tole(0xc5ba3bbeL), tole(0xb2bd0b28L), tole(0x2bb45a92L), tole(0x5cb36a04L),
tole(0xc2d7ffa7L), tole(0xb5d0cf31L), tole(0x2cd99e8bL), tole(0x5bdeae1dL),
tole(0x9b64c2b0L), tole(0xec63f226L), tole(0x756aa39cL), tole(0x026d930aL),
tole(0x9c0906a9L), tole(0xeb0e363fL), tole(0x72076785L), tole(0x05005713L),
tole(0x95bf4a82L), tole(0xe2b87a14L), tole(0x7bb12baeL), tole(0x0cb61b38L),
tole(0x92d28e9bL), tole(0xe5d5be0dL), tole(0x7cdcefb7L), tole(0x0bdbdf21L),
tole(0x86d3d2d4L), tole(0xf1d4e242L), tole(0x68ddb3f8L), tole(0x1fda836eL),
tole(0x81be16cdL), tole(0xf6b9265bL), tole(0x6fb077e1L), tole(0x18b74777L),
tole(0x88085ae6L), tole(0xff0f6a70L), tole(0x66063bcaL), tole(0x11010b5cL),
tole(0x8f659effL), tole(0xf862ae69L), tole(0x616bffd3L), tole(0x166ccf45L),
tole(0xa00ae278L), tole(0xd70dd2eeL), tole(0x4e048354L), tole(0x3903b3c2L),
tole(0xa7672661L), tole(0xd06016f7L), tole(0x4969474dL), tole(0x3e6e77dbL),
tole(0xaed16a4aL), tole(0xd9d65adcL), tole(0x40df0b66L), tole(0x37d83bf0L),
tole(0xa9bcae53L), tole(0xdebb9ec5L), tole(0x47b2cf7fL), tole(0x30b5ffe9L),
tole(0xbdbdf21cL), tole(0xcabac28aL), tole(0x53b39330L), tole(0x24b4a3a6L),
tole(0xbad03605L), tole(0xcdd70693L), tole(0x54de5729L), tole(0x23d967bfL),
tole(0xb3667a2eL), tole(0xc4614ab8L), tole(0x5d681b02L), tole(0x2a6f2b94L),
tole(0xb40bbe37L), tole(0xc30c8ea1L), tole(0x5a05df1bL), tole(0x2d02ef8dL)
};

#define DO_CRC(x) crc = tab[((crc >> 24) ^ (x)) & 255] ^ (crc << 8)

static uint32_t crc32_no_comp(uint32_t crc, const char *buf, uint32_t len)
{
    const uint32_t *tab = crc_table;
    const uint32_t *b =(const uint32_t *)buf;
    size_t rem_len;
    crc = cpu_to_le32(crc);
    /* Align it */
    if (((long)b) & 3 && len) {
     uint8_t *p = (uint8_t *)b;
     do {
          DO_CRC(*p++);
     } while ((--len) && ((long)p)&3);
     b = (uint32_t *)p;
    }

    rem_len = len & 3;
    len = len >> 2;
    for (--b; len; --len) {
     /* load data 32 bits wide, xor data 32 bits wide. */
     crc ^= *++b; /* use pre increment for speed */
     DO_CRC(0);
     DO_CRC(0);
     DO_CRC(0);
     DO_CRC(0);
    }
    len = rem_len;
    /* And the last few bytes */
    if (len) {
     uint8_t *p = (uint8_t *)(b + 1) - 1;
     do {
          DO_CRC(*++p); /* use pre increment for speed */
     } while (--len);
    }

    return le32_to_cpu(crc);
}
#undef DO_CRC

static uint32_t crc32 (uint32_t crc, const char *p, uint32_t len)
{
    return crc32_no_comp(crc ^ 0xffffffffL, p, len) ^ 0xffffffffL;
}

static void rand_buf(char * buf, unsigned int size)
{
    unsigned int i;
    for (i = 0; i < size; i++)
        buf[i] = rand() % 256;
}

int cksum_buf_generate(char * buf, uint32_t bufLen)
{
    uint32_t crc = 0;
    /*
     * Argument check
     */
    if (bufLen <= 4)
        return -ENOSPC;
    if (buf == NULL)
        return -EINVAL;

    /*
     * Randomize data
     */
    rand_buf(buf + 4, bufLen - 4);

    /*
     * Generate cksum
     */
    crc = crc32(crc, buf+4, bufLen - 4);

    /*
     * Store cksum
     */
    memcpy(buf, &crc, sizeof(crc));

    return 0;
}

int cksum_buf_verify(char * buf, uint32_t bufLen)
{
    uint32_t crc = 0;
    uint32_t crc_inside;
    /*
     * Argument check
     */
    if (bufLen <= 4)
        return -ENOSPC;
    if (buf == NULL)
        return -EINVAL;

    /*
     * Generate cksum
     */
    crc = crc32(crc, buf+4, bufLen - 4);

    /*
     * Compare cksum
     */
    memcpy(&crc_inside, buf, sizeof(crc_inside));

    if (crc_inside == crc)
        return 0;
    else
        return -EFAULT;
}


static void timer_hook_give_sem(int arg)
{
    SEM_ID giveSem = (SEM_ID)arg;
    assert(giveSem != NULL);
    semGive(giveSem);
}

int timer_set(uint32_t freq, SEM_ID giveSem)
{
    int fd;
    int ret;

    fd = timer_get();
    if (fd < 0)
        return -EMFILE;

    ret = TimerDisable(fd);
    if (ret)
        goto fail;
    ret = TimerFreqSet(fd, freq);
    if (ret)
        goto fail;
    ret = TimerISRSet(fd, timer_hook_give_sem, (int)giveSem);
    if (ret)
        goto fail;
    ret = TimerEnable(fd);
    if (ret)
        goto fail;

    return fd;
fail:
    DeviceRelease(fd);
    return ret;
}
