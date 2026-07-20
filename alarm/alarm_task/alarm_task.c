/*
 * alarm_task.c
 *
 * Drives the alarm output according to the security state and services chirp
 * requests from other tasks.
 */

#include <stdint.h>
#include <stdbool.h>

#include "alarm_task.h"
#include "globals.h"
#include "ql_log.h"

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

#include "driver/gpio.h"
#include "driver/ledc.h"

QL_LOG_TAG("alarm");

#define BOOST_EN_GPIO GPIO_NUM_17
#define BUZZER_PWM_GPIO GPIO_NUM_16

#define LEDC_TIMER LEDC_TIMER_0
#define LEDC_MODE LEDC_LOW_SPEED_MODE
#define LEDC_CHANNEL LEDC_CHANNEL_0
#define LEDC_DUTY_RES LEDC_TIMER_10_BIT
#define LEDC_FREQ_HZ 2700

#define DUTY_FULL 512

#define CHIRP_DUTY 300
#define CHIRP_MS 100

void alarm_task_init(void)
{
    gpio_config_t boost_en_cfg = {
        .pin_bit_mask = 1ULL << BOOST_EN_GPIO,
        .mode = GPIO_MODE_OUTPUT,
        .pull_up_en = GPIO_PULLUP_DISABLE,
        .pull_down_en = GPIO_PULLDOWN_DISABLE,
        .intr_type = GPIO_INTR_DISABLE,
    };

    gpio_config(&boost_en_cfg);
    gpio_set_level(BOOST_EN_GPIO, 0);

    ledc_timer_config_t timer_cfg = {
        .speed_mode = LEDC_MODE,
        .timer_num = LEDC_TIMER,
        .duty_resolution = LEDC_DUTY_RES,
        .freq_hz = LEDC_FREQ_HZ,
        .clk_cfg = LEDC_AUTO_CLK,
    };

    ledc_timer_config(&timer_cfg);

    ledc_channel_config_t channel_cfg = {
        .speed_mode = LEDC_MODE,
        .channel = LEDC_CHANNEL,
        .timer_sel = LEDC_TIMER,
        .intr_type = LEDC_INTR_DISABLE,
        .gpio_num = BUZZER_PWM_GPIO,
        .duty = 0,
        .hpoint = 0,
    };

    ledc_channel_config(&channel_cfg);
}

void alarm_task(void *arg)
{
    QL_LOGI("task started on core %d", xPortGetCoreID());

    /* Notification Bits */
    /* Bits 31-2 | Bit 1         | Bit 0           */
    /* Unused    | Chirp request | General wake up */

    uint32_t notification_value;
    bool chirp_requested = false;
    while (1) {
        xTaskNotifyWait(0, 0xFFFFFFFF, &notification_value, portMAX_DELAY);
        QL_LOGI("woke up; notification=0x%08x", (unsigned)notification_value);
        chirp_requested = notification_value & ALARM_CHIRP_BIT;

        if (security_state == SECURITY_DISARMED || security_state == SECURITY_ARMED_QUIET) {
            /* Alarm off */
            gpio_set_level(BOOST_EN_GPIO, 0);

            ledc_set_duty(LEDC_MODE,
                          LEDC_CHANNEL,
                          0);

            ledc_update_duty(LEDC_MODE,
                             LEDC_CHANNEL);

        } else if (security_state == SECURITY_ARMED_TIER2) {
            /* Half alarm */
            gpio_set_level(BOOST_EN_GPIO, 0);

            ledc_set_duty(LEDC_MODE,
                          LEDC_CHANNEL,
                          DUTY_FULL);

            ledc_update_duty(LEDC_MODE,
                             LEDC_CHANNEL);

        } else if (security_state == SECURITY_ARMED_TIER3) {
            /* Full alarm */
            gpio_set_level(BOOST_EN_GPIO, 1);

            ledc_set_duty(LEDC_MODE,
                          LEDC_CHANNEL,
                          DUTY_FULL);

            ledc_update_duty(LEDC_MODE,
                             LEDC_CHANNEL);

        } else {
            QL_LOGW("unknown security_state %d", (int)security_state);
        }

        if (chirp_requested && (security_state == SECURITY_DISARMED || security_state == SECURITY_ARMED_QUIET)) {
            /* Chirp alarm */
            gpio_set_level(BOOST_EN_GPIO, 0);

            ledc_set_duty(LEDC_MODE,
                          LEDC_CHANNEL,
                          CHIRP_DUTY);

            ledc_update_duty(LEDC_MODE,
                             LEDC_CHANNEL);

            vTaskDelay(pdMS_TO_TICKS(CHIRP_MS));

            ledc_set_duty(LEDC_MODE,
                          LEDC_CHANNEL,
                          0);

            ledc_update_duty(LEDC_MODE,
                             LEDC_CHANNEL);
        }
    }
}