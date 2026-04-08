#define DT_DRV_COMPAT hynetek_husb238

#define LOG_LEVEL CONFIG_I2C_LOG_LEVEL_DEFAULT
#include <zephyr/logging/log.h>
LOG_MODULE_REGISTER(husb238);

#include <zephyr/drivers/i2c.h>
#include <husb238.h>

#define CHECK_SRC_DETECT_BIT(pdo) ((pdo) & 0x80)
#define PDO_VOLTAGE(pdo) (((pdo) >> 4) & 0x0F)
#define PDO_CURRENT(pdo) ((pdo) & 0x0F)


struct husb238_config {
    struct i2c_dt_spec i2c;  /* pulls address + bus from DTS automatically */
};

int husb238_get_pd_contract(const struct device *dev, husb238_pd_src_cap pd_src_cap)
{
    const struct husb238_config *cfg = dev->config;

    int ret = i2c_reg_read_byte_dt(&cfg->i2c, HUSB238_REG_STATUS0, &pd_src_cap);
    if (ret < 0) return ret;

    return 0;
}

int husb238_request_pdo(const struct device *dev,
                        husb238_pd_src_voltage pdo_req)
{
    const struct husb238_config *cfg = dev->config;

    if (PDO_VOLTAGE(pdo_req) < HUSB238_VOLTAGE_5V || PDO_VOLTAGE(pdo_req) > HUSB238_VOLTAGE_20V) {
        return -EINVAL;
    }

    uint8_t pdo_index = pdo_req - 1; // Map enum to PDO index (5V=0, 9V=1, ..., 20V=5)
    // return i2c_reg_write_byte_dt(&cfg->i2c, SRC_PDO, pdo_index);
}

int husb238_get_src_capabilities(const struct device *dev, uint8_t *src_pdos, const size_t len) {
    const struct husb238_config *cfg = dev->config;
    // Six PDOs (5V, 9V, 12V, 15V, 18V, 20V)
    if (len != 6) return -EINVAL;    

    for (size_t i = 0; i < 6; i++) {
        int ret = i2c_reg_read_byte_dt(&cfg->i2c, HUSB238_REG_SRC_PDO_5V + i, &src_pdos[i]);
        if (ret < 0) return ret;
    }
    return 0;
}

int husb238_print_src_capabilities(const uint8_t *src_pdos, const size_t len) {
    static const char *voltage_strings[] = {
        "5V", "9V", "12V", "15V", "18V", "20V",
    };

    static const char *current_strings[] = {
        "0.5A", "0.7A", "1.0A", "1.25A",
        "1.5A", "1.75A", "2.0A", "2.25A",
        "2.5A", "2.75A", "3.0A", "3.25A",
        "3.5A", "4.0A", "4.5A", "5.0A",
    };

    // Expecting 6 PDOs
    if (len != 6) return -EINVAL;

    for (size_t i = 0; i < len; i++) {
        uint8_t pdo = src_pdos[i];

        if (!CHECK_SRC_DETECT_BIT(pdo))
        {
            LOG_INF("%s, not supported", voltage_strings[i]);
            continue;
        }
        else 
        {
            LOG_INF("%s, Max current=%sA", voltage_strings[i], current_strings[PDO_CURRENT(pdo)]);
        }
    }
    return 0;
}

int husb238_init(const struct device *dev) {
    const struct husb238_config *cfg = dev->config;
    if (!i2c_is_ready_dt(&cfg->i2c)) {
        return -ENODEV;
    }
    return 0;
}

#define HUSB238_DEFINE(inst)                                    \
    static const struct husb238_config husb238_cfg_##inst = {  \
        .i2c = I2C_DT_SPEC_INST_GET(inst),                     \
    };                                                          \
    DEVICE_DT_INST_DEFINE(inst, husb238_init, NULL,            \
                          NULL, &husb238_cfg_##inst,            \
                          POST_KERNEL,                          \
                          CONFIG_I2C_INIT_PRIORITY,             \
                          NULL);

DT_INST_FOREACH_STATUS_OKAY(HUSB238_DEFINE)