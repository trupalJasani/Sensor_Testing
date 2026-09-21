#include "bsp.h"
#include "leaf_wetness.h"

#include "esp_adc/adc_oneshot.h"

#define LEAF_ADC_UNIT     ADC_UNIT_1
#define LEAF_ADC_CHANNEL  ADC_CHANNEL_1

static adc_oneshot_unit_handle_t adc1_handle = NULL;
static LeafSensor_t LeafObj;

/**
 * @brief Reads the raw ADC value from GPIO1.
 *
 * GPIO1 corresponds to ADC1 channel 1 on the ESP32-C3.
 *
 * @param value Pointer where the raw ADC value will be stored.
 * @return 0 on success, -1 on failure.
 */
static int32_t BSP_ADC_Read_Leaf(uint32_t *value)
{
    if (value == NULL || adc1_handle == NULL)
        return -1;

    int adc_value = 0;

    if (adc_oneshot_read(
            adc1_handle,
            LEAF_ADC_CHANNEL,
            &adc_value) != ESP_OK)
    {
        return -1;
    }

    *value = (uint32_t)adc_value;

    return 0;
}

/**
 * @brief Initializes the ESP32-C3 ADC for the Davis leaf sensor.
 *
 * GPIO1 is configured as ADC1 channel 1 with 12 dB attenuation.
 *
 * @return 0 on success, -1 on failure.
 */
static int32_t BSP_ADC_Init(void)
{
    if (adc1_handle != NULL)
        return 0;

    adc_oneshot_unit_init_cfg_t init_config = {
        .unit_id = LEAF_ADC_UNIT,
    };

    if (adc_oneshot_new_unit(
            &init_config,
            &adc1_handle) != ESP_OK)
    {
        return -1;
    }

    adc_oneshot_chan_cfg_t channel_config = {
        .bitwidth = ADC_BITWIDTH_DEFAULT,
        .atten = ADC_ATTEN_DB_12,
    };

    if (adc_oneshot_config_channel(
            adc1_handle,
            LEAF_ADC_CHANNEL,
            &channel_config) != ESP_OK)
    {
        adc_oneshot_del_unit(adc1_handle);
        adc1_handle = NULL;
        return -1;
    }

    return 0;
}

/**
 * @brief Initializes the Davis leaf wetness sensor driver.
 *
 * @return 0 on success, -1 on failure.
 */
int32_t BSP_LEAF_Init(void)
{
    LeafSensor_IO_t leaf_io = {
        .Init = BSP_ADC_Init,
        .DeInit = NULL,
        .ReadRaw = BSP_ADC_Read_Leaf
    };

    if (LeafSensor_RegisterBusIO(
            &LeafObj,
            &leaf_io) != 0)
    {
        return -1;
    }

    return LeafSensor_Init(&LeafObj);
}

/**
 * @brief Reads the raw ADC value from the Davis leaf sensor.
 *
 * @param raw Pointer where the raw ADC value will be stored.
 * @return 0 on success, -1 on failure.
 */
int32_t BSP_LEAF_GetRaw(uint32_t *raw)
{
    return BSP_ADC_Read_Leaf(raw);
}

/**
 * @brief Reads the converted Davis leaf wetness value.
 *
 * @param wetness Pointer where the wetness value will be stored.
 * @return 0 on success, -1 on failure.
 */
int32_t BSP_LEAF_GetWetness(float *wetness)
{
    return LeafSensor_GetWetnessPercent(
        &LeafObj,
        wetness);
}