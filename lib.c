#include "lib.h"
#include <drv/wdb/wdbEndPktDrv.h>
#include <inetLib.h>
#include <lstLib.h>

static LIST * pModules;

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
	int fd = ethdev_get("backplane");
	char ip_addr[16];
	struct in_addr ipAddr;
	IMPORT WDB_END_PKT_DEV *pEndPktDev;
	
	assert(fd >= 0);
	
	sprintf(ip_addr, "192.168.0.%d", 100 + addr_get());
	assert(EthernetIPSet(fd, ip_addr) == 0);
	
	ipAddr.s_addr = inet_addr(ip_addr);
	pEndPktDev->ipAddr = ipAddr;
}

static void mac_setup(void)
{
	int fd = ethdev_get("backplane");
	unsigned long ethaddr_low, ethaddr_high;
    char mac[18];
    
	assert(fd >= 0);
	
    /*
     * setting the 2nd LSB in the most significant byte of
     * the address makes it a locally administered ethernet
     * address
     */
    ethaddr_high = (rand() & 0xfeff) | 0x0200;
    ethaddr_low = rand();

    sprintf(mac, "%02lx.%02lx.%02lx.%02lx.%02lx.%02lx",
	ethaddr_high >> 8, ethaddr_high & 0xff,
	ethaddr_low >> 24, (ethaddr_low >> 16) & 0xff,
	(ethaddr_low >> 8) & 0xff, ethaddr_low & 0xff);

    /* Setup the environment and return with 1 */
    assert(EthernetMACSet(fd, mac) == 0);
    
    DeviceRelease(fd);
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
	redFd = light_get("red");
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
	
	greenFd = light_get("green");
	assert(greenFd >= 0);
		
	/* Start blink task */
	assert(taskSpawn("tLight", 30, VX_SPE_TASK, 0x4000, blink_task, greenFd, 
			0,0,0,0,0,0,0,0,0) != TASK_ID_ERROR);
}

/*
 * Record one bootup issue in /tffs/boot.log
 */
static void boot_record(void)
{
	INT32 fd = DeviceRequest(DescriptionGetByType(SAC_DEVICE_TYPE_RTC, NULL));
	INT32 rtc_time = 0;
	struct tm tm;
	FILE * fp;
	char buf[100] = {0};
	
	if (fd > 0)
	{
		TimeGet(fd, &rtc_time);
		DeviceRelease(fd);
	
		gmtime_r((time_t *)&rtc_time, &tm);
	
		strftime(buf, 100, "%Y-%m-%d %H:%M:%S", &tm);
	}
	else
		sprintf(buf, "NO-RTC-NO-TIME");
	
	fp = fopen("/tffs/boot.log", "a");
	if (fp == NULL)
		return;
	
	fprintf(fp, "%s bootup\n", buf);
	
	fclose(fp);
}

void boot_clear(void)
{
	remove("/tffs/boot.log");
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

void lib_init(void)
{
	UINT32 tb, tl;
	vxTimeBaseGet(&tb, &tl);
	srand(tl);
	list_init();
	boot_record();
	light_start();
	time_setup();
	return;
}

void lib_delayed_init(void)
{
	mac_setup();
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
    static UINT8 addr = 0xFF;
    if (addr == 0xFF)
	    addr = (*(UINT32 *)(0x80000004)) >> 16;

    return addr;
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
	
	if ((strcmp(get_env("board"), "N1101A") == 0) || (strcmp(get_env("board"), "cpu") == 0))
		return 1;
	else
		return 0;
}

int is_hmi(void)
{
	return !is_cpu();
}
