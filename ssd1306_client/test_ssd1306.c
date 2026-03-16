#include "ssd1306_rpmsg_client.h"
#include "ssd1306_draw.h"
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <string.h>

int main(int argc, char* argv[])
{
    printf("=== SSD1306 RPMsg Client Test Program ===\n");
    printf("This test uses RPMsg for RTOS communication\n\n");

    /* Just open RPMsg connection - SKIP INIT (RTOS already initialized at boot) */
    printf("Step 1: Opening RPMsg connection (SKIP init - RTOS did it at boot)...\n");
    if (ssd1306_rpmsg_open_only() < 0) {
        fprintf(stderr, "Failed to open RPMsg connection\n");
        return -1;
    }
    printf("  - RPMsg connection established\n");
    sleep(1);

    /* Clear screen */
    printf("\nStep 2: Clearing screen...\n");
    ssd1306_rpmsg_clear_screen();
    printf("  - Clear command sent\n");
    sleep(2);  // Wait for RTOS to process

    /* Draw text using frame buffer with drawing functions */
    printf("\nStep 3: Drawing text with frame buffer...\n");
    ssd1306_display_data_t* display_data = ssd1306_rpmsg_get_buffer();
    if (display_data) {
        /* Clear frame buffer */
        ssd1306_clear_frame_buffer(display_data->frame_buffer);
        
        /* Draw text strings */
        ssd1306_draw_string(display_data->frame_buffer, 0, 0, "Hello ThreadX!");
        ssd1306_draw_string(display_data->frame_buffer, 0, 1, "RTOS:Driver");
        ssd1306_draw_string(display_data->frame_buffer, 0, 2, "Mailbox Test");
        ssd1306_draw_string(display_data->frame_buffer, 0, 4,  "Test");
        /* Set metadata */
        display_data->face_count = 0;
        display_data->fps = 0.0f;
        
        printf("  - Updating display with text...\n");
        ssd1306_rpmsg_update_display(display_data);
        printf("  - Text drawn successfully\n");
    }
    sleep(1);

    /* Test display off */
    // printf("\nStep 5: Testing display OFF...\n");
    // ssd1306_rpmsg_display_onoff(0);
    // printf("  - Display should be OFF now\n");
    // sleep(2);
    
    // /* Test display on */
    // printf("\nStep 6: Testing display ON...\n");
    // ssd1306_rpmsg_display_onoff(1);
    // printf("  - Display should be ON now\n");
    // sleep(2);

    /* Test frame buffer with text and graphics */
    printf("\nStep 7: Testing character display (debug colon)...\n");
    // display_data = ssd1306_client_get_shared_buffer();
    if (display_data) {
        /* Clear frame buffer first */
        ssd1306_clear_frame_buffer(display_data->frame_buffer);
        ssd1306_rpmsg_update_display(display_data);
        
        /* Test different characters to isolate the problem */
        printf("  - Drawing test strings...\n");
        ssd1306_draw_string(display_data->frame_buffer, 0, 0, "ABC");
        ssd1306_rpmsg_update_display(display_data);
        sleep(4);
        ssd1306_draw_string(display_data->frame_buffer, 0, 1, "123");
        ssd1306_rpmsg_update_display(display_data);
        sleep(4);
        ssd1306_draw_string(display_data->frame_buffer, 0, 2, "!@#");
        // ssd1306_client_update_display(display_data);
        // sleep(4);
        // ssd1306_draw_string(display_data->frame_buffer, 0, 3, ".,;");  // Test colon-like chars
        // ssd1306_draw_string(display_data->frame_buffer, 0, 4, "Face 3");  // Without colon
        // ssd1306_draw_string(display_data->frame_buffer, 0, 5, ":");     // Single colon
        
        display_data->face_count = 3;
        display_data->fps = 30.5f;
        
        printf("  - Updating display...\n");
        ssd1306_rpmsg_update_display(display_data);
        printf("  - Display updated: %d faces, FPS=%.1f\n", 
               display_data->face_count, display_data->fps);
    }

    sleep(4);  // Longer delay to see the result

    /* Cleanup */
    printf("\nStep 8: Cleaning up...\n");
    ssd1306_rpmsg_close();
    printf("  - Cleanup complete\n");

    printf("\n=== Test completed successfully! ===\n");
    return 0;
}
