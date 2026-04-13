#ifndef HUSB238_H
#define HUSB238_H

#include <zephyr/device.h>
#include <zephyr/drivers/i2c.h>

/* I2C Registers */
#define HUSB238_REG_STATUS0     0x00
#define HUSB238_REG_STATUS1     0x01
#define HUSB238_REG_SRC_PDO_5V  0x02
#define HUSB238_REG_SRC_PDO_9V  0x03
#define HUSB238_REG_SRC_PDO_12V 0x04
#define HUSB238_REG_SRC_PDO_15V 0x05
#define HUSB238_REG_SRC_PDO_18V 0x06
#define HUSB238_REG_SRC_PDO_20V 0x07
#define SRC_PDO                 0x08
#define GO_COMMAND              0x09

#define CHECK_SRC_DETECT_BIT(pdo) ((pdo) & 0x80)

typedef uint8_t husb238_pd_src_cap;

/* PD voltage/current contract values readable from STATUS0/STATUS1 */
typedef enum husb238_pd_src_voltage {
    HUSB238_VOLTAGE_UNATTACHED = 0,
    HUSB238_VOLTAGE_5V,
    HUSB238_VOLTAGE_9V,
    HUSB238_VOLTAGE_12V,
    HUSB238_VOLTAGE_15V,
    HUSB238_VOLTAGE_18V,
    HUSB238_VOLTAGE_20V,
} husb238_pd_src_voltage;

typedef enum husb238_pd_src_current {
    HUSB238_CURRENT_0_5A = 0,
    HUSB238_CURRENT_0_7A,
    HUSB238_CURRENT_1_0A,
    HUSB238_CURRENT_1_25A,
    HUSB238_CURRENT_1_5A,
    HUSB238_CURRENT_1_75A,
    HUSB238_CURRENT_2_0A,
    HUSB238_CURRENT_2_25A,
    HUSB238_CURRENT_2_5A,
    HUSB238_CURRENT_2_75A,
    HUSB238_CURRENT_3_0A,
    HUSB238_CURRENT_3_25A,
    HUSB238_CURRENT_3_5A,
    HUSB238_CURRENT_4_0A,
    HUSB238_CURRENT_4_5A,
    HUSB238_CURRENT_5_0A,
} husb238_pd_src_current;

static inline uint8_t PDO_VOLTAGE(uint8_t pd_src_cap) {
    return (uint8_t) ((pd_src_cap >> 4) & 0x0F);
}

static inline uint8_t PDO_CURRENT(uint8_t pd_src_cap) {
    return (uint8_t) (pd_src_cap & 0x0F);
}

/**
 * @brief Reads the current PD contract (voltage and current) from the HUSB238.
 * @param dev Pointer to the device structure for the HUSB238 instance.
 * @param pd_src_cap Pointer to a variable where the raw PD source capability byte from STATUS0 will be stored.
 * @return 0 on success, or a negative error code from the I2C read operation.
 */
int husb238_get_pd_contract(const struct i2c_dt_spec *spec, husb238_pd_src_cap *pd_src_cap);

/**
 * @brief Prints current PD contract from HUSB238 to logger module.
 * @param pd_src_cap The raw PD source contract byte read from STATUS0, containing the voltage and current information.
 */
void husb238_print_pd_contract(husb238_pd_src_cap pd_src_cap);

int husb238_request_pdo(const struct i2c_dt_spec *spec,
                        husb238_pd_src_voltage voltage);
int husb238_reset(const struct i2c_dt_spec *spec);

int husb238_attach_status(const struct i2c_dt_spec *spec, bool *attached);

/**
 * 
 */
int husb238_init(const struct device *dev);

/**
 * @brief Reads the source capabilities (PDOs) from the HUSB238 and fills the provided array with the raw PDO values.
 * @note Each PDO is 1 byte, where bit 7 is source detect bit, and bits 3-0 represent the max current.
 * @note Order of PDOs in the array corresponds to 5V, 9V, 12V, 15V, 18V, 20V respectively.
 * @param dev Pointer to the device structure for the HUSB238 instance.
 * @param src_pdos Pointer to an array of at least 6 bytes where the PDO values will be stored.
 * @param len The length of the src_pdos array. Must be at least 6
 * @return 0 on success, -EINVAL if the length is not 6, or a negative error code from the I2C read operations.
 */
int husb238_get_src_capabilities(const struct i2c_dt_spec *spec, uint8_t *src_pdos, const size_t len);

/**
 * @brief Prints the source capabilities (PDOs) in a human-readable format to the log.
 *       For example: "5V, Max current=3.0A", "9V, not detected", etc.
 * @param src_pdos Pointer to an array of 6 bytes containing the values read from SRC_PDO_nV registers.
 * @param len The length of the src_pdos array. Must be 6.
 * @return 0 on success, -EINVAL if the length is not 6.
 */
int husb238_print_src_capabilities(const uint8_t *src_pdos, const size_t len);

#endif /* HUSB238_H */