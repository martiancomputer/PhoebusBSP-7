// SPDX-License-Identifier: GPL-2.0-only
/* Boot LED indication; userspace takes ownership after the flash/off cycle. */
#include <linux/device.h>
#include <linux/leds.h>
#include <linux/module.h>
#include <linux/of.h>
#include <linux/platform_device.h>
#include <linux/slab.h>
#include <linux/workqueue.h>

struct phoebus_boot_leds {
	struct led_classdev *led[7];
	struct delayed_work work;
	unsigned int phase;
	bool ready;
	bool on;
};

static void boot_led_work(struct work_struct *work)
{
	struct phoebus_boot_leds *boot = container_of(to_delayed_work(work),
						struct phoebus_boot_leds, work);
	unsigned int delay = 250;
	int i;

	if (READ_ONCE(boot->phase) < 2) {
		for (i = 0; i < ARRAY_SIZE(boot->led); i++)
			led_set_brightness_sync(boot->led[i],
				boot->phase == 0 && i != 5 ? LED_FULL : LED_OFF);
		WRITE_ONCE(boot->phase, boot->phase + 1);
		delay = 300;
	} else if (READ_ONCE(boot->ready)) {
		led_set_brightness_sync(boot->led[0], LED_FULL);
		WRITE_ONCE(boot->phase, 3);
		return;
	} else {
		boot->on = !boot->on;
		led_set_brightness_sync(boot->led[0], boot->on ? LED_FULL : LED_OFF);
	}
	schedule_delayed_work(&boot->work, msecs_to_jiffies(delay));
}

static ssize_t userspace_ready_show(struct device *dev,
				   struct device_attribute *attr, char *buf)
{
	struct phoebus_boot_leds *boot = dev_get_drvdata(dev);
	return sysfs_emit(buf, "%u\n", READ_ONCE(boot->ready));
}

static ssize_t userspace_ready_store(struct device *dev,
				    struct device_attribute *attr,
				    const char *buf, size_t count)
{
	struct phoebus_boot_leds *boot = dev_get_drvdata(dev);
	bool ready;
	if (kstrtobool(buf, &ready) || !ready)
		return -EINVAL;
	WRITE_ONCE(boot->ready, true);
	return count;
}
static DEVICE_ATTR_RW(userspace_ready);

static ssize_t phase_show(struct device *dev, struct device_attribute *attr,
			 char *buf)
{
	struct phoebus_boot_leds *boot = dev_get_drvdata(dev);
	return sysfs_emit(buf, "%u\n", READ_ONCE(boot->phase));
}
static DEVICE_ATTR_RO(phase);

static struct attribute *boot_led_attrs[] = {
	&dev_attr_userspace_ready.attr, &dev_attr_phase.attr, NULL,
};
ATTRIBUTE_GROUPS(boot_led);

static void boot_led_cancel(void *data)
{
	struct phoebus_boot_leds *boot = data;
	cancel_delayed_work_sync(&boot->work);
}

static int boot_led_probe(struct platform_device *pdev)
{
	struct device *dev = &pdev->dev;
	struct phoebus_boot_leds *boot;
	int i, ret;

	boot = devm_kzalloc(dev, sizeof(*boot), GFP_KERNEL);
	if (!boot)
		return -ENOMEM;
	for (i = 0; i < ARRAY_SIZE(boot->led); i++) {
		boot->led[i] = devm_of_led_get(dev, i);
		if (IS_ERR(boot->led[i]))
			return dev_err_probe(dev, PTR_ERR(boot->led[i]),
					     "LED %d not available\n", i);
	}
	INIT_DELAYED_WORK(&boot->work, boot_led_work);
	platform_set_drvdata(pdev, boot);
	/* Cancel before the devm LED references and state are released. */
	ret = devm_add_action_or_reset(dev, boot_led_cancel, boot);
	if (ret)
		return ret;
	schedule_delayed_work(&boot->work, 0);
	return 0;
}

static const struct of_device_id boot_led_match[] = {
	{ .compatible = "phoebus,boot-leds" }, { }
};
MODULE_DEVICE_TABLE(of, boot_led_match);

static struct platform_driver boot_led_driver = {
	.probe = boot_led_probe,
	.driver = {
		.name = "phoebus-boot-leds",
		.of_match_table = boot_led_match,
		.dev_groups = boot_led_groups,
	},
};
module_platform_driver(boot_led_driver);
MODULE_LICENSE("GPL");
