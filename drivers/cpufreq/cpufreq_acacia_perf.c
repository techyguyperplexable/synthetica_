#include <linux/cpufreq.h>
#include <linux/init.h>
#include <linux/module.h>
#include <linux/slab.h>

struct acacia_perf_policy {
	struct cpufreq_policy *policy;
	unsigned int cached_max;
};

static DEFINE_PER_CPU(struct acacia_perf_policy *, acacia_perf_policy);

static int cpufreq_gov_acacia_perf_init(struct cpufreq_policy *policy)
{
	struct acacia_perf_policy *ap;

	ap = kzalloc(sizeof(*ap), GFP_KERNEL);
	if (!ap)
		return -ENOMEM;

	ap->policy = policy;
	ap->cached_max = policy->max;
	per_cpu(acacia_perf_policy, policy->cpu) = ap;

	return 0;
}

static void cpufreq_gov_acacia_perf_exit(struct cpufreq_policy *policy)
{
	struct acacia_perf_policy *ap = per_cpu(acacia_perf_policy, policy->cpu);

	if (ap) {
		per_cpu(acacia_perf_policy, policy->cpu) = NULL;
		kfree(ap);
	}
}

static int cpufreq_gov_acacia_perf_start(struct cpufreq_policy *policy)
{
	struct acacia_perf_policy *ap = per_cpu(acacia_perf_policy, policy->cpu);

	if (ap)
		ap->cached_max = policy->max;

	__cpufreq_driver_target(policy, policy->max, CPUFREQ_RELATION_H);
	return 0;
}

static void cpufreq_gov_acacia_perf_limits(struct cpufreq_policy *policy)
{
	struct acacia_perf_policy *ap = per_cpu(acacia_perf_policy, policy->cpu);

	if (likely(ap && policy->max == ap->cached_max && policy->cur == policy->max))
		return;

	if (ap)
		ap->cached_max = policy->max;

	__cpufreq_driver_target(policy, policy->max, CPUFREQ_RELATION_H);
}

static struct cpufreq_governor cpufreq_gov_acacia_perf = {
	.name		= "acacia-perf",
	.init		= cpufreq_gov_acacia_perf_init,
	.exit		= cpufreq_gov_acacia_perf_exit,
	.start		= cpufreq_gov_acacia_perf_start,
	.limits		= cpufreq_gov_acacia_perf_limits,
	.owner		= THIS_MODULE,
};

static int __init cpufreq_gov_acacia_perf_register(void)
{
	return cpufreq_register_governor(&cpufreq_gov_acacia_perf);
}

static void __exit cpufreq_gov_acacia_perf_unregister(void)
{
	cpufreq_unregister_governor(&cpufreq_gov_acacia_perf);
}

MODULE_AUTHOR("techyguyperplexable <objecting@objecting.org>");
MODULE_DESCRIPTION("Acacia Performance Governor - Maximum frequency with minimal overhead");
MODULE_LICENSE("GPL");

#ifdef CONFIG_CPU_FREQ_DEFAULT_GOV_ACACIA_PERF
struct cpufreq_governor *cpufreq_default_governor(void)
{
	return &cpufreq_gov_acacia_perf;
}
#endif

core_initcall(cpufreq_gov_acacia_perf_register);
module_exit(cpufreq_gov_acacia_perf_unregister);
