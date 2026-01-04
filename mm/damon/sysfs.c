// SPDX-License-Identifier: GPL-2.0
/*
 * DAMON sysfs Interface (simplified for 4.19/5.15 API)
 *
 * Copyright (c) 2022 SeongJae Park <sj@kernel.org>
 * Adapted for 4.19 by: techyguyperplexable
 *
 * This is a simplified sysfs interface for DAMON that works with the
 * 5.15-style DAMON API backported to 4.19. It provides basic monitoring
 * controls via /sys/kernel/mm/damon/
 */

#define pr_fmt(fmt) "damon-sysfs: " fmt

#include <linux/damon.h>
#include <linux/kobject.h>
#include <linux/sched.h>
#include <linux/sched/mm.h>
#include <linux/sched/signal.h>
#include <linux/slab.h>
#include <linux/sysfs.h>
#include <linux/pid.h>

static DEFINE_MUTEX(damon_sysfs_lock);

/* Single monitoring context for simplicity */
static struct damon_ctx *damon_sysfs_ctx;
static bool damon_sysfs_running;

/* Monitoring parameters */
static unsigned long sample_interval = 5000;	/* 5ms default */
static unsigned long aggr_interval = 100000;	/* 100ms default */
static unsigned long update_interval = 1000000;	/* 1s default */
static unsigned long min_nr_regions = 10;
static unsigned long max_nr_regions = 1000;
static unsigned long target_pid;

/*
 * Sysfs attributes
 */

static ssize_t sample_interval_show(struct kobject *kobj,
		struct kobj_attribute *attr, char *buf)
{
	return sysfs_emit(buf, "%lu\n", sample_interval);
}

static ssize_t sample_interval_store(struct kobject *kobj,
		struct kobj_attribute *attr, const char *buf, size_t count)
{
	int err;

	err = kstrtoul(buf, 0, &sample_interval);
	if (err)
		return -EINVAL;
	return count;
}

static ssize_t aggr_interval_show(struct kobject *kobj,
		struct kobj_attribute *attr, char *buf)
{
	return sysfs_emit(buf, "%lu\n", aggr_interval);
}

static ssize_t aggr_interval_store(struct kobject *kobj,
		struct kobj_attribute *attr, const char *buf, size_t count)
{
	int err;

	err = kstrtoul(buf, 0, &aggr_interval);
	if (err)
		return -EINVAL;
	return count;
}

static ssize_t update_interval_show(struct kobject *kobj,
		struct kobj_attribute *attr, char *buf)
{
	return sysfs_emit(buf, "%lu\n", update_interval);
}

static ssize_t update_interval_store(struct kobject *kobj,
		struct kobj_attribute *attr, const char *buf, size_t count)
{
	int err;

	err = kstrtoul(buf, 0, &update_interval);
	if (err)
		return -EINVAL;
	return count;
}

static ssize_t min_nr_regions_show(struct kobject *kobj,
		struct kobj_attribute *attr, char *buf)
{
	return sysfs_emit(buf, "%lu\n", min_nr_regions);
}

static ssize_t min_nr_regions_store(struct kobject *kobj,
		struct kobj_attribute *attr, const char *buf, size_t count)
{
	int err;

	err = kstrtoul(buf, 0, &min_nr_regions);
	if (err)
		return -EINVAL;
	return count;
}

static ssize_t max_nr_regions_show(struct kobject *kobj,
		struct kobj_attribute *attr, char *buf)
{
	return sysfs_emit(buf, "%lu\n", max_nr_regions);
}

static ssize_t max_nr_regions_store(struct kobject *kobj,
		struct kobj_attribute *attr, const char *buf, size_t count)
{
	int err;

	err = kstrtoul(buf, 0, &max_nr_regions);
	if (err)
		return -EINVAL;
	return count;
}

static ssize_t target_pid_show(struct kobject *kobj,
		struct kobj_attribute *attr, char *buf)
{
	return sysfs_emit(buf, "%lu\n", target_pid);
}

static ssize_t target_pid_store(struct kobject *kobj,
		struct kobj_attribute *attr, const char *buf, size_t count)
{
	int err;

	if (damon_sysfs_running)
		return -EBUSY;

	err = kstrtoul(buf, 0, &target_pid);
	if (err)
		return -EINVAL;
	return count;
}

static ssize_t state_show(struct kobject *kobj,
		struct kobj_attribute *attr, char *buf)
{
	return sysfs_emit(buf, "%s\n", damon_sysfs_running ? "on" : "off");
}

/*
 * Find the most memory-intensive user process
 * Returns the PID or 0 if none found
 */
static pid_t find_intensive_process(void)
{
	struct task_struct *task, *max_task = NULL;
	unsigned long max_rss = 0;
	pid_t result = 0;

	rcu_read_lock();
	for_each_process(task) {
		struct mm_struct *mm;
		unsigned long rss;

		/* Skip kernel threads and init */
		if (task->flags & PF_KTHREAD)
			continue;
		if (task->pid <= 1)
			continue;

		mm = task->mm;
		if (!mm)
			continue;

		/* Get RSS (resident set size) as memory intensity metric */
		rss = get_mm_rss(mm);
		if (rss > max_rss) {
			max_rss = rss;
			max_task = task;
		}
	}

	if (max_task) {
		result = max_task->pid;
		printk(KERN_INFO "DAMON: auto-selected pid %d (%s) with RSS %lu pages\n",
		       result, max_task->comm, max_rss);
	}
	rcu_read_unlock();

	return result;
}

static int damon_sysfs_turn_on(void)
{
	int err;
	struct pid *pid;
	unsigned long pid_ptr;
	unsigned long actual_pid = target_pid;

	printk(KERN_ERR "DAMON: " "sysfs: turn_on called, target_pid=%lu\n", target_pid);

	if (damon_sysfs_running) {
		printk(KERN_ERR "DAMON: " "sysfs: already running\n");
		return -EBUSY;
	}

	/* Auto-find intensive process if no PID specified */
	if (!actual_pid) {
		actual_pid = find_intensive_process();
		if (!actual_pid) {
			printk(KERN_ERR "DAMON: " "sysfs: no suitable process found\n");
			return -ESRCH;
		}
	}

	/* Convert PID number to struct pid pointer */
	pid = find_get_pid((pid_t)actual_pid);
	if (!pid) {
		printk(KERN_ERR "DAMON: " "sysfs: find_get_pid failed for pid %lu\n", actual_pid);
		return -ESRCH;
	}
	printk(KERN_ERR "DAMON: " "sysfs: got struct pid %px for pid %lu\n", pid, actual_pid);

	damon_sysfs_ctx = damon_new_ctx();
	if (!damon_sysfs_ctx) {
		printk(KERN_ERR "DAMON: " "sysfs: damon_new_ctx failed\n");
		put_pid(pid);
		return -ENOMEM;
	}

	/* Set primitives for virtual address monitoring */
	damon_va_set_primitives(damon_sysfs_ctx);
	printk(KERN_ERR "DAMON: " "sysfs: primitives set\n");

	/* Set monitoring attributes */
	err = damon_set_attrs(damon_sysfs_ctx, sample_interval, aggr_interval,
			update_interval, min_nr_regions, max_nr_regions);
	if (err) {
		printk(KERN_ERR "DAMON: " "sysfs: damon_set_attrs failed: %d\n", err);
		goto destroy_ctx;
	}
	printk(KERN_ERR "DAMON: " "sysfs: attrs set\n");

	/* Set target - pass struct pid pointer cast to unsigned long */
	pid_ptr = (unsigned long)pid;
	err = damon_set_targets(damon_sysfs_ctx, &pid_ptr, 1);
	if (err) {
		printk(KERN_ERR "DAMON: " "sysfs: damon_set_targets failed: %d\n", err);
		goto destroy_ctx;
	}
	printk(KERN_ERR "DAMON: " "sysfs: targets set\n");

	/* Start monitoring */
	err = damon_start(&damon_sysfs_ctx, 1);
	if (err) {
		printk(KERN_ERR "DAMON: " "sysfs: damon_start failed: %d\n", err);
		goto destroy_ctx;
	}

	damon_sysfs_running = true;
	printk(KERN_ERR "DAMON: " "sysfs: monitoring started for pid %lu, running=%d\n",
		target_pid, damon_sysfs_running);
	return 0;

destroy_ctx:
	damon_destroy_ctx(damon_sysfs_ctx);
	damon_sysfs_ctx = NULL;
	put_pid(pid);
	return err;
}

static int damon_sysfs_turn_off(void)
{
	int err;
	struct damon_target *t;

	printk(KERN_ERR "DAMON: " "sysfs: turn_off called, running=%d\n", damon_sysfs_running);

	if (!damon_sysfs_running)
		return -EINVAL;

	err = damon_stop(&damon_sysfs_ctx, 1);
	if (err) {
		printk(KERN_ERR "DAMON: " "sysfs: damon_stop failed: %d\n", err);
		return err;
	}

	/* Release the struct pid references */
	damon_for_each_target(t, damon_sysfs_ctx)
		put_pid((struct pid *)t->id);

	damon_destroy_ctx(damon_sysfs_ctx);
	damon_sysfs_ctx = NULL;
	damon_sysfs_running = false;
	printk(KERN_ERR "DAMON: " "sysfs: monitoring stopped\n");
	return 0;
}

static ssize_t state_store(struct kobject *kobj,
		struct kobj_attribute *attr, const char *buf, size_t count)
{
	int err = 0;

	printk(KERN_ERR "DAMON: " "sysfs: state_store called with '%s'\n", buf);

	mutex_lock(&damon_sysfs_lock);

	if (sysfs_streq(buf, "on")) {
		printk(KERN_ERR "DAMON: " "sysfs: calling turn_on\n");
		err = damon_sysfs_turn_on();
		printk(KERN_ERR "DAMON: " "sysfs: turn_on returned %d\n", err);
	} else if (sysfs_streq(buf, "off")) {
		printk(KERN_ERR "DAMON: " "sysfs: calling turn_off\n");
		err = damon_sysfs_turn_off();
		printk(KERN_ERR "DAMON: " "sysfs: turn_off returned %d\n", err);
	} else {
		printk(KERN_ERR "DAMON: " "sysfs: invalid input\n");
		err = -EINVAL;
	}

	mutex_unlock(&damon_sysfs_lock);

	printk(KERN_ERR "DAMON: " "sysfs: state_store returning %d\n", err ? err : (int)count);
	if (err)
		return err;
	return count;
}

static ssize_t nr_regions_show(struct kobject *kobj,
		struct kobj_attribute *attr, char *buf)
{
	struct damon_target *t;
	unsigned int nr = 0;

	mutex_lock(&damon_sysfs_lock);
	if (damon_sysfs_ctx && damon_sysfs_running) {
		damon_for_each_target(t, damon_sysfs_ctx)
			nr += damon_nr_regions(t);
	}
	mutex_unlock(&damon_sysfs_lock);

	return sysfs_emit(buf, "%u\n", nr);
}

/* Attribute definitions */
static struct kobj_attribute sample_interval_attr =
	__ATTR(sample_interval, 0644, sample_interval_show, sample_interval_store);
static struct kobj_attribute aggr_interval_attr =
	__ATTR(aggr_interval, 0644, aggr_interval_show, aggr_interval_store);
static struct kobj_attribute update_interval_attr =
	__ATTR(update_interval, 0644, update_interval_show, update_interval_store);
static struct kobj_attribute min_nr_regions_attr =
	__ATTR(min_nr_regions, 0644, min_nr_regions_show, min_nr_regions_store);
static struct kobj_attribute max_nr_regions_attr =
	__ATTR(max_nr_regions, 0644, max_nr_regions_show, max_nr_regions_store);
static struct kobj_attribute target_pid_attr =
	__ATTR(target_pid, 0644, target_pid_show, target_pid_store);
static struct kobj_attribute state_attr =
	__ATTR(state, 0644, state_show, state_store);
static struct kobj_attribute nr_regions_attr =
	__ATTR_RO(nr_regions);

static struct attribute *damon_sysfs_attrs[] = {
	&sample_interval_attr.attr,
	&aggr_interval_attr.attr,
	&update_interval_attr.attr,
	&min_nr_regions_attr.attr,
	&max_nr_regions_attr.attr,
	&target_pid_attr.attr,
	&state_attr.attr,
	&nr_regions_attr.attr,
	NULL,
};

static struct attribute_group damon_sysfs_group = {
	.attrs = damon_sysfs_attrs,
};

static struct kobject *damon_sysfs_kobj;

static int __init damon_sysfs_init(void)
{
	int err;

	damon_sysfs_kobj = kobject_create_and_add("damon", mm_kobj);
	if (!damon_sysfs_kobj)
		return -ENOMEM;

	err = sysfs_create_group(damon_sysfs_kobj, &damon_sysfs_group);
	if (err) {
		kobject_put(damon_sysfs_kobj);
		return err;
	}

	printk(KERN_ERR "DAMON: " "sysfs interface registered at /sys/kernel/mm/damon\n");
	return 0;
}
subsys_initcall(damon_sysfs_init);
