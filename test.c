#include "lib.h"

MODULE_DECLARE(canhcb);
MODULE_DECLARE(hsb);
MODULE_DECLARE(ion);
MODULE_DECLARE(func);
MODULE_DECLARE(eth);
MODULE_DECLARE(sv);

static char print_buf[2048];
static SEM_ID displaySem;

static int test_show_entry(int delay)
{
	/* Default to half an hour */
	if (delay <= 0)
		delay = 1800;
	
	FOREVER
	{
		semTake(displaySem, delay * sysClkRateGet());
		memset(print_buf, 0, 2048);
		lib_show(print_buf);
		logMsg(print_buf, 0,0,0,0,0,0);
		taskDelay(sysClkRateGet());
	}
	
	return 0;
}

static void lib_show_start(int delay)
{
	taskSpawn("tShow", 254, VX_SPE_TASK, 0x100000, test_show_entry, delay,0,0,0,0,0,0,0,0,0);
}

static int test_start_entry(int delay)
{	
	/* initialize lib */
	lib_init();
	
	/* Register modules */
#if 0
	hsb_register();
#endif
	canhcb_register();
	if (is_hmi())
	{
	    eth_register();
	}
	if (is_cpu())
	{
	    ion_register();
	    sv_register();
	}
	func_register();
	
	/* delay for some time */
	taskDelay(500);
	
	/* Delayed lib init */
	lib_delayed_init();
	
	/* modules start*/
	lib_start();
	
	/* semaphore initialize */
	displaySem = semBCreate(SEM_Q_PRIORITY, SEM_EMPTY);
	assert(displaySem);
	
	/* show start */
	lib_show_start(delay);
	
	/* Last stage lib init */
	lib_last_stage_init();
	return 0;
}

void test_start(int delay)
{
	taskSpawn("tStart", 255, VX_SPE_TASK, 0x100000, test_start_entry, delay,0,0,0,0,0,0,0,0,0);
}

void test_show(void)
{
	semGive(displaySem);
}

