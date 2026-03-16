/* Standard includes. */
#include <stdio.h>
#include <string.h>

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

/* RPMsg-Lite */
#include "rpmsg_lite.h"
#include "rpmsg_queue.h"
#include "rpmsg_ns.h"
#include "rpmsg_env.h"
#include "rpmsg_static_config.h"

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


static struct rpmsg_lite_instance g_rpmsg_ctxt;      /* RPMsg instance context */
static struct rpmsg_lite_ept_static_context g_ept_ctxt;  /* Endpoint context */
static  rpmsg_static_queue_ctxt g_queue_ctxt;   /* Queue context */

/* Global handles to prevent stack corruption */
static struct rpmsg_lite_instance *g_rpmsg_inst = NULL;
static struct rpmsg_lite_endpoint *g_rpmsg_ept = NULL;
static rpmsg_queue_handle g_rpmsg_queue = NULL;

/* 
 * Size: 2 * RL_BUFFER_COUNT * sizeof(rpmsg_queue_rx_cb_data_t)
 * rpmsg_queue_rx_cb_data_t = 12 bytes (src:4 + data:4 + len:4)
 * 2 * 64 * 12 = 1536 bytes
 */
static uint8_t g_queue_storage[2 * RPMSG_BUFFER_COUNT * sizeof(rpmsg_queue_rx_cb_data_t)] __attribute__((aligned(4)));


static volatile int g_early_kick_received = 0;  /* Flag: Linux kick before RPMsg init */

/* 
 * Static buffers for RPMsg task to prevent stack overflow
 * Moving these from stack to static reduces stack usage by ~1KB
 */
static char g_rx_buffer[512];
static char g_tx_buffer[512];

/**
 * comm_mailbox_send_rpmsg_kick - Send RPMsg VirtIO kick to Linux via mailbox
 * @vq_id: VirtQueue ID (0 or 1)
 * 
 * This is the unified interface for RPMsg-Lite to send VirtIO kicks to Linux.
 * It properly handles ThreadX hardware spinlock and mailbox context setup.
 * 
 * Returns: 0 on success, -1 on failure
 */
int comm_mailbox_send_rpmsg_kick(uint32_t vq_id)
{
	int flags;
	int valid;

	cmdqu_t *rtos_cmdqu_t;
	rtos_cmdqu_t = (cmdqu_t *)mailbox_context;
	
	drv_spin_lock_irqsave(&mailbox_lock, flags);
	if (flags == MAILBOX_LOCK_FAILED) {
		printf("[comm_mailbox] ERROR: Failed to acquire spinlock for RPMsg kick vq_id=%d\n", (int)vq_id);
		return -1;
	}
	
	for (valid = 0; valid < MAILBOX_MAX_NUM; valid++) {
		if (rtos_cmdqu_t->resv.valid.linux_valid == 0 &&
		    rtos_cmdqu_t->resv.valid.rtos_valid == 0) {

			/* Found free slot - fill in RPMsg kick context */
			int *ptr = (int *)rtos_cmdqu_t;
			
			/* Debug: Print mailbox state before sending */
			printf("[comm_mailbox] Slot %d: Before send - mbox_en=0x%x mbox_set=0x%x\n",
			       valid,
			       mbox_reg->cpu_mbox_en[SEND_TO_CPU1].mbox_info,
			       mbox_reg->mbox_set.mbox_set);
			
			/* Slot format: ip_id=4 (IP_RGN/IP_RPMSG_KICK), cmd_id=vq_id */
			*ptr = ((4 << 0) |           /* ip_id = 4 (IP_RGN = IP_RPMSG_KICK) */
			        (vq_id << 8) |        /* cmd_id = vq_id */
			        (0 << 15) |           /* block = 0 */
			        (0 << 16) |           /* linux_valid = 0 */
			        (1 << 24));           /* rtos_valid = 1 */
			
			rtos_cmdqu_t->param_ptr = 0; /* No param for RPMsg kick */
			
			/* Memory barrier to ensure data is written before triggering interrupt */
			__asm__ volatile("fence ow, ow" ::: "memory");
			
			/* Clear and trigger mailbox interrupt */
			mbox_reg->cpu_mbox_set[SEND_TO_CPU1].cpu_mbox_int_clr.mbox_int_clr = (1 << valid);
			mbox_reg->cpu_mbox_en[SEND_TO_CPU1].mbox_info |= (1 << valid);
			mbox_reg->mbox_set.mbox_set = (1 << valid);
			
			/* Debug: Print mailbox state after sending */
			// printf("[comm_mailbox] Slot %d: After send - mbox_en=0x%x mbox_set=0x%x int=0x%x\n",
			//        valid,
			//        mbox_reg->cpu_mbox_en[SEND_TO_CPU1].mbox_info,
			//        mbox_reg->mbox_set.mbox_set,
			//        mbox_reg->cpu_mbox_set[SEND_TO_CPU1].cpu_mbox_int_int.mbox_int);
			
			/* Debug: Read back the target CPU's view (Linux = CPU1) */
			// printf("[comm_mailbox] Linux view: en=0x%x int=0x%x\n",
			//        mbox_reg->cpu_mbox_en[1].mbox_info,
			//        mbox_reg->cpu_mbox_set[1].cpu_mbox_int_int.mbox_int);
			
			break;
		}
		rtos_cmdqu_t++;
	}
	
	drv_spin_unlock_irqrestore(&mailbox_lock, flags);
	if (valid >= MAILBOX_MAX_NUM) {
		printf("[comm_mailbox] ERROR: No available mailbox slot for RPMsg kick vq_id=%d\n", (int)vq_id);
		return -1;
	}
	
	printf("[comm_mailbox] Sent RPMsg kick: vq_id=%d, slot=%d\n", (int)vq_id, valid);
	return 0;
}

void main_cvirtos(void)
{
	int i;
	
	//arch_usleep(1000 * 1000);
	cvi_spinlock_init();
	printf("create cvi task\n");
	
	/* Initialize mailbox hardware registers */
	mbox_reg = (struct mailbox_set_register *)MAILBOX_REG_BASE;
	mbox_done_reg = (struct mailbox_done_register *)(MAILBOX_REG_BASE + 2);
	mailbox_context = (unsigned long *)(MAILBOX_REG_BUFF);
	

	 //clear RTOS channel (RECEIVE_CPU = 2 = C906L)
	for (i = 0; i < MAILBOX_MAX_NUM; i++) {
		mbox_reg->cpu_mbox_set[RECEIVE_CPU].cpu_mbox_int_clr.mbox_int_clr = (1 << i);
		mbox_reg->cpu_mbox_en[RECEIVE_CPU].mbox_info &= ~(1 << i);
	}

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

#define DEMO_STACK_SIZE 2048
#define DEMO_BYTE_POOL_SIZE configTOTAL_HEAP_SIZE

#define TX_MS_TO_TICKS( xTimeInMs )    ( (unsigned long) ( ( (unsigned long) ( xTimeInMs ) * (unsigned long) TX_TIMER_TICKS_PER_SECOND ) / (unsigned long) 1000U ) )

UCHAR byte_pool_memory[DEMO_BYTE_POOL_SIZE] __attribute__ ( (section( ".heap" )) );


void prvCmdQuRunTask(ULONG thread_input);
void prvRpmsgTask(ULONG thread_input);
void prvLedBlinkTask(ULONG thread_input);
TX_THREAD mail_thread;
TX_THREAD rpmsg_thread;
TX_THREAD led_blink_thread;
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


	ret = tx_byte_allocate(&byte_pool_0, (VOID **)&pointer, DEMO_STACK_SIZE, TX_NO_WAIT);
	IS_TX_ERROR(ret);

	ret = tx_thread_create(&mail_thread, "mail thread", prvCmdQuRunTask, 0, 
			 pointer, DEMO_STACK_SIZE, 1, 1, TX_NO_TIME_SLICE, TX_AUTO_START);
	IS_TX_ERROR(ret);

	/* Create RPMsg task with much larger stack for I2C operations (32KB) */
	ret = tx_byte_allocate(&byte_pool_0, (VOID **)&pointer, DEMO_STACK_SIZE * 16, TX_NO_WAIT);
	IS_TX_ERROR(ret);

	ret = tx_thread_create(&rpmsg_thread, "rpmsg thread", prvRpmsgTask, 0,
			 pointer, DEMO_STACK_SIZE * 16, 5, 5, TX_NO_TIME_SLICE, TX_AUTO_START);
	IS_TX_ERROR(ret);
	
	printf("RPMsg task created\n");
	
	/* Initialize SSD1306 early in main thread (safer, larger stack) */
	printf("[RTOS] Early SSD1306 initialization...\n");
	ret = ssd1306_rtos_init(1, 64, 128);  /* I2C1, 64 lines, 128 columns */
	if (ret == 0) {
		printf("[RTOS] SSD1306 initialized successfully\n");
	} else {
		printf("[RTOS] SSD1306 init failed: %d\n", ret);
	}
	
	/* Create LED blink task for heartbeat */
	ret = tx_byte_allocate(&byte_pool_0, (VOID **)&pointer, DEMO_STACK_SIZE, TX_NO_WAIT);
	IS_TX_ERROR(ret);

	ret = tx_thread_create(&led_blink_thread, "led blink", prvLedBlinkTask, 0,
			 pointer, DEMO_STACK_SIZE, 10, 10, TX_NO_TIME_SLICE, TX_AUTO_START);
	IS_TX_ERROR(ret);
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

	cmdq = &rtos_cmdq;
	
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
		
		/* ⭐ CRITICAL: Copy data from shared memory IMMEDIATELY before it gets overwritten */
		string_data_t local_data;
		string_data_t* str_data = (string_data_t*)rtos_cmdq.param_ptr;
		memcpy(&local_data, str_data, sizeof(string_data_t));
		
		printf("[SSD1306] Write string at (%d,%d): %s\n", 
		       local_data.x, local_data.y, local_data.text);
		
		ssd1306_rtos_set_cursor(local_data.x, local_data.y);
		uint8_t result = ssd1306_rtos_write_string(local_data.font_size, local_data.text);
		
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
		/* ⭐ CRITICAL: Copy entire display data (1024 + 8 bytes) from shared memory */
		static ssd1306_shared_data_t local_display_data;
		ssd1306_shared_data_t* display_data = (ssd1306_shared_data_t*)rtos_cmdq.param_ptr;
		memcpy(&local_display_data, display_data, sizeof(ssd1306_shared_data_t));
		
		printf("[SSD1306] Update display: %d faces, FPS=%.1f\n",
		       (unsigned int)local_display_data.face_count, local_display_data.fps);
		
		uint8_t result = ssd1306_rtos_update_display(&local_display_data);
		
		rtos_cmdq.param_ptr = result;
		rtos_cmdq.resv.valid.rtos_valid = 1;
		rtos_cmdq.resv.valid.linux_valid = 0;
		goto send_label;
	}
	default:
		send_label:
			/* used to send command to linux*/
			rtos_cmdqu_t = (cmdqu_t *)mailbox_context;

			debug_printf("RTOS_CMDQU_SEND %d\n", SEND_TO_CPU1);
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
					mbox_reg->cpu_mbox_set[SEND_TO_CPU1]
						.cpu_mbox_int_clr.mbox_int_clr =
						(1 << valid);
					// trigger mailbox valid to rtos
					mbox_reg->cpu_mbox_en[SEND_TO_CPU1]
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

void prvRpmsgTask(ULONG thread_input)
{
	(void)thread_input;
	
	/* Use global handles instead of local variables to prevent stack corruption */
	struct rpmsg_lite_instance *rpmsg_inst;  /* Local temp for initialization */
	
	/* Use static buffers instead of stack allocation to prevent overflow */
	char *rx_buffer = g_rx_buffer;
	char *tx_buffer = g_tx_buffer;
	uint32_t rx_len;
	uint32_t remote_addr;
	/* Use static counters to prevent stack corruption */
	static int msg_sent = 0;
	static int msg_received = 0;
	static int loop_count = 0;
	int32_t ret;
	
	printf("\n");
	printf("============================================\n");
	printf("[RTOS] RPMsg Demo - Static Memory Mode\n");
	printf("============================================\n");
	printf("[RTOS] Shared memory base: 0x%08lx\n", RPMSG_SHMEM_BASE);
	printf("[RTOS] VRing0 (TX to Linux): 0x%08lx\n", RPMSG_VRING0_ADDR);
	printf("[RTOS] VRing1 (RX from Linux): 0x%08lx\n", RPMSG_VRING1_ADDR);
	printf("[RTOS] Link ID: %d\n", RPMSG_LITE_LINK_ID);
	printf("[RTOS] Static instance: %p\n", &g_rpmsg_ctxt);
	printf("[RTOS] Static endpoint: %p\n", &g_ept_ctxt);
	printf("[RTOS] Static queue storage: %p (%d bytes)\n", 
	       g_queue_storage, (int)sizeof(g_queue_storage));
	
	printf("[RTOS] Calling rpmsg_lite_remote_init (static)...\n");
	rpmsg_inst = rpmsg_lite_remote_init(
		(void *)RPMSG_SHMEM_BASE,
		RPMSG_LITE_LINK_ID,
		RL_NO_FLAGS,
		&g_rpmsg_ctxt
	);
	
	if (!rpmsg_inst || rpmsg_inst == RL_NULL) {
		printf("[RTOS] ERROR: rpmsg_lite_remote_init failed!\n");
		g_rpmsg_inst = NULL;
		return;
	}
	
	g_rpmsg_inst = rpmsg_inst;
	
	printf("[RTOS] RPMsg-Lite initialized (static mode)\n");
	printf("[RTOS]   Instance: %p\n", g_rpmsg_inst);
	printf("[RTOS]   TVQ: %p\n", g_rpmsg_inst->tvq);
	printf("[RTOS]   RVQ: %p\n", g_rpmsg_inst->rvq);
	printf("[RTOS]   LOCK: %p\n", g_rpmsg_inst->lock);
	printf("[RTOS]   lock_static_ctxt addr: %p\n", &g_rpmsg_inst->lock_static_ctxt);
	
	if (g_early_kick_received) {
		printf("[RTOS] Processing early kick received before init\n");
		if (g_rpmsg_inst->link_state == 0) {
			g_rpmsg_inst->link_state = 1;
			printf("[RTOS] Link UP (from early kick)\n");
		}
		g_early_kick_received = 0;
	}
	
	printf("[RTOS] Waiting for Linux VirtIO ready (timeout 30s)...\n");
	uint32_t link_up = rpmsg_lite_wait_for_link_up(g_rpmsg_inst, 30000);
	if (!link_up) {
		printf("[RTOS] ERROR: Timeout waiting for link up\n");
		printf("[RTOS] Hint: Check if Linux remoteproc loaded the firmware\n");
		rpmsg_lite_deinit(g_rpmsg_inst);
		g_rpmsg_inst = NULL;
		return;
	}
	printf("[RTOS] Link is UP!\n");
	
	printf("[RTOS] Creating queue (static)...\n");
	g_rpmsg_queue = rpmsg_queue_create(g_rpmsg_inst, g_queue_storage, &g_queue_ctxt);
	if (!g_rpmsg_queue || g_rpmsg_queue == RL_NULL) {
		printf("[RTOS] ERROR: Failed to create queue\n");
		rpmsg_lite_deinit(g_rpmsg_inst);
		g_rpmsg_inst = NULL;
		return;
	}
	printf("[RTOS] Queue created: %p\n", g_rpmsg_queue);
	
	printf("[RTOS] Creating endpoint (addr=%d, static)...\n", RPMSG_LOCAL_EPT_ADDR);
	g_rpmsg_ept = rpmsg_lite_create_ept(
		g_rpmsg_inst,
		RPMSG_LOCAL_EPT_ADDR,
		rpmsg_queue_rx_cb,
		g_rpmsg_queue,
		&g_ept_ctxt
	);
	if (!g_rpmsg_ept || g_rpmsg_ept == RL_NULL) {
		printf("[RTOS] ERROR: Failed to create endpoint\n");
		rpmsg_queue_destroy(g_rpmsg_inst, g_rpmsg_queue);
		rpmsg_lite_deinit(g_rpmsg_inst);
		g_rpmsg_inst = NULL;
		return;
	}
	printf("[RTOS] Endpoint created: addr=%d\n", g_rpmsg_ept->addr);
	
	/* Announce service to Linux */
	printf("[RTOS] Announcing service: %s\n", RPMSG_NS_SERVICE_NAME);
	ret = rpmsg_ns_announce(g_rpmsg_inst, g_rpmsg_ept, 
	                       RPMSG_NS_SERVICE_NAME, RL_NS_CREATE);
	if (ret != RL_SUCCESS) {
		printf("[RTOS] WARNING: Name service announce failed (ret=%d)\n", ret);
		printf("[RTOS] Continuing anyway - Linux might find us via probing\n");
	} else {
		printf("[RTOS] Service announced successfully\n");
	}
	
	printf("\n");
	printf("============================================\n");
	printf("[RTOS] RPMsg Ready - SSD1306 Command Handler\n");
	printf("============================================\n\n");
	
	/* Static frame buffer for accumulating chunks */
	static uint8_t accumulated_fb[1024];
	static uint16_t fb_received_mask = 0;  /* Track which chunks received */
	
	/* Reset message counters */
	msg_sent = 0;
	msg_received = 0;
	loop_count = 0;
	
	/* Main loop - handle SSD1306 commands */
	while (1) {
		loop_count++;
		
		/* Clear rx_buffer before receiving to avoid stale data */
		memset(rx_buffer, 0, sizeof(g_rx_buffer));
		
		/* Print VirtQueue status periodically */
		if (loop_count % 50 == 1 && loop_count > 1) {
			struct virtqueue *rvq = g_rpmsg_inst->rvq;
			printf("[RTOS] Waiting for message... (loop=%d, vq_available_idx=%d, avail->idx=%d)\n",
			       loop_count, rvq->vq_available_idx, rvq->vq_ring.avail->idx);
		}
		
		/* Receive message - with timeout for status updates */
		ret = rpmsg_queue_recv(g_rpmsg_inst, g_rpmsg_queue, &remote_addr,
		                      rx_buffer, sizeof(g_rx_buffer) - 1, &rx_len, 
		                      1000);  /* 1 second timeout */
		
		if (ret != RL_SUCCESS) {
			if (ret == RL_ERR_NO_BUFF) {
				/* Timeout - just continue waiting */
				continue;
			}
			printf("[RTOS] RX failed: ret=%d, loop=%d, queue=%p\n", ret, loop_count, g_rpmsg_queue);
			printf("[RTOS] g_rpmsg_inst=%p, g_rpmsg_inst->link_state=%d\n", 
			       g_rpmsg_inst, g_rpmsg_inst ? g_rpmsg_inst->link_state : -1);
			/* Don't sleep on error - might be a transient issue */
			continue;
		}
		
		msg_received++;
		
		/* Check minimum message size (4 byte header) */
		if (rx_len < 4) {
			continue;
		}
		
		/* Parse header: cmd(1) + reserved(1) + length(2) */
		uint8_t cmd = (uint8_t)rx_buffer[0];
		uint16_t payload_len = (uint8_t)rx_buffer[2] | ((uint8_t)rx_buffer[3] << 8);
		uint8_t *payload = (uint8_t *)&rx_buffer[4];
		
		/* Minimal logging */
		printf("[RTOS] RX: cmd=0x%02x\n", cmd);
		
		uint8_t result = 0;
		
		switch (cmd) {
		case 0x01:  /* SSD1306_CMD_INIT */
			printf("[RTOS] INIT: skip\n");
			result = 0;
			break;
			
		case 0x02:  /* SSD1306_CMD_DEINIT */
			printf("[RTOS] DEINIT\n");
			result = ssd1306_rtos_deinit();
			break;
			
		case 0x03:  /* SSD1306_CMD_CLEAR */
			printf("[RTOS] CLEAR: start\n");
			
			/* SKIP all RPMsg-Lite functions - they hang! */
			printf("[RTOS] CLEAR: calling ssd1306_rtos_clear_screen...\n");
			ssd1306_rtos_clear_screen();
			printf("[RTOS] CLEAR: done\n");
			
			/* DON'T free buffer - causes hang */
			/* DON'T send response - causes hang */
			/* Just continue and hope Linux sends more messages */
			printf("[RTOS] CLEAR: SKIP free & response, continue\n");
			continue;
			
		case 0x04:  /* SSD1306_CMD_SET_CURSOR */
			if (payload_len >= 2) {
				uint8_t x = payload[0];
				uint8_t y = payload[1];
				printf("[RTOS] SET_CURSOR: (%d,%d)\n", x, y);
				result = ssd1306_rtos_set_cursor(x, y);
			} else {
				result = 1;
			}
			break;
			
		case 0x05:  /* SSD1306_CMD_WRITE_STRING */
			if (payload_len >= 4) {
				uint8_t font_size = payload[0];
				uint8_t x = payload[1];
				uint8_t y = payload[2];
				char *text = (char *)&payload[3];
				text[payload_len - 3 - 1] = '\0';  /* Ensure null terminated */
				printf("[RTOS] WRITE: \"%s\" at (%d,%d)\n", text, x, y);
				ssd1306_rtos_set_cursor(x, y);
				result = ssd1306_rtos_write_string(font_size, text);
			} else {
				result = 1;
			}
			break;
			
		case 0x06:  /* SSD1306_CMD_DISPLAY_ONOFF */
			if (payload_len >= 1) {
				uint8_t onoff = payload[0];
				printf("[RTOS] DISPLAY: %s\n", onoff ? "ON" : "OFF");
				result = ssd1306_rtos_display_onoff(onoff);
			} else {
				result = 1;
			}
			break;
			
		case 0x07: {  /* SSD1306_CMD_UPDATE_FB - frame buffer chunk */
			if (payload_len >= 4) {
				uint16_t offset = payload[0] | (payload[1] << 8);
				uint16_t chunk_len = payload[2] | (payload[3] << 8);
				
				if (offset + chunk_len <= 1024 && chunk_len <= 256) {
					memcpy(&accumulated_fb[offset], &payload[4], chunk_len);
					
					/* Mark which chunk received (256 byte chunks at 0,256,512,768) */
					int chunk_idx = offset / 256;
					fb_received_mask |= (1 << chunk_idx);
					
					printf("[RTOS] FB chunk: offset=%d len=%d mask=0x%x\n", 
					       offset, chunk_len, fb_received_mask);
					result = 0;
				} else {
					printf("[RTOS] FB chunk INVALID: offset=%d len=%d\n", offset, chunk_len);
					result = 1;
				}
			} else {
				result = 1;
			}
			break;
		}
			
		case 0x08: {  /* SSD1306_CMD_FLUSH_FB - flush accumulated buffer */
			printf("[RTOS] FLUSH_FB: mask=0x%x\n", fb_received_mask);
			
			if (fb_received_mask == 0x0F) {  /* All 4 chunks received */
				/* Extract metadata: face_count(4) + fps(4) */
				uint32_t face_count = 0;
				float fps = 0.0f;
				if (payload_len >= 8) {
					memcpy(&face_count, payload, 4);
					memcpy(&fps, &payload[4], 4);
				}
				
				/* Send quick response BEFORE slow I2C transfer */
				tx_buffer[0] = 0;  /* Success */
				ret = rpmsg_lite_send(g_rpmsg_inst, g_rpmsg_ept, remote_addr,
				                     tx_buffer, 1, 
				                     RL_DONT_BLOCK);
				if (ret == RL_SUCCESS) {
					msg_sent++;
					printf("[RTOS] FLUSH_FB: response sent\n");
				}
				
				/* Now do the slow I2C transfer */
				ssd1306_shared_data_t display_data;
				memcpy(display_data.frame_buffer, accumulated_fb, 1024);
				display_data.face_count = face_count;
				display_data.fps = fps;
				
				printf("[RTOS] FLUSH_FB: updating display...\n");
				result = ssd1306_rtos_update_display(&display_data);
				
				/* Reset for next frame */
				fb_received_mask = 0;
				
				/* Release RX buffer */
				ret = rpmsg_queue_nocopy_free(g_rpmsg_inst, rx_buffer);
				
				/* Already sent response, skip the normal response below */
				continue;
			} else {
				printf("[RTOS] FLUSH_FB: WARNING incomplete mask=0x%x\n", 
				       fb_received_mask);
				result = 1;
			}
			break;
		}
			
		default:
			result = 0xFF;
			break;
		}
		
		/* SKIP sending response - rpmsg_lite_send() hangs! */
		printf("[RTOS] Response ready: result=0x%02x (NOT SENT - testing)\n", result);
		
		/* Release the RX buffer after processing (CRITICAL!) */
		ret = rpmsg_queue_nocopy_free(g_rpmsg_inst, rx_buffer);
		if (ret != RL_SUCCESS) {
			printf("[RTOS] ERR: Free failed\n");
		} else {
			printf("[RTOS] RX buffer freed OK\n");
		}
	}  /* End of while(1) loop */
	
	/* Should never reach here */
	printf("[RTOS] ERROR: Exited main loop unexpectedly!\n");
}


/* ============================================
 * LED Blink Task
 * ============================================ */
void prvLedBlinkTask(ULONG thread_input)
{
	(void)thread_input;

	duo_led_control(0);
	tx_thread_sleep(TX_MS_TO_TICKS(1000));
	
	int led_state = 1;
	
	for (int i = 0;i < 5;i++) {
		duo_led_control(led_state);
		led_state = !led_state;
		tx_thread_sleep(TX_MS_TO_TICKS(500));
	}
	duo_led_control(0);
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
	
	/* ALWAYS print when ISR is triggered */
	// printf("\n=== [ISR ENTRY] set_val=0x%x ===\n", set_val);

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
				
				// copy cmdq context (8 bytes) to buffer ASAP
				*((unsigned long *)&rtos_cmdq) =
					*((unsigned long *)cmdq);
				
				printf("[ISR] Slot %d: ip_id=0x%02x cmd_id=%d g_rpmsg_inst=%p\n",
				       i, rtos_cmdq.ip_id, rtos_cmdq.cmd_id, g_rpmsg_inst);
				
			/* Check if this is RPMsg VirtIO kick (ip_id=4, IP_RGN) */
			if (rtos_cmdq.ip_id == 4) {
				int vq_id = rtos_cmdq.cmd_id;  /* Use int instead of uint32_t */
				
				if (g_rpmsg_inst == NULL) {
					/* Early kick: Linux sent kick before RPMsg init */
					g_early_kick_received = 1;
					printf("[RPMsg ISR] Early kick recorded (RPMsg not initialized yet)\n");
					
					/* Clear mailbox and continue */
					mbox_reg->cpu_mbox_set[RECEIVE_CPU]
						.cpu_mbox_int_clr.mbox_int_clr = valid_val;
					mbox_reg->cpu_mbox_en[RECEIVE_CPU].mbox_info &= ~valid_val;
					*((unsigned long *)cmdq) = 0;
					continue;
				}
				
				/* RPMsg initialized - process vring notification */
				printf("[RPMsg ISR] VirtQueue kick: vq_id=%d\n", vq_id);
				
				/* Clear mailbox interrupt */
				mbox_reg->cpu_mbox_set[RECEIVE_CPU]
					.cpu_mbox_int_clr.mbox_int_clr = valid_val;
				mbox_reg->cpu_mbox_en[RECEIVE_CPU].mbox_info &= ~valid_val;
				*((unsigned long *)cmdq) = 0;
				
				/* Set link state when kick received */
				if (g_rpmsg_inst->link_state == 0) {
					g_rpmsg_inst->link_state = 1;
					printf("[RPMsg ISR] Link UP!\n");
				}
				
				/* Call RPMsg-Lite ISR handler */
				env_isr(vq_id);
				
				continue; // Skip normal cmdqu processing
			}
				/* Normal CMDQU mailbox path */
				/* Clear mailbox interrupt first, then disable enable bit, then clear slot buffer.
				 * If we skip cpu_mbox_int_clr, set_val bit can stay asserted and ISR re-enters forever.
				 */
				mbox_reg->cpu_mbox_set[RECEIVE_CPU]
					.cpu_mbox_int_clr.mbox_int_clr = valid_val;
				mbox_reg->cpu_mbox_en[RECEIVE_CPU].mbox_info &= ~valid_val;

				*((unsigned long *)cmdq) = 0;
					
					/* Set link state when kick received */
				if (g_rpmsg_inst != NULL && g_rpmsg_inst->link_state == 0) {
					g_rpmsg_inst->link_state = 1;
					printf("[RPMsg ISR] Link UP!\n");
				}

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
						"cmdq->rtos_valid =%d\n",
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