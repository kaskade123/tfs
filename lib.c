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
	assert(taskSpawn("tQueue", 40, VX_SPE_TASK, 0x80000, jobQueueProcess,
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
	
	if (strncmp(get_env("board"), "NPS-CPU", strlen("NPS-CPU")) == 0)
		return 1;
	else
		return 0;
}

int is_hmi(void)
{
	return !is_cpu();
}
