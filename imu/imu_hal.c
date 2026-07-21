#include "imu_hal.h"

#include "driver/i2c_master.h"
#include "driver/gpio.h"
#include "freertos/semphr.h"
#include "esp_log.h"

static const char *TAG = "imu_hal";

/* ---- MPU-6050 register map (RM-MPU-6000A-00) ---- */
#define MPU6050_I2C_ADDR         0x68
#define REG_SMPLRT_DIV           0x19
#define REG_CONFIG               0x1A
#define REG_GYRO_CONFIG          0x1B
#define REG_ACCEL_CONFIG         0x1C
#define REG_INT_PIN_CFG          0x37
#define REG_INT_ENABLE           0x38
#define REG_ACCEL_XOUT_H         0x3B
#define REG_PWR_MGMT_1           0x6B
#define REG_WHO_AM_I             0x75

#define INT_ENABLE_DATA_RDY      0x01

/* Config per Section 3.3.1: +/-4g accel (8192 LSB/g), +/-500 dps gyro
 * (65.5 LSB/dps), DLPF_CFG=3 -> 44 Hz accel bandwidth. */
#define ACCEL_FS_SEL_4G          0x08
#define GYRO_FS_SEL_500DPS       0x08
#define DLPF_CFG_44HZ            0x03
#define ACCEL_SENSITIVITY_LSB_PER_G   8192.0f
#define GYRO_SENSITIVITY_LSB_PER_DPS  65.5f

/* With DLPF enabled the internal sample rate is 1 kHz; divide down to the
 * 100 Hz design point from T1 (Section 3.3.5). */
#define SMPLRT_DIV_100HZ         0x09

#define I2C_MASTER_SDA_IO        21
#define I2C_MASTER_SCL_IO        22
#define I2C_MASTER_FREQ_HZ       100000

/* MPU-6050 INT pin must be wired to this GPIO. GPIO34 is RTC-capable and
 * input-only, which suits a pure interrupt line (Section 3.3.4). */
#define IMU_HAL_INT_GPIO         GPIO_NUM_34

static i2c_master_dev_handle_t s_dev_handle;
static SemaphoreHandle_t s_data_ready_sem;

static void IRAM_ATTR imu_int_isr_handler(void *arg)
{
    BaseType_t woken = pdFALSE;
    xSemaphoreGiveFromISR(s_data_ready_sem, &woken);
    portYIELD_FROM_ISR(woken);
}

static esp_err_t write_reg(uint8_t reg, uint8_t value)
{
    uint8_t buf[2] = { reg, value };
    return i2c_master_transmit(s_dev_handle, buf, sizeof(buf), 1000);
}

static esp_err_t init_i2c_bus(void)
{
    i2c_master_bus_config_t bus_config = {
        .i2c_port = I2C_NUM_0,
        .sda_io_num = I2C_MASTER_SDA_IO,
        .scl_io_num = I2C_MASTER_SCL_IO,
        .clk_source = I2C_CLK_SRC_DEFAULT,
        .glitch_ignore_cnt = 7,
        .flags.enable_internal_pullup = true,
    };
    i2c_master_bus_handle_t bus_handle;
    esp_err_t err = i2c_new_master_bus(&bus_config, &bus_handle);
    if (err != ESP_OK) {
        return err;
    }

    i2c_device_config_t dev_config = {
        .dev_addr_length = I2C_ADDR_BIT_LEN_7,
        .device_address = MPU6050_I2C_ADDR,
        .scl_speed_hz = I2C_MASTER_FREQ_HZ,
    };
    return i2c_master_bus_add_device(bus_handle, &dev_config, &s_dev_handle);
}

static esp_err_t init_mpu6050_registers(void)
{
    esp_err_t err;

    /* Wake up: device resets into sleep mode (PWR_MGMT_1 default 0x40). */
    err = write_reg(REG_PWR_MGMT_1, 0x00);
    if (err != ESP_OK) return err;

    err = write_reg(REG_SMPLRT_DIV, SMPLRT_DIV_100HZ);
    if (err != ESP_OK) return err;

    err = write_reg(REG_CONFIG, DLPF_CFG_44HZ);
    if (err != ESP_OK) return err;

    err = write_reg(REG_GYRO_CONFIG, GYRO_FS_SEL_500DPS);
    if (err != ESP_OK) return err;

    err = write_reg(REG_ACCEL_CONFIG, ACCEL_FS_SEL_4G);
    if (err != ESP_OK) return err;

    /* INT pin: push-pull, active-high, 50us pulse (register default 0x00). */
    err = write_reg(REG_INT_PIN_CFG, 0x00);
    if (err != ESP_OK) return err;

    return write_reg(REG_INT_ENABLE, INT_ENABLE_DATA_RDY);
}

static esp_err_t init_interrupt_gpio(void)
{
    s_data_ready_sem = xSemaphoreCreateBinary();
    if (s_data_ready_sem == NULL) {
        return ESP_ERR_NO_MEM;
    }

    gpio_config_t io_conf = {
        .pin_bit_mask = 1ULL << IMU_HAL_INT_GPIO,
        .mode = GPIO_MODE_INPUT,
        .pull_up_en = GPIO_PULLUP_DISABLE,
        .pull_down_en = GPIO_PULLDOWN_DISABLE,
        .intr_type = GPIO_INTR_POSEDGE,
    };
    esp_err_t err = gpio_config(&io_conf);
    if (err != ESP_OK) return err;

    err = gpio_install_isr_service(0);
    if (err != ESP_OK && err != ESP_ERR_INVALID_STATE) {
        /* INVALID_STATE just means another driver already installed the
         * shared ISR service; that's fine, we can still add our handler. */
        return err;
    }

    return gpio_isr_handler_add(IMU_HAL_INT_GPIO, imu_int_isr_handler, NULL);
}

esp_err_t imu_hal_init(void)
{
    esp_err_t err = init_i2c_bus();
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "I2C bus init failed: %s", esp_err_to_name(err));
        return err;
    }

    /* Some GY-521 clone boards don't report the datasheet 0x68 here even
     * though the chip otherwise works fine, so a mismatch is logged, not
     * treated as fatal -- only a transaction failure aborts init. */
    uint8_t who_am_i = 0;
    uint8_t reg = REG_WHO_AM_I;
    err = i2c_master_transmit_receive(s_dev_handle, &reg, 1, &who_am_i, 1, 1000);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "MPU6050 not responding on I2C: %s", esp_err_to_name(err));
        return err;
    }
    if (who_am_i != MPU6050_I2C_ADDR) {
        ESP_LOGW(TAG, "Unexpected WHO_AM_I=0x%02X (expected 0x%02X); continuing anyway",
                 who_am_i, MPU6050_I2C_ADDR);
    }

    err = init_mpu6050_registers();
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "MPU6050 register config failed: %s", esp_err_to_name(err));
        return err;
    }

    err = init_interrupt_gpio();
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "INT GPIO setup failed: %s", esp_err_to_name(err));
        return err;
    }

    ESP_LOGI(TAG, "MPU6050 ready (100 Hz, +/-4g, +/-500dps, INT on GPIO%d)", IMU_HAL_INT_GPIO);
    return ESP_OK;
}

bool imu_hal_wait_for_data_ready(TickType_t timeout_ticks)
{
    return xSemaphoreTake(s_data_ready_sem, timeout_ticks) == pdTRUE;
}

esp_err_t imu_hal_read(imu_data_t *out)
{
    uint8_t reg = REG_ACCEL_XOUT_H;
    uint8_t data[14];
    esp_err_t err = i2c_master_transmit_receive(s_dev_handle, &reg, 1, data, sizeof(data), 1000);
    if (err != ESP_OK) {
        return err;
    }

    int16_t accel_x = (data[0] << 8) | data[1];
    int16_t accel_y = (data[2] << 8) | data[3];
    int16_t accel_z = (data[4] << 8) | data[5];
    int16_t temp_raw = (data[6] << 8) | data[7];
    int16_t gyro_x = (data[8] << 8) | data[9];
    int16_t gyro_y = (data[10] << 8) | data[11];
    int16_t gyro_z = (data[12] << 8) | data[13];

    out->accel_x_g = accel_x / ACCEL_SENSITIVITY_LSB_PER_G;
    out->accel_y_g = accel_y / ACCEL_SENSITIVITY_LSB_PER_G;
    out->accel_z_g = accel_z / ACCEL_SENSITIVITY_LSB_PER_G;
    out->gyro_x_dps = gyro_x / GYRO_SENSITIVITY_LSB_PER_DPS;
    out->gyro_y_dps = gyro_y / GYRO_SENSITIVITY_LSB_PER_DPS;
    out->gyro_z_dps = gyro_z / GYRO_SENSITIVITY_LSB_PER_DPS;
    out->temp_c = temp_raw / 340.0f + 36.53f;

    return ESP_OK;
}
