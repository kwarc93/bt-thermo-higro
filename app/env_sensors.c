/*
 * env_sensors.c
 *
 *  Created on: 13 lut 2022
 *      Author: kwarc
 */

#include "env_sensors.h"

#include <stdbool.h>

#include "nrf_gpio.h"
#include "nrf_delay.h"
#include "hts221.h"
#include "lps22hb.h"

//-----------------------------------------------------------------------------
/* definitions */

#define ENV_SENS_PWR_PIN                NRF_GPIO_PIN_MAP(0,22)
#define ENV_SENS_PUP_PIN                NRF_GPIO_PIN_MAP(1,0)

#define TWI_INSTANCE_ID                 0

//-----------------------------------------------------------------------------
/* private */

NRF_TWI_MNGR_DEF(twi_mngr, 10, TWI_INSTANCE_ID);
NRF_TWI_SENSOR_DEF(twi_sensor, &twi_mngr, HTS221_MIN_QUEUE_SIZE + LPS22HB_MIN_QUEUE_SIZE);

static env_sens_drdy_cb_t env_sens_drdy_cb = NULL;
static env_sens_data_t env_sens_data = {0};

//-----------------------------------------------------------------------------

HTS221_INSTANCE_DEF(hts221_sensor, &twi_sensor, HTS221_BASE_ADDRESS);

typedef struct
{
    bool ready;
    uint8_t reg;
    int16_t raw_temp;
    int16_t raw_hum;
} hts221_ctx_t;

static hts221_ctx_t hts221_ctx = {0};

LPS22HB_INSTANCE_DEF(lps22hb_sensor, &twi_sensor, LPS22HB_BASE_ADDRESS_LOW);

typedef struct
{
    bool ready;
    uint8_t reg;
    lps22hb_data_t raw_data;
} lps22hb_ctx_t;

static lps22hb_ctx_t lps22hb_ctx = {0};

//-----------------------------------------------------------------------------

static bool is_data_ready_from_all_sensors(void)
{
    return (hts221_ctx.raw_temp != INT16_MIN && hts221_ctx.raw_hum != INT16_MIN && lps22hb_ctx.raw_data.pressure != INT32_MIN);
}

static void hts221_clear_ctx(void);
static void hts221_reg_cb(ret_code_t result, void * p_register_data);
static void hts221_temp_data_cb(ret_code_t result, int16_t * p_data);
static void hts221_hum_data_cb(ret_code_t result, int16_t * p_data);

static void hts221_clear_ctx(void)
{
    hts221_ctx.ready = false;
    hts221_ctx.reg = 0;
    hts221_ctx.raw_temp = INT16_MIN;
    hts221_ctx.raw_hum = INT16_MIN;
}

static void hts221_reg_cb(ret_code_t result, void * p_register_data)
{
    if (result == NRF_SUCCESS)
    {
        uint8_t status_reg = *(uint8_t*)p_register_data;
        if (status_reg & 0b11)
        {
            /* Data is ready - read it */
            hts221_temp_read(&hts221_sensor, hts221_temp_data_cb, &hts221_ctx.raw_temp);
            hts221_hum_read(&hts221_sensor, hts221_hum_data_cb, &hts221_ctx.raw_hum);
            return;
        }
    }

    /* Read again */
    hts221_status_read(&hts221_sensor, hts221_reg_cb, &hts221_ctx.reg);
}

static void hts221_temp_data_cb(ret_code_t result, int16_t * p_data)
{
    if (result == NRF_SUCCESS)
    {
        /* Data is ready - process it */
        int16_t temp = *(int16_t*)p_data;
        env_sens_data.temperature = hts221_temp_process(&hts221_sensor, temp) / 8.0f;

        if (is_data_ready_from_all_sensors())
        {
            if (env_sens_drdy_cb != NULL)
                env_sens_drdy_cb(&env_sens_data);
        }

        return;
    }

    /* Read again */
    hts221_temp_read(&hts221_sensor, hts221_temp_data_cb, &hts221_ctx.raw_temp);
}

static void hts221_hum_data_cb(ret_code_t result, int16_t * p_data)
{
    if (result == NRF_SUCCESS)
    {
        /* Data is ready - process it */
        int16_t hum = *(int16_t*)p_data;
        env_sens_data.humidity = hts221_hum_process(&hts221_sensor, hum) / 2.0f;

        if (is_data_ready_from_all_sensors())
        {
            if (env_sens_drdy_cb != NULL)
                env_sens_drdy_cb(&env_sens_data);
        }

        return;
    }

    /* Read again */
    hts221_hum_read(&hts221_sensor, hts221_hum_data_cb, &hts221_ctx.raw_hum);
}

static void hts221_who_am_i_cb(ret_code_t result, void *p_register_data)
{
    if (result == NRF_SUCCESS)
    {
        uint8_t who_am_i_reg = *(uint8_t*)p_register_data;
        if (who_am_i_reg == HTS221_WHO_AM_I)
        {
            /* Sensor is ready */
            hts221_ctx.ready = true;
            return;
        }
    }

    /* Read again */
    hts221_who_am_i_read(&hts221_sensor, hts221_who_am_i_cb, &hts221_ctx.reg);
}

//-----------------------------------------------------------------------------

static void lps22hb_clear_ctx(void);
static void lps22hb_who_am_i_cb(ret_code_t result, void *p_register_data);
static void lps22hb_reg_cb(ret_code_t result, void * p_register_data);
static void lps22hb_drdy_cb(ret_code_t result, lps22hb_data_t * p_raw_data);


static void lps22hb_clear_ctx(void)
{
    lps22hb_ctx.ready = false;
    lps22hb_ctx.reg = 0;
    lps22hb_ctx.raw_data.pressure = INT32_MIN;
    lps22hb_ctx.raw_data.temperature = INT16_MIN;
}

static void lps22hb_who_am_i_cb(ret_code_t result, void *p_register_data)
{
    if (result == NRF_SUCCESS)
    {
        uint8_t who_am_i_reg = *(uint8_t*)p_register_data;
        if (who_am_i_reg == LPS22HB_WHO_AM_I)
        {
            /* Sensor is ready */
            lps22hb_ctx.ready = true;
            return;
        }
    }

    /* Read again */
    lps22hb_who_am_i_read(&lps22hb_sensor, lps22hb_who_am_i_cb, &lps22hb_ctx.reg);
}

static void lps22hb_reg_cb(ret_code_t result, void * p_register_data)
{
    if (result == NRF_SUCCESS)
    {
        uint8_t status_reg = *(uint8_t*)p_register_data;
        if (status_reg & 0b11)
        {
            /* Data is ready - read it */
            lps22hb_data_read(&lps22hb_sensor, lps22hb_drdy_cb, &lps22hb_ctx.raw_data, 1);
            return;
        }
    }

    /* Read again */
    lps22hb_status_read(&lps22hb_sensor, lps22hb_reg_cb, &lps22hb_ctx.reg);
}

static void lps22hb_drdy_cb(ret_code_t result, lps22hb_data_t * p_raw_data)
{
    if (result == NRF_SUCCESS)
    {
        /* Data is ready - decode it */
        lps22hb_data_decode(p_raw_data, 1);
        env_sens_data.pressure = p_raw_data->pressure / 4096.0f;

        if (is_data_ready_from_all_sensors())
        {
            if (env_sens_drdy_cb != NULL)
                env_sens_drdy_cb(&env_sens_data);
        }

        return;
    }

    /* Read again */
    lps22hb_data_read(&lps22hb_sensor, lps22hb_drdy_cb, &lps22hb_ctx.raw_data, 1);

}

//-----------------------------------------------------------------------------
/* public */

env_sens_stat_t env_sensors_init(void)
{
    /* Config  pull-up resistors on TWI */
    nrf_gpio_cfg_output(ENV_SENS_PUP_PIN);

    /* Config  power to sensors */
    nrf_gpio_cfg_output(ENV_SENS_PWR_PIN);

    env_sensors_pwr_on();

    const nrf_drv_twi_config_t twi_config = {
       .scl                = 15,
       .sda                = 14,
       .frequency          = NRF_DRV_TWI_FREQ_100K,
       .interrupt_priority = APP_IRQ_PRIORITY_LOW,
       .clear_bus_init     = true
    };

    ret_code_t result = NRF_SUCCESS;
    result |= nrf_twi_mngr_init(&twi_mngr, &twi_config);
    result |= nrf_twi_sensor_init(&twi_sensor);

    result |= lps22hb_init(&lps22hb_sensor);
    result |= lps22hb_who_am_i_read(&lps22hb_sensor, lps22hb_who_am_i_cb, &lps22hb_ctx.reg);

    result |= hts221_init(&hts221_sensor);
    result |= hts221_who_am_i_read(&hts221_sensor, hts221_who_am_i_cb, &hts221_ctx.reg);

    while (!hts221_ctx.ready || !lps22hb_ctx.ready);

    hts221_clear_ctx();
    lps22hb_clear_ctx();

    env_sensors_pwr_off();

    return (result == NRF_SUCCESS) ? ENV_SENS_OK : ENV_SENS_ERROR;
}

env_sens_stat_t env_sensors_deinit(void)
{
    /* TODO */
    return ENV_SENS_OK;
}

env_sens_stat_t env_sensors_pwr_on(void)
{
    /* Enable pull-up resistors on TWI */
    nrf_gpio_pin_set(ENV_SENS_PUP_PIN);

    /* Enable power to sensors */
    nrf_gpio_pin_set(ENV_SENS_PWR_PIN);

    nrf_delay_ms(10);

    return ENV_SENS_OK;
}

env_sens_stat_t env_sensors_pwr_off(void)
{
    /* Disable power to sensors */
//    nrf_gpio_pin_clear(ENV_SENS_PWR_PIN);

    /* Disable pull-up resistors on TWI */
//    nrf_gpio_pin_clear(ENV_SENS_PUP_PIN);

    return ENV_SENS_OK;
}

env_sens_stat_t env_sensors_trigger_measurement(env_sens_drdy_cb_t drdy_cb)
{

    hts221_pd_enable(&hts221_sensor, true);
    nrf_delay_ms(5);
    hts221_data_rate_cfg(&hts221_sensor, HTS221_ODR_ONESHOT);

    hts221_clear_ctx();
    hts221_oneshot(&hts221_sensor);
    hts221_status_read(&hts221_sensor, hts221_reg_cb, &hts221_ctx.reg);

    lps22hb_clear_ctx();
    lps22hb_oneshot(&lps22hb_sensor);
    lps22hb_status_read(&lps22hb_sensor, lps22hb_reg_cb, &lps22hb_ctx.reg);

    if (drdy_cb != NULL)
        env_sens_drdy_cb = drdy_cb;

    return ENV_SENS_OK;
}

env_sens_stat_t env_sensors_get_data(env_sens_data_t *data)
{
    if (data == NULL)
        return ENV_SENS_ERROR;

    *data = env_sens_data;
    return ENV_SENS_OK;
}
