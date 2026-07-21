/*
 * belt_detection_task.c
 *
 * GPIO interrupt + debounce based belt detection.
 */

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "driver/gpio.h"
#include "esp_timer.h"
#include "globals.h"
#include "ql_log.h"

QL_LOG_TAG("belt");

#define BELT_GPIO GPIO_NUM_39
#define BELT_DEBOUNCE_MS 40

static esp_timer_handle_t s_debounce_timer;
static volatile int s_pending_level;

static void belt_debounce_timeout_cb(void *arg)
{
    int level = gpio_get_level(BELT_GPIO);

    if (level != s_pending_level) {
        return;
    }

    enum BELT_STATE new_state = level ? BELT_OPEN : BELT_CLOSED;

    if (new_state == belt_state) {
        return;
    }

    belt_state = new_state;

    QL_LOGI("belt %s", level ? "OPEN" : "CLOSED");

    belt_detection_wake_up_security_task();
}

static void IRAM_ATTR belt_gpio_isr_handler(void *arg)
{
    s_pending_level = gpio_get_level(BELT_GPIO);

    esp_timer_stop(s_debounce_timer);
    esp_timer_start_once(s_debounce_timer,
                         BELT_DEBOUNCE_MS * 1000ULL);
}

void belt_detection_init(void)
{
    gpio_config_t io_conf = {
        .pin_bit_mask = (1ULL << BELT_GPIO),
        .mode = GPIO_MODE_INPUT,
        .pull_up_en = GPIO_PULLUP_DISABLE,
        .pull_down_en = GPIO_PULLDOWN_DISABLE,
        .intr_type = GPIO_INTR_ANYEDGE,
    };

    gpio_config(&io_conf);

    const esp_timer_create_args_t timer_args = {
        .callback = &belt_debounce_timeout_cb,
        .name = "belt_debounce",
    };

    esp_timer_create(&timer_args, &s_debounce_timer);

    gpio_isr_handler_add(BELT_GPIO,
                         belt_gpio_isr_handler,
                         NULL);

    int level = gpio_get_level(BELT_GPIO);

    belt_state = level ? BELT_OPEN : BELT_CLOSED;

    gpio_intr_enable(BELT_GPIO);
}

void belt_detection_enable_interrupt(void)
{
    gpio_intr_enable(BELT_GPIO);
}

void belt_detection_disable_interrupt(void)
{
    gpio_intr_disable(BELT_GPIO);
}