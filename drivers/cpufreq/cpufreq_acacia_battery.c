#include <linux/cpufreq.h>
#include <linux/init.h>
#include <linux/module.h>
#include <linux/slab.h>

struct acacia_battery_policy {
	struct cpufreq_policy *policy;
	unsigned int cached_min;
};

static DEFINE_PER_CPU(struct acacia_battery_policy *, acacia_battery_policy);

static int cpufreq_gov_acacia_battery_init(struct cpufreq_policy *policy)
{
	struct acacia_battery_policy *ap;

	ap = kzalloc(sizeof(*ap), GFP_KERNEL);
	if (!ap)
		return -ENOMEM;

	ap->policy = policy;
	ap->cached_min = policy->min;
	per_cpu(acacia_battery_policy, policy->cpu) = ap;

	return 0;
}

static void cpufreq_gov_acacia_battery_exit(struct cpufreq_policy *policy)
{
	struct acacia_battery_policy *ap = per_cpu(acacia_battery_policy, policy->cpu);

	if (ap) {
		per_cpu(acacia_battery_policy, policy->cpu) = NULL;
		kfree(ap);
	}
}

static int cpufreq_gov_acacia_battery_start(struct cpufreq_policy *policy)
{
	struct acacia_battery_policy *ap = per_cpu(acacia_battery_policy, policy->cpu);

	if (ap)
		ap->cached_min = policy->min;

	__cpufreq_driver_target(policy, policy->min, CPUFREQ_RELATION_L);
	return 0;
}

static void cpufreq_gov_acacia_battery_limits(struct cpufreq_policy *policy)
{
	struct acacia_battery_policy *ap = per_cpu(acacia_battery_policy, policy->cpu);

	if (likely(ap && policy->min == ap->cached_min && policy->cur == policy->min))
		return;

	if (ap)
		ap->cached_min = policy->min;

	__cpufreq_driver_target(policy, policy->min, CPUFREQ_RELATION_L);
}

static struct cpufreq_governor cpufreq_gov_acacia_battery = {
	.name		= "acacia-battery",
	.init		= cpufreq_gov_acacia_battery_init,
	.exit		= cpufreq_gov_acacia_battery_exit,
	.start		= cpufreq_gov_acacia_battery_start,
	.limits		= cpufreq_gov_acacia_battery_limits,
	.owner		= THIS_MODULE,
};

static int __init cpufreq_gov_acacia_battery_register(void)
{
	return cpufreq_register_governor(&cpufreq_gov_acacia_battery);
}

static void __exit cpufreq_gov_acacia_battery_unregister(void)
{
	cpufreq_unregister_governor(&cpufreq_gov_acacia_battery);
}

MODULE_AUTHOR("techyguyperplexable <objecting@objecting.org>");
MODULE_DESCRIPTION("Acacia Battery Governor - Minimum frequency for maximum battery life");
MODULE_LICENSE("GPL");

#ifdef CONFIG_CPU_FREQ_DEFAULT_GOV_ACACIA_BATTERY
struct cpufreq_governor *cpufreq_default_governor(void)
{
	return &cpufreq_gov_acacia_battery;
}
#endif

core_initcall(cpufreq_gov_acacia_battery_register);
module_exit(cpufreq_gov_acacia_battery_unregister);
