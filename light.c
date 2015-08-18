#include "lib.h"

/*
 * Red light should be on, Green light should be blinking
 */

static int redFd;
static int greenFd;

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

void light_start(void)
{
	/* Get two light handler */
	redFd = light_get("red");
	assert(redFd >= 0);
	
	greenFd = light_get("test");
	assert(greenFd >= 0);
	
	/* Turn On Red Light */
	assert(LightOn(redFd) == 0);
	
	/* Start blink task */
	assert(taskSpawn("tLight", 30, VX_SPE_TASK, 0x4000, blink_task, greenFd, 
			0,0,0,0,0,0,0,0,0) != TASK_ID_ERROR);
}
