/**
 ******************************************************************************
 * @file    max17048.h
 * @brief   Driver for the Maxim/Analog Devices MAX17048 1-cell Li-ion fuel gauge
 *
 * Pure C, no ESP-IDF headers. All bus access goes through the
 * MAX17048_IO_t function pointers; no dynamic memory.
 *
 * Register facts used (MAX17048 datasheet):
 *   - 7-bit I2C address 0x36, all registers 16 bit, MSB first.
 *   - VCELL  0x02 : 78.125 uV / LSB (full 16 bit)
 *   - SOC    0x04 : 1/256 % / LSB  (high byte = whole percent)
 *   - MODE   0x06 : bit14 Quick-Start, bit13 EnSleep, bit12 HibStat (read only)
 *   - VERSION0x08 : 0x001x for MAX17048/49
 *   - HIBRT  0x0A : hibernate thresholds (default 0x8030)
 *   - CONFIG 0x0C : RCOMP (high byte), SLEEP/ALSC/ALRT/ATHD (low byte)
 *   - CRATE  0x16 : signed, 0.208 %/h / LSB
 *   - STATUS 0x1A : flags in the high byte: RI VH VL VR HD SC, EnVr = bit 14
 *   - CMD    0xFE : write 0x5400 = power-on reset
 ******************************************************************************
 */
#ifndef MAX17048_H
#define MAX17048_H

#include <stdint.h>
#include <stdbool.h>

/* --- Return codes --- */
#define MAX17048_OK            (0)
#define MAX17048_ERROR         (-1)   /* bus error or bad argument          */
#define MAX17048_ERROR_ID      (-2)   /* chip answered, VERSION not 0x001x  */

#define MAX17048_I2C_ADDR      (0x36U)

/* --- Registers --- */
#define MAX17048_REG_VCELL     (0x02U)
#define MAX17048_REG_SOC       (0x04U)
#define MAX17048_REG_MODE      (0x06U)
#define MAX17048_REG_VERSION   (0x08U)
#define MAX17048_REG_HIBRT     (0x0AU)
#define MAX17048_REG_CONFIG    (0x0CU)
#define MAX17048_REG_VALRT     (0x14U)
#define MAX17048_REG_CRATE     (0x16U)
#define MAX17048_REG_VRESET_ID (0x18U)
#define MAX17048_REG_STATUS    (0x1AU)
#define MAX17048_REG_CMD       (0xFEU)

/* --- Bits --- */
#define MAX17048_MODE_QUICKSTART  (0x4000U)
#define MAX17048_MODE_ENSLEEP     (0x2000U)
#define MAX17048_MODE_HIBSTAT     (0x1000U)

/* STATUS flags, as returned by MAX17048_GetStatus() (high byte of STATUS) */
#define MAX17048_STATUS_RI     (0x01U)  /* reset indicator: chip was (re)powered */
#define MAX17048_STATUS_VH     (0x02U)  /* voltage high alert                     */
#define MAX17048_STATUS_VL     (0x04U)  /* voltage low alert                      */
#define MAX17048_STATUS_VR     (0x08U)  /* voltage reset alert                    */
#define MAX17048_STATUS_HD     (0x10U)  /* SOC low (empty) alert                  */
#define MAX17048_STATUS_SC     (0x20U)  /* SOC changed by 1 %                     */
#define MAX17048_STATUS_FLAGS  (0x3FU)

#define MAX17048_CMD_POR       (0x5400U)

/* --- 1. Bus IO structure (I2C abstraction) --- */
typedef struct {
    int32_t (*Init)(void);    /* optional */
    int32_t (*DeInit)(void);  /* optional */
    /* Register write: sends Reg followed by Length data bytes. 0 = OK. */
    int32_t (*WriteReg)(uint16_t DevAddr, uint8_t Reg, const uint8_t *pData, uint16_t Length);
    /* Register read: sends Reg, repeated start, reads Length bytes. 0 = OK. */
    int32_t (*ReadReg)(uint16_t DevAddr, uint8_t Reg, uint8_t *pData, uint16_t Length);
    void    (*Delay)(uint32_t ms);
} MAX17048_IO_t;

/* --- 2. Device object --- */
typedef struct {
    MAX17048_IO_t IO;
    uint8_t       is_initialized;
    uint8_t       i2c_address;
    uint16_t      version;
} MAX17048_Object_t;

/* --- 3. Snapshot of all useful values --- */
typedef struct {
    float    voltage_v;        /* cell voltage                         */
    float    soc_percent;      /* state of charge, may read > 100 %    */
    float    crate_pct_per_h;  /* + charging, - discharging            */
    uint8_t  status_flags;     /* MAX17048_STATUS_*                    */
    bool     hibernating;      /* chip in hibernate (slow update, ~3 uA) */
} MAX17048_Data_t;

/* --- 4. Component driver structure --- */
typedef struct {
    int32_t (*Init)(MAX17048_Object_t *pObj);
    int32_t (*DeInit)(MAX17048_Object_t *pObj);
    int32_t (*GetVoltage)(MAX17048_Object_t *pObj, float *Volt);
    int32_t (*GetSOC)(MAX17048_Object_t *pObj, float *Percent);
    int32_t (*GetChargeRate)(MAX17048_Object_t *pObj, float *PctPerHour);
    int32_t (*ReadAll)(MAX17048_Object_t *pObj, MAX17048_Data_t *pData);
} MAX17048_Drv_t;

extern MAX17048_Drv_t MAX17048_Driver;

/* Copies the IO pointers; WriteReg, ReadReg and Delay are mandatory. Calls IO.Init if set. */
int32_t MAX17048_RegisterBusIO(MAX17048_Object_t *pObj, const MAX17048_IO_t *pIO);

/* Reads VERSION (must be 0x001x) and writes MODE = 0 so sleep mode stays
   disabled (EnSleep = 0): a long-low I2C bus while the ESP32 sleeps must not
   stop the gauge. Does NOT clear the reset flag; see ClearStatus(). */
int32_t MAX17048_Init(MAX17048_Object_t *pObj);
int32_t MAX17048_DeInit(MAX17048_Object_t *pObj);

int32_t MAX17048_GetVoltage(MAX17048_Object_t *pObj, float *Volt);
int32_t MAX17048_GetSOC(MAX17048_Object_t *pObj, float *Percent);
int32_t MAX17048_GetChargeRate(MAX17048_Object_t *pObj, float *PctPerHour);
int32_t MAX17048_GetVersion(MAX17048_Object_t *pObj, uint16_t *Version);
int32_t MAX17048_GetStatus(MAX17048_Object_t *pObj, uint8_t *Flags);
/* Clears the given STATUS flags (e.g. MAX17048_STATUS_RI), keeps EnVr and the others. */
int32_t MAX17048_ClearStatus(MAX17048_Object_t *pObj, uint8_t Flags);
int32_t MAX17048_IsHibernating(MAX17048_Object_t *pObj, bool *Hibernating);
/* Restarts the SOC estimate from the present voltage. Only useful right after
   the cell was connected with a noisy start; not needed in normal use. */
int32_t MAX17048_QuickStart(MAX17048_Object_t *pObj);
int32_t MAX17048_ReadAll(MAX17048_Object_t *pObj, MAX17048_Data_t *pData);

/* Low-level 16-bit register access (MSB first). */
int32_t MAX17048_ReadReg16(MAX17048_Object_t *pObj, uint8_t Reg, uint16_t *Value);
int32_t MAX17048_WriteReg16(MAX17048_Object_t *pObj, uint8_t Reg, uint16_t Value);

/* Pure conversions (no bus access), exposed for tests. */
float MAX17048_RawToVoltage(uint16_t Raw);
float MAX17048_RawToSOC(uint16_t Raw);
float MAX17048_RawToCRate(uint16_t Raw);

#endif /* MAX17048_H */
