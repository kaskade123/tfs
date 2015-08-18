#include "lib.h"

BUS_DECLARE(canhcb);
BUS_DECLARE(hsb);

static int test_start_entry(void)
{	
	/* initialize lib */
	lib_init();
	
	/* delay for some time */
	taskDelay(300);
	
	hsb_sender_start();
	canhcb_sender_start();
	return 0;
}

void test_start(void)
{
	taskSpawn("tStart", 255, VX_SPE_TASK, 0x100000, test_start_entry, 0,0,0,0,0,0,0,0,0,0);
}

static int test_show_entry(void)
{
	char print_buf[1024] = {0};
	
	canhcb_show(print_buf + strlen(print_buf));
	hsb_show(print_buf + strlen(print_buf));
	
	logMsg(print_buf, 0,0,0,0,0,0);
	
	return 0;
}

void test_show(void)
{
	taskSpawn("tShow", 255, VX_SPE_TASK, 0x100000, test_show_entry, 0,0,0,0,0,0,0,0,0,0);
}
