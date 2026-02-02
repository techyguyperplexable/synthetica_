#include <linux/cpufreq.h>
#include <linux/init.h>
#include <linux/module.h>

static void cpufreq_gov_acacia_limits(struct cpufreq_policy *policy)
{
    if (policy->cur == policy->max)
        return;
    __cpufreq_driver_target(policy, policy->max, CPUFREQ_RELATION_H);
}

static struct cpufreq_governor cpufreq_gov_acacia = {
    .name       = "acacia",
    .limits     = cpufreq_gov_acacia_limits,
    .owner      = THIS_MODULE,
    /* Removed incompatible flags for 4.19 */
};

static int __init cpufreq_gov_acacia_init(void)
{
    return cpufreq_register_governor(&cpufreq_gov_acacia);
}

static void __exit cpufreq_gov_acacia_exit(void)
{
    cpufreq_unregister_governor(&cpufreq_gov_acacia);
}

MODULE_AUTHOR("techyguyperplexable <objecting@objecting.org>");
MODULE_DESCRIPTION("Acacia High Performance Governor");
MODULE_LICENSE("GPL");

#ifdef CONFIG_CPU_FREQ_DEFAULT_GOV_ACACIA
struct cpufreq_governor *cpufreq_default_governor(void)
{
    return &cpufreq_gov_acacia;
}
#endif

module_init(cpufreq_gov_acacia_init);
module_exit(cpufreq_gov_acacia_exit);
