#include "lib.h"
extern void light_start(void);

void ip_setup(void)
{
	int fd = ethdev_get("backplane");
	char ip_addr[16];
	
	assert(fd >= 0);
	
	sprintf(ip_addr, "192.168.0.%d", 100 + addr_get());
	assert(EthernetIPSet(fd, ip_addr) == 0);
}

void lib_init(void)
{
	UINT32 tb, tl;
	vxTimeBaseGet(&tb, &tl);
	srand(tl);
	light_start();
	return;
}

void lib_delayed_init(void)
{
	ip_setup();
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
	static INDICATOR_DEV_S * pDev = NULL;
	
	do
	{
		pDev = DescriptionGetByType(SAC_DEVICE_TYPE_INDICATOR, pDev);
		if (pDev != NULL)
		{
			if (strcmp(pDev->color, color) == 0)
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

void rand_range(UINT8 * ptr, UINT32 size)
{
	UINT32 i;
	for (i = 0; i < size; i++)
		ptr[i] = rand();
}
