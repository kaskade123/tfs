#include "lib.h"

BUS_DECLARE(canhcb);
BUS_DECLARE(hsb);
BUS_DECLARE(ion);
BUS_DECLARE(func);
BUS_DECLARE(eth);

static int test_start_entry(void)
{	
	/* initialize lib */
	lib_init();
	
	/* delay for some time */
	taskDelay(400);
	
	/* Delayed lib init */
	lib_delayed_init();
	
	hsb_start();
	canhcb_start();
	ion_start();
	func_start();
	eth_start();
	
	/* Last stage lib init */
	lib_last_stage_init();
	return 0;
}

void test_start(void)
{
	taskSpawn("tStart", 255, VX_SPE_TASK, 0x100000, test_start_entry, 0,0,0,0,0,0,0,0,0,0);
}

static int test_show_entry(int delay)
{
	char print_buf[2048];
	
	if (delay > 0 && delay < 10)
	{
		FOREVER
		{
			memset(print_buf, 0, 2048);
			canhcb_show(print_buf + strlen(print_buf));
			hsb_show(print_buf + strlen(print_buf));
			ion_show(print_buf + strlen(print_buf));
			eth_show(print_buf + strlen(print_buf));
			func_show(print_buf + strlen(print_buf));
			logMsg(print_buf, 0,0,0,0,0,0);
			taskDelay(delay * sysClkRateGet());
		}
	}
	else
	{
		memset(print_buf, 0, 2048);
		canhcb_show(print_buf + strlen(print_buf));
		hsb_show(print_buf + strlen(print_buf));
		ion_show(print_buf + strlen(print_buf));
		eth_show(print_buf + strlen(print_buf));
		func_show(print_buf + strlen(print_buf));
		logMsg(print_buf, 0,0,0,0,0,0);
	}
	
	return 0;
}

void test_show(int delay)
{
	taskSpawn("tShow", 254, VX_SPE_TASK, 0x100000, test_show_entry, delay,0,0,0,0,0,0,0,0,0);
}

