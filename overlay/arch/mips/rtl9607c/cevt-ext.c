/*
 * Copyright 2019, Realtek Semiconductor Corp.
 *
 * Andrew Chang <yachang@realtek.com>
 *
 */

#include <linux/clk.h>
#include <linux/clockchips.h>
#include <linux/cpu.h>
#include <linux/init.h>
#include <linux/interrupt.h>
#include <linux/irqchip/mips-gic.h>
#include <linux/notifier.h>
#include <linux/of_irq.h>
#include <linux/of_address.h>
#include <linux/sched_clock.h>
#include <linux/percpu.h>
#include <linux/smp.h>
#include <linux/time.h>
#include <linux/clkdev.h>

#define NAME "rtktimer"
#define MHZ 1000000
#define TIMER_FREQ 10000000 //10 MHz
#define TC_MAX 9
#define RTK_CLOCK_DEFAULT_FACTOR 2
static volatile struct rtk_hw_timer_reg *rtk_csrc_base;

struct rtk_hw_timer_reg {
	u32 tc_data;
	u32 tc_value;
	u32 tc_ctrl;
	u32 tc_intr;
};
static u32 rtk_timer_base_freq;

struct rtk_cevt_device {
	void __iomem *base;
	struct clock_event_device dev;
	raw_spinlock_t lock;
	bool irq_requested;
};

static u32 tick_div;
static u32 clk_freq;
static u32 hw_timer_used_idx = 0;
static DEFINE_PER_CPU(struct rtk_cevt_device, rtk_cdev);

static u64 rtk_timer_read(struct clocksource *cs)
{
	return (u64)rtk_csrc_base->tc_value;
}

static u64 notrace rtk_read_sched_clock(void)
{
    return (u64)rtk_csrc_base->tc_value;
}

static struct clocksource rtk_clocksource = {
	.name   = "rtk-timer",
	.read   = rtk_timer_read,
	.mask   = CLOCKSOURCE_MASK(28),
	.flags  = CLOCK_SOURCE_IS_CONTINUOUS,
	.rating = 300,
};

static void rtk_clocksource_setup(void *base, u32 div) {

	u32 freq;

	if (rtk_csrc_base) {
		printk(NAME ": is already registered\n");
		return;
	}

	if ((div < 2) || (div > 0xffff)) {
		printk(NAME ": invalid div %u\n", div);
		return;
	}

	rtk_csrc_base = base;

	rtk_csrc_base->tc_ctrl = 0;
	rtk_csrc_base->tc_intr = 0;
	rtk_csrc_base->tc_data = CLOCKSOURCE_MASK(28);
	rtk_csrc_base->tc_ctrl = BIT(28) | BIT(24) | div;

	freq = rtk_timer_base_freq/div;
	if (clocksource_register_hz(&rtk_clocksource, freq)) {
		printk(NAME ": failed to register %s\n", rtk_clocksource.name);
		rtk_csrc_base = NULL;
	}

	sched_clock_register(rtk_read_sched_clock, 28, freq);
}

static void __rtk_cevt_shutdown(struct rtk_cevt_device *rdev) {
	u32 val;
	struct rtk_hw_timer_reg *reg = rdev->base;

	writel(0, &reg->tc_ctrl);
	wmb();
	val = readl(&reg->tc_intr) & ~BIT(20);
	writel(val, &reg->tc_intr);
}

static int rtk_cevt_shutdown(struct clock_event_device *cdev){
	struct rtk_cevt_device *rdev = container_of(cdev, struct rtk_cevt_device, dev);

	raw_spin_lock(&rdev->lock);
	__rtk_cevt_shutdown(rdev);
	raw_spin_unlock(&rdev->lock);

	return 0;
}

static int rtk_cevt_set_periodic(struct clock_event_device *cdev){
	struct rtk_cevt_device *rdev = container_of(cdev, struct rtk_cevt_device, dev);
	struct rtk_hw_timer_reg *reg = rdev->base;
	u32 val;

	raw_spin_lock(&rdev->lock);
	__rtk_cevt_shutdown(rdev);

	val = DIV_ROUND_UP(TIMER_FREQ, HZ);
	writel(val, &reg->tc_data);

	val = BIT(20);
	writel(val, &reg->tc_intr);

	val = BIT(28) | BIT(24) | tick_div;
	wmb();
	writel(val, &reg->tc_ctrl);
	raw_spin_unlock(&rdev->lock);

	return 0;
}

static int rtk_cevt_next_event(unsigned long event, struct clock_event_device *cdev) {
	struct rtk_cevt_device *rdev = container_of(cdev, struct rtk_cevt_device, dev);
	struct rtk_hw_timer_reg *reg = rdev->base;
	u32 val, tmp;
	int cnt;

	raw_spin_lock(&rdev->lock);

reset:
	__rtk_cevt_shutdown(rdev);

	writel(event, &reg->tc_data);

	val = BIT(20);
	writel(val, &reg->tc_intr);

	val = BIT(28) | tick_div;
	wmb();
	writel(val, &reg->tc_ctrl);

	//check tc_vaule for HW flaw
	cnt = 0;
	while(cnt < 10 && !(tmp = readl(&reg->tc_value))) {
		cnt++;
	}
	//reset timer if the HW flaw is detected
	if (!readl(&reg->tc_value) && !(readl(&reg->tc_intr) & BIT(16))) {
		goto reset;
	}

	raw_spin_unlock(&rdev->lock);

	return 0;
}

static irqreturn_t rtk_timer_interrupt(int irq, void *dev_id) {
	struct rtk_cevt_device *rdev = this_cpu_ptr(&rtk_cdev);
	//struct rtk_cevt_device *rdev = dev_id;
	struct rtk_hw_timer_reg *reg = rdev->base;
	u32 val;

	val = readl(&reg->tc_intr);
	writel(val, &reg->tc_intr);

	rdev->dev.event_handler(&rdev->dev);

	return IRQ_HANDLED;
}

static void rtk_clockevent_setup(struct rtk_cevt_device *cd) {
	int cpu;

	cpu = smp_processor_id();
	cd = this_cpu_ptr(&rtk_cdev);

	cd->dev.name    = "rtk-cevt";
	cd->dev.rating  = 600;
	cd->dev.features = CLOCK_EVT_FEAT_PERIODIC | CLOCK_EVT_FEAT_ONESHOT;
	cd->dev.cpumask = cpumask_of(cpu);

	cd->dev.set_state_shutdown = rtk_cevt_shutdown;
	cd->dev.set_state_periodic = rtk_cevt_set_periodic;
	cd->dev.set_state_oneshot = rtk_cevt_shutdown;
	cd->dev.tick_resume = rtk_cevt_shutdown;
	cd->dev.set_next_event = rtk_cevt_next_event;
	raw_spin_lock_init(&cd->lock);

	clockevents_config_and_register(&cd->dev, TIMER_FREQ, 0x3, 0xfffffff);
}

/*
 * Runs on each CPU as it comes online (and once per already-online CPU
 * at registration time).  Each CPU owns a dedicated HW timer block whose
 * GIC shared interrupt is routed to that CPU via the normal affinity API,
 * replacing the old out-of-tree mips_gic_init_one() hack.
 */
static int rtk_timer_online_cpu(unsigned int cpu)
{
	struct rtk_cevt_device *cd = per_cpu_ptr(&rtk_cdev, cpu);
	int ret;

	if (cd->dev.irq && !cd->irq_requested) {
		ret = request_irq(cd->dev.irq, rtk_timer_interrupt,
				  IRQF_TIMER | IRQF_NOBALANCING,
				  "rtk-tick", cd);
		if (ret) {
			pr_err(NAME ": cpu%u: request_irq(%d) failed: %d\n",
			       cpu, cd->dev.irq, ret);
			return ret;
		}
		cd->irq_requested = true;
		irq_force_affinity(cd->dev.irq, cpumask_of(cpu));
	}

	rtk_clockevent_setup(cd);
	return 0;
}

static void __init rtk_clocksource_of_init(struct device_node *node)
{
	struct clk *clk;
	int cpu, ret;
	u64 size;
	const __be32 *addr;
	unsigned int flags;
	void __iomem *mem;

	clk = of_clk_get(node, 0);
	if (IS_ERR(clk))
		panic("unable to get clock, err=%ld", PTR_ERR(clk));

	clk_freq = clk_get_rate(clk);
	printk(NAME ": base freq=%u\n", clk_freq);
	clk_put(clk);
	tick_div = clk_freq / TIMER_FREQ;

	rtk_timer_base_freq = clk_freq;

	tick_div = clk_freq/TIMER_FREQ;

	addr = of_get_address(node, hw_timer_used_idx, &size, &flags);
	if (addr) {
		mem = ioremap(be32_to_cpu(*addr), size);
		pr_info(NAME": %xh map to %p\n", be32_to_cpu(*addr), mem);
		if (mem) {

			rtk_clocksource_setup(mem, RTK_CLOCK_DEFAULT_FACTOR);
		} else {
			pr_err(NAME ": fail to map %x\n", be32_to_cpu(*addr));
			return;
		}
	} else {
		pr_err(NAME ": fail to get clksrc addr\n");
		return;
	}
	hw_timer_used_idx++;

	for_each_possible_cpu(cpu) {
		int irq;
		struct rtk_cevt_device *d;

		if (cpu >= CONFIG_NR_CPUS)
			break;

		addr = of_get_address(node, hw_timer_used_idx, &size, &flags);
		if (!addr) {
			pr_err(NAME ": fail to get clkevt addr\n");
			return;
		}

		irq = irq_of_parse_and_map(node, hw_timer_used_idx);

		d = per_cpu_ptr(&rtk_cdev, cpu);

		d->base = ioremap(be32_to_cpu(*addr), size);
		pr_info(NAME": %xh map to %p\n", be32_to_cpu(*addr), d->base);
		if (!d->base) {
			pr_err(NAME ": fail to map %x\n", be32_to_cpu(*addr));
			return;
		}

		d->dev.irq = irq;
		hw_timer_used_idx++;
	}

	pr_info(NAME ": %d hw timer used\n", hw_timer_used_idx);

	ret = cpuhp_setup_state(CPUHP_AP_ONLINE_DYN, "rtktimer:online",
				rtk_timer_online_cpu, NULL);
	if (ret < 0)
		pr_warn(NAME ": failed to register cpuhp callback\n");
}

/* unfortunately irq-mips-gic does not provide means to setup IRQ to VPE on initilaization, add the follwowing to irq-mips-gic */
/*

void mips_gic_init_one(unsigned int intr, int cpu,
				    struct irqaction *action)
{
	int virq, i;

	intr -= GIC_NUM_LOCAL_INTRS;

	virq = irq_create_mapping(gic_irq_domain,
				      GIC_SHARED_TO_HWIRQ(intr));

	//printk("%s(%d): cpu%d %d=>%d\n",__func__,__LINE__,cpu,intr,virq);
	gic_map_to_vpe(intr, mips_cm_vp_id(cpu));
	for (i = 0; i < NR_CPUS; i++)
		clear_bit(intr, pcpu_masks[i].pcpu_mask);
	set_bit(intr, pcpu_masks[cpu].pcpu_mask);

	irq_set_irq_type(virq, IRQ_TYPE_LEVEL_HIGH);

	irq_set_handler(virq, handle_percpu_irq);
	i = setup_irq(virq, action);
}

*/

TIMER_OF_DECLARE(rtk_timer, "rtk,soc-timer",
		       rtk_clocksource_of_init);

/* sample dt-binding

	timer1 {
			compatible = "rtk,soc-timer";
			clocks = <&apro_soc_clk 1>;
			reg = <
				0x18003200 0x10
				0x18003210 0x10
				0x18003220 0x10
				0x18003230 0x10
				0x18003240 0x10
				0x18003250 0x10
				0x18003270 0x10
				0x18003280 0x10
				0x18003290 0x10
				>;

			interrupt-parent = <&gic>;
			interrupts = <
				GIC_SHARED GIC_EXT_TC0 IRQ_TYPE_LEVEL_HIGH
				GIC_SHARED GIC_EXT_TC1 IRQ_TYPE_LEVEL_HIGH
				GIC_SHARED GIC_EXT_TC2 IRQ_TYPE_LEVEL_HIGH
				GIC_SHARED GIC_EXT_TC3 IRQ_TYPE_LEVEL_HIGH
				GIC_SHARED GIC_EXT_TC4 IRQ_TYPE_LEVEL_HIGH
				>;
		};

*/
