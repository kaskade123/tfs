#include "lib.h"

#include <unistd.h>
#include <fcntl.h>
#include <sys/stat.h>

static void _temperature_print(TMPSNR_DEV_S * pDev, char * buf)
{
    char location[8] = {0};
    INT32 handler;
    INT32 temp;
    UINT32 ratio;

    handler = DeviceRequest(pDev);
    if (handler < 0)
        return;

    switch (pDev->location)
    {
    case SACDEV_TMPSNR_LOC_PROCCESSOR:
        strcpy(location, "CPU");
        break;
    case SACDEV_TMPSNR_LOC_BOARD:
        strcpy(location, "PCB");
        break;
    case SACDEV_TMPSNR_LOC_AMBIENT:
        strcpy(location, "AMBIENT");
        break;
    case SACDEV_TMPSNR_LOC_FPGA:
        strcpy(location, "FPGA");
        break;
    default:
        strcpy(location, "UNKNOWN");
        break;
    }

    TemperatureGet(handler, &temp, &ratio);

    DeviceRelease(handler);

    sprintf(buf, "%s : %d.%d\t", location, temp / ratio, temp % ratio);
}

static void _voltage_print(VOLSNR_DEV_S * pDev, char * buf)
{
    INT32 hdr;
    UINT32 vol;

    hdr = DeviceRequest(pDev);
    if (hdr < 0)
        return;

    VoltageGet(hdr, &vol);

    DeviceRelease(hdr);

    sprintf(buf, "%d/%d mV  ", vol, pDev->normal_voltage);
}

static void rh_print(char * buf)
{
    INT32 hdr;
    UINT32 rh;
    UINT32 ratio;
    RHSNR_DEV_S * pDev;
    
    pDev = DescriptionGetByType(SAC_DEVICE_TYPE_RH_SENSOR, NULL);
    hdr = DeviceRequest(pDev);
    if (hdr < 0)
        return;

    RHGet(hdr, &rh, &ratio);

    DeviceRelease(hdr);

    sprintf(buf, "RH : %d.%03d%%\t", rh / ratio, rh % ratio);
}

static void fram_print(char * buf)
{
    INT32 hdr;
    INT32 regRead, regWrite;
    MEMSPACE_DEV_S * pDev = NULL;
    
	do
	{
		pDev = DescriptionGetByType(SAC_DEVICE_TYPE_MEMSPACE, pDev);
		if (pDev && strcmp(pDev->devName, "FRAM"))
			continue;
		else
			break;
	}while(pDev != NULL);

    /* Request handler */
    hdr = DeviceRequest(pDev);
    if (hdr < 0)
        return;

    /* Update address 0 with random value */
    regWrite = rand() & 0xFF;
    if (MSRegWrite(hdr, 0, regWrite))
    {
    	sprintf(buf, "FRAM : Write Fail\n");
    	goto ends;
    }

    /* Read out and check */
    regRead = MSRegRead(hdr, 0);
    if (regRead < 0)
    {
        sprintf(buf, "FRAM : Read Fail\t");
        goto ends;
    }

    /* Verify */
    if (regRead != regWrite)
    {
        sprintf(buf, "FRAM : Read != Write\t");
        goto ends;
    }

    /* good to go */
    sprintf(buf, "FRAM : OK\t");

ends:
    DeviceRelease(hdr);
}

static void rtc_print(char * str)
{
    INT32 hdr;
    INT32 t;
    RTC_DEV_S * pDev = NULL;
    
    pDev = DescriptionGetByType(SAC_DEVICE_TYPE_RTC, NULL);
    hdr = DeviceRequest(pDev);
    if (hdr < 0)
        return;

    if (TimeGet(hdr, &t))
    {
        sprintf(str, "RTC : Failed\t");
        goto ends;
    }

    sprintf(str, "RTC : OK\t");
    
ends:
    DeviceRelease(hdr);
}

static void irigb_print(char * str)
{
    INT32 hdr;
    RTC_DEV_S * pDev = NULL;
    
    pDev = DescriptionGetByType(SAC_DEVICE_TYPE_DATETIME, NULL);
    hdr = DeviceRequest(pDev);
    if (hdr < 0)
        return;

    if (DateTimeStatus(hdr))
        sprintf(str, "IRIGB : Failed\t");
    else
    	sprintf(str, "IRIGB : OK\t");
    
    DeviceRelease(hdr);
}

static void type_print(UINT16 type, char * buf, FUNCPTR _print)
{
	void * pDev = NULL;
	int called = 0;
	
	do
	{
		pDev = DescriptionGetByType(type, pDev);
		if (pDev)
		{
			called = 1;
			_print(pDev, buf + strlen(buf));
		}
	}while(pDev != NULL);
	
	if (called)
		sprintf(buf + strlen(buf), "\n");
}

static void fs_test(char * path, char * str)
{
    int fd;
    char filename[32];
    struct stat s;
    
    sprintf(filename, "/%s/test", path);

    /* create one file in HRFS will have immediate commit */
    fd = open(filename, O_WRONLY | O_CREAT | O_TRUNC, 0666);
    if (fd <= 0)
    {
        sprintf(str, "%s : Open Fail\t", path);
        return;
    }
    
    close(fd);
    
    /* see if the file is really created */
    if (stat(filename, &s))
    {
        sprintf(str, "%s : Create Fail\t", path);
        return;
    }
    
    /* remove this file */
    if (remove(filename))
    {
        sprintf(str, "%s : Remove Fail\t", path);
        return;
    }
    
    sprintf(str, "%s : OK\t", path);
}

static void serial_test(char * str)
{
    int fd;
    char ch[128];

    fd = open("/tyCo/1", O_WRONLY | O_NOCTTY | O_NONBLOCK, 0666);
    if (fd < 0)
    {
        sprintf(str, "RS232 : Open Fail\t");
        return;
    }
    
    memset(ch, 0x55, 128);

    if (write(fd, ch, 128) != 128)
    {
        sprintf(str, "RS232 : Write Fail\t");
        goto ends;
    }

    sprintf(str, "RS232 : OK\t");

ends:
    close(fd);
}

static int dummy_load_task(void)
{
	static UINT32 dummy = 0;
    FOREVER
    {
        while(dummy < 0x1000000)
            dummy++;
        dummy = 0;
        taskDelay(10);
    }
}

static void func_start(void)
{
	/* Spawn a dummy task if CPU board */
	if (is_cpu())
	{
		assert (taskSpawn("tDummy", 255, 0, 0x200, dummy_load_task, 
				0,0,0,0,0,0,0,0,0,0) != TASK_ID_ERROR);
	}
}

static void func_show(char * buf)
{
	sprintf(buf, "\n********** BOARD **********\n");
	type_print(SAC_DEVICE_TYPE_TEMP_SENSOR, buf + strlen(buf),
			(FUNCPTR)_temperature_print);
	type_print(SAC_DEVICE_TYPE_VOL_SENSOR, buf + strlen(buf),
			(FUNCPTR)_voltage_print);
	rh_print(buf + strlen(buf));
	fram_print(buf + strlen(buf));
	rtc_print(buf + strlen(buf));
	irigb_print(buf + strlen(buf));
	fs_test("tffs", buf + strlen(buf));
	fs_test("mmc0:0", buf + strlen(buf));
	serial_test(buf + strlen(buf));
	sprintf(buf + strlen(buf), "\n");
}

MODULE_REGISTER(func);
