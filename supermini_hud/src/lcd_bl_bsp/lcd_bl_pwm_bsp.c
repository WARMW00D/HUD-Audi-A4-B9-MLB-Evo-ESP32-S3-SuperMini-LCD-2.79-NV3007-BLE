#include <stdint.h>
#include "lcd_bl_pwm_bsp.h"
#include "esp_err.h"
#include "driver/ledc.h"
#include "user_config.h"

/* Preserve the UI's legacy 0=bright, 255=dark API; adapt polarity here. */
static uint32_t physical_duty(uint16_t duty)
{
    if (duty > 255) duty = 255;
    return HUD_BL_ACTIVE_HIGH ? 255 - duty : duty;
}
void lcd_bl_pwm_bsp_init(uint16_t duty)
{
    ledc_timer_config_t timer = {
        .speed_mode = LEDC_LOW_SPEED_MODE,
        .duty_resolution = LEDC_TIMER_8_BIT,
        .timer_num = LEDC_TIMER_3,
        .freq_hz = 5000,
        .clk_cfg = LEDC_AUTO_CLK,
    };
    ledc_channel_config_t channel = {
        .gpio_num = EXAMPLE_PIN_NUM_BK_LIGHT,
        .speed_mode = LEDC_LOW_SPEED_MODE,
        .channel = LEDC_CHANNEL_1,
        .intr_type = LEDC_INTR_DISABLE,
        .timer_sel = LEDC_TIMER_3,
        .duty = physical_duty(duty),
        .hpoint = 0,
    };
    ESP_ERROR_CHECK(ledc_timer_config(&timer));
    ESP_ERROR_CHECK(ledc_channel_config(&channel));
}
void setUpduty(uint16_t duty)
{
    ESP_ERROR_CHECK(ledc_set_duty(LEDC_LOW_SPEED_MODE, LEDC_CHANNEL_1, physical_duty(duty)));
    ESP_ERROR_CHECK(ledc_update_duty(LEDC_LOW_SPEED_MODE, LEDC_CHANNEL_1));
}
