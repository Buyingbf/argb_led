#define DT_DRV_COMPAT hynetek_husb238

#define LOG_LEVEL LOG_LEVEL_INF // this was set too low and wasn't allowing LOG_INF to print probably
#include <zephyr/logging/log.h>
LOG_MODULE_REGISTER(husb238);

#include <errno.h>
#include <string.h>
#include <zephyr/drivers/i2c.h>
#include <husb238.h>

static const char *voltage_strings[] = {
    "5", "9", "12", "15", "18", "20",
};

static const char *current_strings[] = {
    "0.5", "0.7", "1.0", "1.25",
    "1.5", "1.75", "2.0", "2.25",
    "2.5", "2.75", "3.0", "3.25",
    "3.5", "4.0", "4.5", "5.0",
};

struct husb238_config {
    struct i2c_dt_spec i2c;  /* pulls address + bus from DTS automatically */
};

int husb238_get_pd_contract(const struct i2c_dt_spec *spec, husb238_pd_src_cap *pd_src_cap)
{
    int reg = {HUSB238_REG_STATUS0};
    int ret = i2c_write_read_dt(spec, &reg, 1, pd_src_cap, 1);
    if (ret < 0) return ret;
    
    return 0;
}

void husb238_print_pd_contract(husb238_pd_src_cap pd_src_cap)
{
    switch (PDO_VOLTAGE(pd_src_cap))
    {
        case HUSB238_VOLTAGE_UNATTACHED:
            LOG_ERR("No PD contract established");
            return;
        case HUSB238_VOLTAGE_5V:
        case HUSB238_VOLTAGE_9V:
        case HUSB238_VOLTAGE_12V:
        case HUSB238_VOLTAGE_15V:
        case HUSB238_VOLTAGE_18V:
        case HUSB238_VOLTAGE_20V:
            LOG_INF("PD voltage: %sV", voltage_strings[PDO_VOLTAGE(pd_src_cap)-1]);
            break;
        default:
            LOG_ERR("Unknown value");
            return;
    }

    LOG_INF("PD current: %sA", current_strings[PDO_CURRENT(pd_src_cap)]);
}

int husb238_request_pdo(const struct i2c_dt_spec *spec,
                        const husb238_pd_src_voltage pdo_req)
{
    if (pdo_req < HUSB238_VOLTAGE_5V || pdo_req > HUSB238_VOLTAGE_20V)
    {
        return -EINVAL;
    }

    // Shift request vals for 15V, 18V, 20V to match expected bit positions in the PDO_SELECT field of SRC_PDO
    uint8_t pdo_select_val = (pdo_req >= HUSB238_VOLTAGE_15V) ? (pdo_req + 4) : pdo_req;

    // Shift into PDO_SELECT field (bits 7:4) and send request
    int ret = i2c_reg_write_byte_dt(spec, SRC_PDO, pdo_select_val << 4);
    if (ret < 0) return ret;

    // Send PDO set request to source
    ret = i2c_reg_write_byte_dt(spec, GO_COMMAND, 0b00001);
    if (ret < 0) return ret;

    return 0;
}

int husb238_get_src_capabilities(const struct i2c_dt_spec *spec, uint8_t *src_pdos, const size_t len) {
    // const struct husb238_config *cfg = dev->config;
    int ret;
    // Six PDOs (5V, 9V, 12V, 15V, 18V, 20V)
    if (len != 6) return -EINVAL;   
     
    // Send "Get Source Capabilities" command
    ret = i2c_reg_write_byte_dt(spec, GO_COMMAND, 0b00100);
    if (ret < 0) return ret;

    k_sleep(K_MSEC(100)); // Wait for the device to process the command and update the PDO registers


    // for (size_t i = 0; i < 6; i++) {
    //     ret = i2c_reg_read_byte_dt(spec, HUSB238_REG_SRC_PDO_5V + i, &src_pdos[i]);
    //     if (ret < 0) return ret;
    // }
    ret = i2c_burst_read_dt(spec, HUSB238_REG_SRC_PDO_5V, src_pdos, 6);
    if (ret < 0) return ret;

    return 0;
}

int husb238_print_src_capabilities(const uint8_t *src_pdos, const size_t len) {
    // Expecting 6 PDOs
    if (len != 6)
    {
        return -EINVAL;
    }

    for (size_t i = 0; i < len; i++) {
        if (!CHECK_SRC_DETECT_BIT(src_pdos[i]))
        {
            LOG_INF("%sV, not supported", voltage_strings[i]);
            continue;
        }
        else 
        {
            LOG_INF("%sV, Max current=%sA", voltage_strings[i], current_strings[PDO_CURRENT(src_pdos[i])]);
        }
    }
    return 0;
}

int husb238_init(const struct device *dev) {
    const struct husb238_config *cfg = dev->config;
    if (!i2c_is_ready_dt(&cfg->i2c)) {
        LOG_ERR("HUSB238 device is not ready");
        return -ENODEV;
    }
    else
    {
        LOG_INF("HUSB238 device is ready");
        return 0;
    }
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