// SPDX-License-Identifier: GPL-2.0-only
/* Hardware monitoring: read-only bridge to the initialized SoC sensor. */
#include <linux/device.h>
#include <linux/err.h>
#include <linux/hwmon.h>
#include <linux/init.h>
#include <linux/kernel.h>
#include <linux/math64.h>
#include <linux/platform_device.h>

/* The DAL checks switch_init before touching hardware; unlike the public
 * mapper dispatch it also remains safe before SDK initialization completes. */
extern int dal_dal9607c_switch_thermal_get(int *integer, int *fraction);

static umode_t phoebus_visible(const void *data, enum hwmon_sensor_types type,
			      u32 attr, int channel)
{
	return type == hwmon_temp &&
		(attr == hwmon_temp_input || attr == hwmon_temp_label) ? 0444 : 0;
}

static int phoebus_read(struct device *dev, enum hwmon_sensor_types type,
			u32 attr, int channel, long *value)
{
	int integer, fraction;
	s64 micro;

	if (type != hwmon_temp || attr != hwmon_temp_input || channel)
		return -EOPNOTSUPP;
	if (dal_dal9607c_switch_thermal_get(&integer, &fraction))
		return -EAGAIN;
	micro = (s64)integer * 1000000 + fraction;
	if (micro < -100000000 || micro > 250000000)
		return -ERANGE;
	*value = div_s64(micro, 1000);
	return 0;
}

static int phoebus_read_string(struct device *dev, enum hwmon_sensor_types type,
			       u32 attr, int channel, const char **value)
{
	if (type != hwmon_temp || attr != hwmon_temp_label || channel)
		return -EOPNOTSUPP;
	*value = "SoC Temperature";
	return 0;
}

static const struct hwmon_ops phoebus_ops = {
	.is_visible = phoebus_visible,
	.read = phoebus_read,
	.read_string = phoebus_read_string,
};
static const struct hwmon_channel_info * const phoebus_info[] = {
	HWMON_CHANNEL_INFO(temp, HWMON_T_INPUT | HWMON_T_LABEL),
	NULL,
};
static const struct hwmon_chip_info phoebus_chip = {
	.ops = &phoebus_ops,
	.info = phoebus_info,
};

static int __init phoebus_hwmon_probe(struct platform_device *pdev)
{
	struct device *dev;

	dev = devm_hwmon_device_register_with_info(&pdev->dev, "rtl9607c", NULL,
						 &phoebus_chip, NULL);
	if (IS_ERR(dev))
		return dev_err_probe(&pdev->dev, PTR_ERR(dev),
				     "cannot register SoC temperature sensor\n");

	dev_info(&pdev->dev, "read-only SoC temperature sensor registered\n");
	return 0;
}

static struct platform_driver phoebus_hwmon_driver = {
	.driver = {
		.name = "phoebus-hwmon",
	},
};

static int __init phoebus_hwmon_init(void)
{
	struct platform_device *pdev;

	/* The platform bundle provides the mandatory hwmon parent and binds
	 * synchronously, unwinding both device and driver on probe failure.
	 * Do not read the DAL here: SDK initialization may still be pending.
	 */
	pdev = platform_create_bundle(&phoebus_hwmon_driver,
				     phoebus_hwmon_probe, NULL, 0, NULL, 0);
	if (IS_ERR(pdev)) {
		pr_err("phoebus-hwmon: registration failed: %ld\n", PTR_ERR(pdev));
		return PTR_ERR(pdev);
	}
	return 0;
}
late_initcall(phoebus_hwmon_init);
