// SPDX-License-Identifier: GPL-2.0-only
#include <linux/cpufreq.h>
#include <linux/init.h>
#include <linux/module.h>

static void cpufreq_gov_acacia_limits(struct cpufreq_policy *policy)
{
	/* Target the absolute Maximum frequency defined by hardware */
	__cpufreq_driver_target(policy, policy->max, CPUFREQ_RELATION_H);
}

static struct cpufreq_governor cpufreq_gov_acacia = {
	.name		= "acacia",
	.limits		= cpufreq_gov_acacia_limits,
	.owner		= THIS_MODULE,
};

MODULE_AUTHOR("techyguyperplexable <objecting@objecting.org>");
MODULE_DESCRIPTION("Acacia High Performance Governor");
MODULE_LICENSE("GPL");

#ifdef CONFIG_CPU_FREQ_DEFAULT_GOV_ACACIA
struct cpufreq_governor *cpufreq_default_governor(void)
{
	return &cpufreq_gov_acacia;
}
#endif

cpufreq_governor_init(cpufreq_gov_acacia);
cpufreq_governor_exit(cpufreq_gov_acacia);
