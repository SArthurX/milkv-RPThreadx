#include <stdio.h>
#include <string.h>
#include "rpmsg_platform.h"
#include "rpmsg_env.h"
#include "arch_helpers.h"

#if defined(RL_USE_ENVIRONMENT_CONTEXT) && (RL_USE_ENVIRONMENT_CONTEXT == 1)
#error "This RPMsg-Lite port requires RL_USE_ENVIRONMENT_CONTEXT set to 0"
#endif

static int32_t isr_counter = 0;
static void *platform_lock;

#if defined(RL_USE_STATIC_API) && (RL_USE_STATIC_API == 1)
static LOCK_STATIC_CONTEXT platform_lock_static_ctxt;
#endif


static volatile uint32_t *mailbox_reg = (volatile uint32_t *)MAILBOX_BASE;

/* External interface from comm_main.c for mailbox send with hardware spinlock */
extern int comm_mailbox_send_rpmsg_kick(uint32_t vq_id);



// step 1
int32_t platform_init(void)
{
#if defined(RL_USE_STATIC_API) && (RL_USE_STATIC_API == 1)
    if (env_create_mutex(&platform_lock, 1, &platform_lock_static_ctxt) != 0) {
        printf("[platform] ERROR: Failed to create mutex (static)\n");
        return -1;
    }
#else
    if (env_create_mutex(&platform_lock, 1) != 0) {
        printf("[platform] ERROR: Failed to create mutex\n");
        return -1;
    }
#endif
    return 0;
}

int32_t platform_deinit(void)
{
    /* 釋放互斥鎖 */
    if (platform_lock) {
        env_delete_mutex(platform_lock);
        platform_lock = NULL;
    }

    return 0;
}

/**
 * platform_cache_all_flush_invalidate
 *
 * Dummy implementation
 *
 */
void platform_cache_all_flush_invalidate(void)
{
}

/**
 * platform_cache_flush
 *
 * Uses ThreadX's existing flush_dcache_range function
 * which implements C906L's dcache.cipa instruction
 */
void platform_cache_flush(void *data, uint32_t len)
{
    flush_dcache_range((uintptr_t)data, (size_t)len);
}

/**
 * platform_cache_invalidate
 *
 * Uses ThreadX's existing inv_dcache_range function  
 * which implements C906L's dcache.ipa instruction
 */
void platform_cache_invalidate(void *data, uint32_t len)
{
    inv_dcache_range((uintptr_t)data, (size_t)len);
}

/**
 * platform_cache_disable
 *
 * Dummy implementation
 *
 */
void platform_cache_disable(void)
{
}

void platform_map_mem_region(uint32_t vrt_addr, uint32_t phy_addr,
                             uint32_t size, uint32_t flags)
{
}

// interrupt
int32_t platform_init_interrupt(uint32_t vector_id, void *isr_data)
{
    env_register_isr(vector_id, isr_data);
    env_lock_mutex(platform_lock);
    isr_counter++;
    env_unlock_mutex(platform_lock);
    return 0;
}

int32_t platform_deinit_interrupt(uint32_t vector_id)
{
    env_lock_mutex(platform_lock);
    isr_counter--;
    env_unlock_mutex(platform_lock);

    env_unregister_isr(vector_id);

    return 0;
}


void platform_notify(uint32_t vector_id)
{
    printf("[platform_notify] ENTRY: vq_id=%d\n", (int)vector_id);
    
    /*
     * Use unified mailbox send interface instead of direct mailbox write.
     * This ensures proper ThreadX hardware spinlock handling.
     */
    int ret = comm_mailbox_send_rpmsg_kick(vector_id);
    if (ret != 0) {
        printf("[platform_notify] ERROR: Failed to send RPMsg kick (vq_id=%d)\n", (int)vector_id);
        return;
    }
    
    printf("[platform_notify] SUCCESS: Kicked Linux (vq_id=%d)\n", (int)vector_id);
}

void platform_interrupt_enable(uint32_t vector_id)
{
    (void)vector_id;
}

void platform_interrupt_disable(uint32_t vector_id)
{
    (void)vector_id;
}

int32_t platform_in_isr(void)
{
    return 0;
}

int32_t platform_time_delay(uint32_t num_msec)
{
    env_sleep_msec(num_msec); 
    return 0;
}

uint32_t platform_get_time_stamp(void)
{
    return (uint32_t)env_get_timestamp();
}
