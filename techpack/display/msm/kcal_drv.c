#include <linux/module.h>
#include <linux/platform_device.h>
#include <linux/types.h>
#include <linux/moduleparam.h>

unsigned int kcal_red = 256;
unsigned int kcal_green = 256;
unsigned int kcal_blue = 256;
unsigned int kcal_sat = 255;
unsigned int kcal_hue = 0;
unsigned int kcal_val = 255;
unsigned int kcal_cont = 255;

EXPORT_SYMBOL_GPL(kcal_red);
EXPORT_SYMBOL_GPL(kcal_green);
EXPORT_SYMBOL_GPL(kcal_blue);
EXPORT_SYMBOL_GPL(kcal_sat);
EXPORT_SYMBOL_GPL(kcal_hue);
EXPORT_SYMBOL_GPL(kcal_val);
EXPORT_SYMBOL_GPL(kcal_cont);

module_param(kcal_red, uint, 0644);
module_param(kcal_green, uint, 0644);
module_param(kcal_blue, uint, 0644);
module_param(kcal_hue, uint, 0644);
module_param(kcal_sat, uint, 0644);
module_param(kcal_val, uint, 0644);
module_param(kcal_cont, uint, 0644);

static ssize_t kcal_show(struct device *dev, struct device_attribute *attr, char *buf)
{
	return sprintf(buf, "%u %u %u\n", kcal_red, kcal_green, kcal_blue);
}

static ssize_t kcal_store(struct device *dev, struct device_attribute *attr, const char *buf, size_t count)
{
	if (sscanf(buf, "%u %u %u", &kcal_red, &kcal_green, &kcal_blue) != 3)
		return -EINVAL;
	return count;
}

static ssize_t kcal_helper_show(struct device *dev, struct device_attribute *attr, char *buf, unsigned int *val)
{
	return sprintf(buf, "%u\n", *val);
}

static ssize_t kcal_helper_store(struct device *dev, struct device_attribute *attr, const char *buf, size_t count, unsigned int *val)
{
	unsigned int temp;
	if (sscanf(buf, "%u", &temp) != 1)
		return -EINVAL;
	*val = temp;
	return count;
}

#define KCAL_ATTR_RW(name, target) \
static ssize_t name##_show(struct device *dev, struct device_attribute *attr, char *buf) \
{ return kcal_helper_show(dev, attr, buf, &target); } \
static ssize_t name##_store(struct device *dev, struct device_attribute *attr, const char *buf, size_t count) \
{ return kcal_helper_store(dev, attr, buf, count, &target); } \
static DEVICE_ATTR_RW(name);

static DEVICE_ATTR_RW(kcal);
KCAL_ATTR_RW(kcal_sat, kcal_sat);
KCAL_ATTR_RW(kcal_hue, kcal_hue);
KCAL_ATTR_RW(kcal_val, kcal_val);
KCAL_ATTR_RW(kcal_cont, kcal_cont);

static struct attribute *kcal_attrs[] = {
	&dev_attr_kcal.attr,
	&dev_attr_kcal_sat.attr,
	&dev_attr_kcal_hue.attr,
	&dev_attr_kcal_val.attr,
	&dev_attr_kcal_cont.attr,
	NULL
};

static struct attribute_group kcal_attr_group = {
	.attrs = kcal_attrs,
};

static int kcal_probe(struct platform_device *pdev)
{
	return sysfs_create_group(&pdev->dev.kobj, &kcal_attr_group);
}

static int kcal_remove(struct platform_device *pdev)
{
	sysfs_remove_group(&pdev->dev.kobj, &kcal_attr_group);
	return 0;
}

static struct platform_driver kcal_driver = {
	.probe = kcal_probe,
	.remove = kcal_remove,
	.driver = { .name = "kcal_ctrl", },
};

static struct platform_device kcal_device = {
	.name = "kcal_ctrl",
	.id = 0,
};

static int __init kcal_init(void)
{
	platform_device_register(&kcal_device);
	return platform_driver_register(&kcal_driver);
}

module_init(kcal_init);
MODULE_LICENSE("GPL v2");
