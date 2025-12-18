/* Standard includes. */
#include <stdio.h>

/* Kernel includes. */
#include "tx_api.h"
#include "tx_port.h"

/* cvitek includes. */
#include "printf.h"
#include "rtos_cmdqu.h"
#include "cvi_mailbox.h"
#include "intr_conf.h"
#include "top_reg.h"
#include "memmap.h"
#include "comm.h"
#include "cvi_spinlock.h"

/* Milk-V Duo */
#include "milkv_duo_io.h"

/* SSD1306 OLED Driver */
#include "ssd1306_rtos.h"


#define __DEBUG__
#ifdef __DEBUG__
#define debug_printf printf
#else
#define debug_printf(...)
#endif

void prvQueueISR(void);
DEFINE_CVI_SPINLOCK(mailbox_lock, SPIN_MBOX);
/* mailbox parameters */
volatile struct mailbox_set_register *mbox_reg;
volatile struct mailbox_done_register *mbox_done_reg;
volatile unsigned long *mailbox_context; // mailbox buffer context is 64 Bytess

void main_cvirtos(void)
{
	//arch_usleep(1000 * 1000);
	printf("create cvi task\n");

	/* Start the tasks and timer running. */ // cvitek/driver/common/src/system.c
	request_irq(MBOX_INT_C906_2ND, prvQueueISR, 0, "mailbox", (void *)0);

	/* Enter the ThreadX kernel.  */
	tx_kernel_enter();

	/* If all is well, the scheduler will now be running, and the following
    line will never be reached.  If the following line does execute, then
    there was either insufficient FreeRTOS heap memory available for the idle
    and/or timer tasks to be created, or vTaskStartScheduler() was called from
    User mode.  See the memory management section on the FreeRTOS web site for
    more details on the FreeRTOS heap http://www.freertos.org/a00111.html.  The
    mode from which main() is called is set in the C start up code and must be
    a privileged mode (not user mode). */
	printf("cvi task end\n");

	for (;;)
		;
}

#define IS_TX_ERROR(x) \
	do{ \
		if((x) != TX_SUCCESS) \
			printf("error: %d at %s\n", __LINE__, __FILE__); \
	}while(0)

#define DEMO_STACK_SIZE 1024
#define DEMO_BYTE_POOL_SIZE configTOTAL_HEAP_SIZE

#define TX_MS_TO_TICKS( xTimeInMs )    ( (unsigned long) ( ( (unsigned long) ( xTimeInMs ) * (unsigned long) TX_TIMER_TICKS_PER_SECOND ) / (unsigned long) 1000U ) )

UCHAR byte_pool_memory[DEMO_BYTE_POOL_SIZE] __attribute__ ( (section( ".heap" )) );


void prvCmdQuRunTask(ULONG thread_input);
void thread_0_entry(ULONG thread_input);
void thread_1_entry(ULONG thread_input);
TX_THREAD thread_0;
TX_THREAD thread_1;
TX_THREAD mail_thread;
volatile int thread_0_counter = 10;
volatile int thread_1_counter = 10;
TX_BYTE_POOL byte_pool_0;

TX_QUEUE                mailbox_queue;
#define DEMO_QUEUE_SIZE         30

/* Define what the initial system looks like.  */
void tx_application_define(void *first_unused_memory)
{
	(void)first_unused_memory;

	CHAR *pointer = TX_NULL;
	UINT ret = 0;

	/* Create a byte memory pool from which to allocate the thread stacks.  */
	ret = tx_byte_pool_create(&byte_pool_0, "byte pool 0", byte_pool_memory,DEMO_BYTE_POOL_SIZE);
	IS_TX_ERROR(ret);

	
    /* Allocate the message queue.  */
	ret = tx_byte_allocate(&byte_pool_0, (VOID **) &pointer, DEMO_QUEUE_SIZE*sizeof(cmdqu_t), TX_NO_WAIT);
	IS_TX_ERROR(ret);

    /* Create the message queue */
	ret = tx_queue_create(&mailbox_queue, "mailbox_queue", sizeof(cmdqu_t), pointer, DEMO_QUEUE_SIZE*sizeof(cmdqu_t));
	IS_TX_ERROR(ret);

	/* Allocate the stack for thread 0.  */
	ret = tx_byte_allocate(&byte_pool_0, (VOID **)&pointer, DEMO_STACK_SIZE, TX_NO_WAIT);

	IS_TX_ERROR(ret);

	ret = tx_thread_create(&thread_0, "thread 0", thread_0_entry, 0, pointer,
			 DEMO_STACK_SIZE, 6, 6, 10,
			 TX_AUTO_START);
	IS_TX_ERROR(ret);
	
	ret = tx_byte_allocate(&byte_pool_0, (VOID **)&pointer, DEMO_STACK_SIZE, TX_NO_WAIT);
	IS_TX_ERROR(ret);

	ret = tx_thread_create(&thread_1, "thread 1", thread_1_entry, 99, pointer,
			 DEMO_STACK_SIZE, 6, 6, 10,
			 TX_AUTO_START);
	IS_TX_ERROR(ret);

	ret = tx_byte_allocate(&byte_pool_0, (VOID **)&pointer, DEMO_STACK_SIZE, TX_NO_WAIT);
	IS_TX_ERROR(ret);

	ret = tx_thread_create(&mail_thread, "mail thread", prvCmdQuRunTask, 0, 
			 pointer, DEMO_STACK_SIZE, 1, 1, TX_NO_TIME_SLICE, TX_AUTO_START);
	IS_TX_ERROR(ret);
}

void thread_0_entry(ULONG thread_input)
{
	(void)thread_input;

	//UINT status;

	printf("thread 0 in\n");
	double result = 0;
	while (1) {
		printf("threadx 0 running: %d\n", thread_0_counter++);

		result = result + thread_0_counter * 1.5;
		printf("float cal: %d\n", (int)result);

		tx_thread_sleep(TX_MS_TO_TICKS(4000));  // 5ms per tick(200Hz)
	}
}

void thread_1_entry(ULONG thread_input)
{
	(void)thread_input;

	printf("thread 1 in\n");
	while (1) {
		printf("threadx 1 running: %d\n", thread_1_counter++);
		tx_thread_sleep(TX_MS_TO_TICKS(8000));  
	}
}

void prvCmdQuRunTask(ULONG thread_input)
{
	/* Remove compiler warning about unused parameter. */
	(void)thread_input;

	cmdqu_t rtos_cmdq;
	cmdqu_t *cmdq;
	cmdqu_t *rtos_cmdqu_t;
	static int stop_ip = 0;
	int flags;
	int valid;
	int send_to_cpu = SEND_TO_CPU1;

	unsigned int reg_base = MAILBOX_REG_BASE;

	/* to compatible code with linux side */
	cmdq = &rtos_cmdq;
	mbox_reg = (struct mailbox_set_register *)reg_base;
	mbox_done_reg = (struct mailbox_done_register *)(reg_base + 2);
	mailbox_context = (unsigned long *)(MAILBOX_REG_BUFF);

	cvi_spinlock_init();
	printf("prvCmdQuRunTask run\n");

	for (;;) {
		//xQueueReceive(gTaskCtx[0].queHandle, &rtos_cmdq, portMAX_DELAY);
		tx_queue_receive(&mailbox_queue, &rtos_cmdq, TX_WAIT_FOREVER);

		switch (rtos_cmdq.cmd_id) {
		case CMD_DUO_LED:
			rtos_cmdq.cmd_id = CMD_DUO_LED;
			printf("recv cmd(%d) from C906B, param_ptr [0x%x]\n",
			       rtos_cmdq.cmd_id, rtos_cmdq.param_ptr);
			if (rtos_cmdq.param_ptr == DUO_LED_ON) {
				duo_led_control(1);
			} else {
				duo_led_control(0);
			}
			rtos_cmdq.param_ptr = DUO_LED_DONE;
		rtos_cmdq.resv.valid.rtos_valid = 1;
		rtos_cmdq.resv.valid.linux_valid = 0;
		printf("recv cmd(%d) from C906B...send [0x%x] to C906B\n",
		       rtos_cmdq.cmd_id, rtos_cmdq.param_ptr);
		goto send_label;
		
	case CMD_SSD1306_INIT: {
		uint8_t i2c_bus = (rtos_cmdq.param_ptr >> 16) & 0xFF;
		uint8_t lines = (rtos_cmdq.param_ptr >> 8) & 0xFF;
		uint8_t cols = rtos_cmdq.param_ptr & 0xFF;
		
		printf("[SSD1306] Init: I2C%d, %dx%d\n", i2c_bus, cols, lines);
		uint8_t result = ssd1306_rtos_init(i2c_bus, lines, cols);
		
		rtos_cmdq.param_ptr = result;
		rtos_cmdq.resv.valid.rtos_valid = 1;
		rtos_cmdq.resv.valid.linux_valid = 0;
		goto send_label;
	}
	
	case CMD_SSD1306_DEINIT:
		printf("[SSD1306] Deinit\n");
		rtos_cmdq.param_ptr = ssd1306_rtos_deinit();
		rtos_cmdq.resv.valid.rtos_valid = 1;
		rtos_cmdq.resv.valid.linux_valid = 0;
		goto send_label;
	
	case CMD_SSD1306_CLEAR:
		printf("[SSD1306] Clear screen\n");
		rtos_cmdq.param_ptr = ssd1306_rtos_clear_screen();
		rtos_cmdq.resv.valid.rtos_valid = 1;
		rtos_cmdq.resv.valid.linux_valid = 0;
		goto send_label;
	
	case CMD_SSD1306_SET_CURSOR: {
		uint8_t x = (rtos_cmdq.param_ptr >> 8) & 0xFF;
		uint8_t y = rtos_cmdq.param_ptr & 0xFF;
		
		printf("[SSD1306] Set cursor: (%d, %d)\n", x, y);
		rtos_cmdq.param_ptr = ssd1306_rtos_set_cursor(x, y);
		rtos_cmdq.resv.valid.rtos_valid = 1;
		rtos_cmdq.resv.valid.linux_valid = 0;
		goto send_label;
	}
	
	case CMD_SSD1306_WRITE_STRING: {
		// param_ptr points to shared memory with string data
		typedef struct {
			uint8_t font_size;
			uint8_t x;
			uint8_t y;
			char text[128];
		} string_data_t;
		
		string_data_t* str_data = (string_data_t*)rtos_cmdq.param_ptr;
		printf("[SSD1306] Write string at (%d,%d): %s\n", 
		       str_data->x, str_data->y, str_data->text);
		
		ssd1306_rtos_set_cursor(str_data->x, str_data->y);
		uint8_t result = ssd1306_rtos_write_string(str_data->font_size, str_data->text);
		
		rtos_cmdq.param_ptr = result;
		rtos_cmdq.resv.valid.rtos_valid = 1;
		rtos_cmdq.resv.valid.linux_valid = 0;
		goto send_label;
	}
	
	case CMD_SSD1306_DISPLAY_ONOFF:
		printf("[SSD1306] Display %s\n", rtos_cmdq.param_ptr ? "ON" : "OFF");
		rtos_cmdq.param_ptr = ssd1306_rtos_display_onoff(rtos_cmdq.param_ptr);
		rtos_cmdq.resv.valid.rtos_valid = 1;
		rtos_cmdq.resv.valid.linux_valid = 0;
		goto send_label;
	
	case CMD_SSD1306_UPDATE_DISPLAY: {
		// param_ptr points to shared memory with display data
		ssd1306_shared_data_t* display_data = (ssd1306_shared_data_t*)rtos_cmdq.param_ptr;
		
		printf("[SSD1306] Update display: %u faces, FPS=%.1f\n",
		       (unsigned int)display_data->face_count, display_data->fps);
		
		uint8_t result = ssd1306_rtos_update_display(display_data);
		
		rtos_cmdq.param_ptr = result;
		rtos_cmdq.resv.valid.rtos_valid = 1;
		rtos_cmdq.resv.valid.linux_valid = 0;
		goto send_label;
	}
	
	default:
		send_label:
			/* used to send command to linux*/
			rtos_cmdqu_t = (cmdqu_t *)mailbox_context;

			debug_printf("RTOS_CMDQU_SEND %d\n", send_to_cpu);
			debug_printf("ip_id=%d cmd_id=%d param_ptr=%x\n",
				     cmdq->ip_id, cmdq->cmd_id,
				     (unsigned int)cmdq->param_ptr);
			debug_printf("mailbox_context = %x\n", mailbox_context);
			debug_printf("linux_cmdqu_t = %x\n", rtos_cmdqu_t);
			debug_printf("cmdq->ip_id = %d\n", cmdq->ip_id);
			debug_printf("cmdq->cmd_id = %d\n", cmdq->cmd_id);
			debug_printf("cmdq->block = %d\n", cmdq->block);
			debug_printf("cmdq->para_ptr = %x\n", cmdq->param_ptr);

			drv_spin_lock_irqsave(&mailbox_lock, flags);
			if (flags == MAILBOX_LOCK_FAILED) {
				printf("[%s][%d] drv_spin_lock_irqsave failed! ip_id = %d , cmd_id = %d\n",
				       cmdq->ip_id, cmdq->cmd_id);
				break;
			}

			for (valid = 0; valid < MAILBOX_MAX_NUM; valid++) {
				if (rtos_cmdqu_t->resv.valid.linux_valid == 0 &&
				    rtos_cmdqu_t->resv.valid.rtos_valid == 0) {
					// mailbox buffer context is 4 bytes write access
					int *ptr = (int *)rtos_cmdqu_t;

					cmdq->resv.valid.rtos_valid = 1;
					*ptr = ((cmdq->ip_id << 0) |
						(cmdq->cmd_id << 8) |
						(cmdq->block << 15) |
						(cmdq->resv.valid.linux_valid
						 << 16) |
						(cmdq->resv.valid.rtos_valid
						 << 24));
					rtos_cmdqu_t->param_ptr =
						cmdq->param_ptr;
					debug_printf(
						"rtos_cmdqu_t->linux_valid = %d\n",
						rtos_cmdqu_t->resv.valid
							.linux_valid);
					debug_printf(
						"rtos_cmdqu_t->rtos_valid = %d\n",
						rtos_cmdqu_t->resv.valid
							.rtos_valid);
					debug_printf(
						"rtos_cmdqu_t->ip_id =%x %d\n",
						&rtos_cmdqu_t->ip_id,
						rtos_cmdqu_t->ip_id);
					debug_printf(
						"rtos_cmdqu_t->cmd_id = %d\n",
						rtos_cmdqu_t->cmd_id);
					debug_printf(
						"rtos_cmdqu_t->block = %d\n",
						rtos_cmdqu_t->block);
					debug_printf(
						"rtos_cmdqu_t->param_ptr addr=%x %x\n",
						&rtos_cmdqu_t->param_ptr,
						rtos_cmdqu_t->param_ptr);
					debug_printf("*ptr = %x\n", *ptr);
					// clear mailbox
					mbox_reg->cpu_mbox_set[send_to_cpu]
						.cpu_mbox_int_clr.mbox_int_clr =
						(1 << valid);
					// trigger mailbox valid to rtos
					mbox_reg->cpu_mbox_en[send_to_cpu]
						.mbox_info |= (1 << valid);
					mbox_reg->mbox_set.mbox_set =
						(1 << valid);
					break;
				}
				rtos_cmdqu_t++;
			}
			drv_spin_unlock_irqrestore(&mailbox_lock, flags);
			if (valid >= MAILBOX_MAX_NUM) {
				printf("No valid mailbox is available\n");
			}
			break;
		}
	}
}

void prvQueueISR(void)
{
	printf("prvQueueISR\n");
	unsigned char set_val;
	unsigned char valid_val;
	int i;
	cmdqu_t *cmdq;
	//BaseType_t YieldRequired = pdFALSE;
	UINT ret;

	set_val = mbox_reg->cpu_mbox_set[RECEIVE_CPU].cpu_mbox_int_int.mbox_int;

	if (set_val) {
		for (i = 0; i < MAILBOX_MAX_NUM; i++) {
			valid_val = set_val & (1 << i);

			if (valid_val) {
				cmdqu_t rtos_cmdq;
				cmdq = (cmdqu_t *)(mailbox_context) + i;

				debug_printf("mailbox_context =%x\n",
					     mailbox_context);
				debug_printf("sizeof mailbox_context =%x\n",
					     sizeof(cmdqu_t));
				/* mailbox buffer context is send from linux, clear mailbox interrupt */
				mbox_reg->cpu_mbox_set[RECEIVE_CPU]
					.cpu_mbox_int_clr.mbox_int_clr =
					valid_val;
				// need to disable enable bit
				mbox_reg->cpu_mbox_en[RECEIVE_CPU].mbox_info &=
					~valid_val;

				// copy cmdq context (8 bytes) to buffer ASAP
				*((unsigned long *)&rtos_cmdq) =
					*((unsigned long *)cmdq);
				/* need to clear mailbox interrupt before clear mailbox buffer */
				*((unsigned long *)cmdq) = 0;

				/* mailbox buffer context is send from linux*/
				if (rtos_cmdq.resv.valid.linux_valid == 1) {
					debug_printf("cmdq=%x\n", cmdq);
					debug_printf("cmdq->ip_id =%d\n",
						     rtos_cmdq.ip_id);
					debug_printf("cmdq->cmd_id =%d\n",
						     rtos_cmdq.cmd_id);
					debug_printf("cmdq->param_ptr =%x\n",
						     rtos_cmdq.param_ptr);
					debug_printf("cmdq->block =%x\n",
						     rtos_cmdq.block);
					debug_printf("cmdq->linux_valid =%d\n",
						     rtos_cmdq.resv.valid
							     .linux_valid);
					debug_printf(
						"cmdq->rtos_valid =%x\n",
						rtos_cmdq.resv.valid.rtos_valid);

					if ((ret = tx_queue_send(&mailbox_queue, &rtos_cmdq, TX_NO_WAIT)) != TX_SUCCESS)
					{
						printf("rtos cmdq send failed: %d\n", ret);
					}
					//xQueueSendFromISR(gTaskCtx[0].queHandle,
					// 		  &rtos_cmdq,
					// 		  &YieldRequired);

					//portYIELD_FROM_ISR(YieldRequired);
				} else
					printf("rtos cmdq is not valid %d, ip=%d , cmd=%d\n",
					       rtos_cmdq.resv.valid.rtos_valid,
					       rtos_cmdq.ip_id,
					       rtos_cmdq.cmd_id);
			}
		}
	}
}