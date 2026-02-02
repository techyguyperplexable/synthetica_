/*
 *  drivers/devfreq/governor_acacia_batt.c
 *
 *  Copyright (C) 2024 Acacia
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 */

#include <linux/devfreq.h>
#include <linux/module.h>
#include "governor.h"

static int devfreq_acacia_batt_func(struct devfreq *df,
				  unsigned long *freq)
{
	/*
	 * Sets the frequency at the minimum available frequency.
	 */
	*freq = df->min_freq;
	return 0;
}

static int devfreq_acacia_batt_handler(struct devfreq *devfreq,
				unsigned int event, void *data)
{
	int ret = 0;

	if (event == DEVFREQ_GOV_START) {
		mutex_lock(&devfreq->lock);
		ret = update_devfreq(devfreq);
		mutex_unlock(&devfreq->lock);
	}

	return ret;
}

static struct devfreq_governor devfreq_acacia_batt = {
	.name = "acacia_batt",
	.get_target_freq = devfreq_acacia_batt_func,
	.event_handler = devfreq_acacia_batt_handler,
};

static int __init devfreq_acacia_batt_init(void)
{
	return devfreq_add_governor(&devfreq_acacia_batt);
}
subsys_initcall(devfreq_acacia_batt_init);

static void __exit devfreq_acacia_batt_exit(void)
{
	int ret;

	ret = devfreq_remove_governor(&devfreq_acacia_batt);
	if (ret)
		pr_err("%s: failed remove governor %d\n", __func__, ret);

	return;
}
module_exit(devfreq_acacia_batt_exit);
MODULE_LICENSE("GPL");
