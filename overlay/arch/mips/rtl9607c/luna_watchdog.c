// SPDX-License-Identifier: GPL-2.0+
/*
 * Copyright (C) 2011 Realtek Semiconductor Corp.
 *
 * Purpose : watchdog timeout kernel thread
 *
 * Feature : Use kernel thread to perodically kick the watchdog
 *
 */

/*
 * Include Files
 */
#include <linux/module.h>
#include <linux/kernel.h>
#include <linux/kthread.h>
#include <linux/sched.h>
#include <linux/init.h>
#include <linux/fs.h>
#include <linux/proc_fs.h>
#include <linux/seq_file.h>
#include <linux/time.h>
#include <linux/spinlock.h>
#include <bspchip.h>

/*
 * Data Declaration
 */
unsigned int watchdog_flag = 0;
unsigned int kick_wdg_time = 5;

unsigned int wdg_timeout = CONFIG_WDT_PH1_TO;
unsigned int wdg_timeout_ph2 = CONFIG_WDT_PH2_TO;
unsigned int wdg_clk_sc = CONFIG_WDT_CLK_SC;
unsigned int wdg_rest_mode = CONFIG_WDT_RESET_MODE;
struct proc_dir_entry *watchdog_proc_dir = NULL;

#define DEBUG_LOCK_TEST
#ifdef DEBUG_LOCK_TEST
DEFINE_SPINLOCK(wdt_lock);
#endif

struct task_struct *pWatchdogTask[NR_CPUS];

#define _RTK_WDG_EN()   (REG32(BSP_WDTCTRLR) |= WDT_E)

static inline void RTK_WDG_EN(void) {
	extern void rtk_soc_reboot_reason_set(int reason);
	_RTK_WDG_EN();
	rtk_soc_reboot_reason_set(3);
}

#define RTK_WDG_DIS()  (REG32(BSP_WDTCTRLR) &= (~WDT_E))
#define RTK_WDG_KICK() (REG32(BSP_WDTCNTRR) = REG32(BSP_WDTCNTRR)| WDT_KICK )

#define _DEBUG_RTK_WDT_ALWAYS_   0xffffffff

#define DEBUG_RTK_WDT
#ifdef DEBUG_RTK_WDT
static unsigned int _debug_rtk_wdt_ = (_DEBUG_RTK_WDT_ALWAYS_);//|DEBUG_READ|DEBUG_WRITE|DEBUG_ERASE);
static int debug_mask = 0;	/* 6.18: silence per-kick console spam */
#define DEBUG_RTK_WDT_PRINT(mask, string) \
            if ((_debug_rtk_wdt_ & mask) || (mask == _DEBUG_RTK_WDT_ALWAYS_)) \
            printk string
#else
#define DEBUG_RTK_WDT_PRINT(mask, string)
#endif

#if defined(CONFIG_SMP)
#include <linux/mutex.h>
static DEFINE_MUTEX(luna_wtd_mutex);
static DECLARE_BITMAP(wtd_count, NR_CPUS);

static void inline touch_wtd_count(void)
{
	int cpu = get_cpu();

	set_bit(cpu, wtd_count);
	put_cpu();
}

static int luna_watchdog_thread (void *data)
{
	while(!kthread_should_stop()) {

		//gettimeofday(&start,NULL);
		/* No need to wake up earlier */
		set_current_state(TASK_UNINTERRUPTIBLE);
		schedule_timeout(kick_wdg_time * HZ);

		if(watchdog_flag)
			touch_wtd_count();
	}

	return 0;
}

static void luan_watchdog_thread_init(void)
{
	int cpu;

	for_each_online_cpu(cpu) {
		if (cpu == 0)
			continue;

		if (cpu >= NR_CPUS)
			break;

		pWatchdogTask[cpu] = kthread_create(luna_watchdog_thread, NULL, "luna_watchdog/%d", cpu);
		if (WARN_ON(!pWatchdogTask[cpu])) {
			printk("Create luan_watchdog/%d failed!", cpu);
			goto out_free;
		}

		kthread_bind(pWatchdogTask[cpu], cpu);
		wake_up_process(pWatchdogTask[cpu]);
	}

	return;

out_free:
	for_each_online_cpu(cpu) {
		if (cpu >= NR_CPUS)
			break;

		kthread_stop(pWatchdogTask[cpu]);
		pWatchdogTask[cpu] = NULL;
	}

	return;
}

static void CHECK_CPUS_AND_KICK(void)
{
	int cpu;
	int debug = 0;

	if (_debug_rtk_wdt_ & debug_mask)	debug = 1;

	for_each_online_cpu(cpu) {
		if ( cpu == 0 )
			continue;

		if(!test_bit(cpu, wtd_count)) {
			if(debug)	printk("luna_watchdog: luna_watchdog/%d not respond!\n", cpu);
			return;
		}
	}

	for_each_online_cpu(cpu)
		clear_bit(cpu, wtd_count);

	RTK_WDG_KICK();

	if(debug)	printk("CPU%d kick watchdog!\n", smp_processor_id());
}

#endif

void luna_watchdog_kick(void) {
	RTK_WDG_KICK();
}

time64_t  start;
time64_t  end;

static void show_usage(void)
{
	unsigned int reg_val = REG32(BSP_WDTCTRLR);

	printk("************ Watchdog Setting ****************\n");
	printk("WDT_E=%d, (1-enable, 0-disable)\n", ( (reg_val >> 31) & 0x1));
	//printk("LX(MHz)=%u\n", BSP_MHZ);
	printk("WDT_CLK_SC=%d\n", ( (reg_val >> 29) & 0x3));
	printk("PH1_TO=%d\n", ( (reg_val >> 22) & 0x1F));
	printk("PH2_TO=%d\n", ( (reg_val >> 15) & 0x1F));
	printk("WDT_RESET_MODE=%d\n", ( (reg_val ) & 0x3));
	printk("**********************************************\n");
}

/* for LX 200M Hz */
void set_rtk_wdt_ph1_threshold(int sec)
{
	unsigned int reg = 0;

	reg = REG32(BSP_WDTCTRLR);
	reg &= ~(WDT_PH12_TO_MSK << WDT_PH1_TO_SHIFT);

	reg &= ~(WDT_PH12_TO_MSK << WDT_PH2_TO_SHIFT);
	reg |= (wdg_timeout_ph2 << WDT_PH2_TO_SHIFT);

	reg |= (wdg_clk_sc << WDT_CLK_SC_SHIFT);
	reg |= wdg_rest_mode;

	if(sec ==0)
	        REG32(BSP_WDTCTRLR) = reg | (31 << WDT_PH1_TO_SHIFT);
	else
	        REG32(BSP_WDTCTRLR) = reg | (sec << WDT_PH1_TO_SHIFT);

	show_usage();
}

void kick_rtk_watchdog(void)
{
	if (_debug_rtk_wdt_ & debug_mask){
		end = ktime_get_seconds();

		printk("tv_sec:%lld\t",end);
	}

#ifdef CONFIG_SMP
	CHECK_CPUS_AND_KICK();
#else
	RTK_WDG_KICK();
#endif
}

/*
 *   CPU0 : Kick watchdog  register
 */
int watchdog_timeout_thread(void *data)
{
	while(!kthread_should_stop()) {
		//gettimeofday(&start,NULL);
		/* No need to wake up earlier */
		set_current_state(TASK_UNINTERRUPTIBLE);
		schedule_timeout(kick_wdg_time * HZ);

		if(watchdog_flag)	kick_rtk_watchdog();
	}

	return 0;
}

static void wdt_thread_maintain(int flag){
	int cpu = 0;
	if(flag) {
		if( pWatchdogTask[cpu] == NULL){
			pWatchdogTask[cpu] = kthread_create(watchdog_timeout_thread, NULL, "luna_watchdog/%d", cpu);

			if(IS_ERR(pWatchdogTask[cpu])) {
				printk("[Kthread : luna_watchdog] init failed %ld!\n", PTR_ERR(pWatchdogTask[cpu]));
			}
			else
			{
#ifdef CONFIG_SMP
				luan_watchdog_thread_init();			/* CPU1~CPUxx */
				kthread_bind( pWatchdogTask[cpu], cpu);		/* CPU0 */
#endif
				wake_up_process(pWatchdogTask[cpu]);
				printk("[Kthread : luan_watchdog ] init complete!\n");
			}
		}
	}else {
		for_each_online_cpu(cpu) {
	                if (cpu >= NR_CPUS)
        	                break;
			if( pWatchdogTask[cpu] != NULL) {
				kthread_stop(pWatchdogTask[cpu]);
				pWatchdogTask[cpu] = NULL;
			}
		}
	}
}

/* Kthread watchdog */
static int kick_wdg_time_show(struct seq_file *seq, void *v)
{
	seq_printf(seq, "[kick_wdg_time = %d sec]\n", kick_wdg_time);
	seq_printf(seq, "set watchdog kick time to 1~31 sec\n");

	return 0;
}


static ssize_t  kick_wdg_time_write(struct file *file,  const char __user *buf, size_t size, loff_t *_pos)
{
	unsigned char tmpBuf[16] = {0};
	int len = (size > 15) ? 15 : size;

	if (buf && !copy_from_user(tmpBuf, buf, len)) {
		kick_wdg_time = simple_strtoul(tmpBuf, NULL, 10);
		printk("write kick_wdg_time to %d\n", kick_wdg_time);
		//set_wdt_ph1_threshold(wdg_timeout);
		return size;
	}

	return -EFAULT;
}

/* Watch Dog Timer Control Register: PH1 */
static int wdg_timeout_show(struct seq_file *seq, void *v)
{
	unsigned int reg_val;
	reg_val = REG32(BSP_WDTCTRLR);

	seq_printf(seq, "WDT_E=%d, (1-enable, 0-disable)\n", ( (reg_val >> 31) & 0x1));
	//seq_printf(seq, "LX(MHz)=%u\n", BSP_MHZ);
	seq_printf(seq, "WDT_CLK_SC=%d\n", ( (reg_val >> 29) & 0x3));
	seq_printf(seq, "PH1_TO=%d\n", ( (reg_val >> 22) & 0x1F));
	seq_printf(seq, "PH2_TO=%d\n", ( (reg_val >> 15) & 0x1F));
	seq_printf(seq, "WDT_RESET_MODE=%d\n", ( (reg_val ) & 0x3));

	return 0;
}

static ssize_t wdg_timeout_write(struct file *file,  const char __user *buf, size_t size, loff_t *_pos)
{
	unsigned char tmpBuf[16] = {0};
	int len = (size > 15) ? 15 : size;

	if (buf && !copy_from_user(tmpBuf, buf, len)) {
		wdg_timeout = simple_strtoul(tmpBuf, NULL, 10);
		printk("write wdg_timeout to value(%d)\n", wdg_timeout);
		set_rtk_wdt_ph1_threshold(wdg_timeout);

		return size;
	}

	return -EFAULT;
}

static int watchdog_debug_show(struct seq_file *seq, void *v)
{
	seq_printf(seq, "[watchdog_flag = 0x%08x]\n", watchdog_flag);
	seq_printf(seq, "0x1: enable hw watchdog timeout\n");
	seq_printf(seq,"0x3: enable hw watchdog timeout debug message\n");

	return 0;
}

static ssize_t watchdog_write(struct file *file,  const char __user *buf, size_t size, loff_t *_pos)
{
	unsigned char tmpBuf[16] = {0};
	int len = (size > 15) ? 15 : size;

	if (buf && !copy_from_user(tmpBuf, buf, len)) {
		watchdog_flag = simple_strtoul(tmpBuf, NULL, 16);
		printk("write watchdog_flag to 0x%08x\n", watchdog_flag);

		if(watchdog_flag){
			wdt_thread_maintain(1);
			RTK_WDG_EN();
			printk("REG32(BSP_WDTCTRLR) = 0x%08x\n", REG32(BSP_WDTCTRLR) );
			RTK_WDG_KICK();
		}else{
			RTK_WDG_DIS();
			wdt_thread_maintain(0);		/* stop thread */
		}

		if(watchdog_flag & 0x02)	debug_mask = 1;
		else				debug_mask = 0;

		return size;
	}

	return -EFAULT;
}


static ssize_t proc_wdt_registers_r(struct seq_file *seq, void *v)
{
	int i;

	for(i = 0 ; i < 3 ; i++)
		seq_printf(seq, "REG32(0x%08x)=0x%08x\n", (0xb8003260+(i*4)), REG32((0xb8003260+(i*4))));

	return 0;
}

#ifdef DEBUG_LOCK_TEST

static ssize_t wdt_lock_read(struct seq_file *seq, void *v)
{
	seq_printf(seq, "[watchdog_flag = 0x%08x]\n", watchdog_flag);

	return 0;
}

#ifdef CONFIG_SMP
static void wdt_deadlock_test(void *info)
{
	printk("[Watchdog Timeout Testing]Dead lock forever on CPU%d.\n", smp_processor_id());
	spin_lock(&wdt_lock);
	spin_lock(&wdt_lock);
}
#endif

static void wdt_lock_func(void)
{
	printk("[Watchdog Timeout Testing]Dead lock forever.");
#ifdef CONFIG_SMP
	smp_call_function_single( num_online_cpus() -1, wdt_deadlock_test, NULL, 0);
#else
	spin_lock(&wdt_lock);
	spin_lock(&wdt_lock);
#endif
}

static ssize_t wdt_lock_write(struct file *file,  const char __user *buf, size_t size, loff_t *_pos)
{
	unsigned char tmpBuf[16] = {0};
	int len = (size > 15) ? 15 : size;
	int lock_flag = 0;

	if (buf && !copy_from_user(tmpBuf, buf, len)) {
		lock_flag = simple_strtoul(tmpBuf, NULL, 10);
		printk("write lock_flag to 0x%08x\n", lock_flag);

		if(lock_flag)	wdt_lock_func();

		return size;
	}

	return -EFAULT;
}

#endif	/* DEBUG_LOCK_TEST */

static int watchdog_open(struct inode *inode, struct file *file)
{
	return single_open(file, watchdog_debug_show, inode->i_private);
}

static const struct proc_ops bsp_luna_wdt_flag_fops = {
    .proc_open       = watchdog_open,
    .proc_read       = seq_read,
    .proc_lseek     = seq_lseek,
    .proc_release    = single_release,
    .proc_write      = watchdog_write,
};


static int timeout_open(struct inode *inode, struct file *file)
{
	return single_open(file, wdg_timeout_show, inode->i_private);
}

static const struct proc_ops bsp_luna_wdt_timeout_fops = {
    .proc_open       = timeout_open,
    .proc_read       = seq_read,
    .proc_lseek     = seq_lseek,
    .proc_release    = single_release,
    .proc_write      = wdg_timeout_write,
};


static int kick_open(struct inode *inode, struct file *file)
{
	return single_open(file, kick_wdg_time_show, inode->i_private);
}

static const struct proc_ops bsp_luna_wdt_kick_fops = {
    .proc_open       = kick_open,
    .proc_read       = seq_read,
    .proc_lseek     = seq_lseek,
    .proc_release    = single_release,
    .proc_write      = kick_wdg_time_write,
};

static int regs_open(struct inode *inode, struct file *file)
{
	return single_open(file, proc_wdt_registers_r, inode->i_private);
}

static const struct proc_ops bsp_luna_wdt_regs_fops = {
    .proc_open       = regs_open,
    .proc_read       = seq_read,
    .proc_lseek     = seq_lseek,
    .proc_release    = single_release,
};


#ifdef DEBUG_LOCK_TEST
static int locktest_open(struct inode *inode, struct file *file)
{
	return single_open(file, wdt_lock_read, inode->i_private);
}
static const struct proc_ops bsp_luna_wdt_locktest_fops = {
    .proc_open       = locktest_open,
    .proc_read       = seq_read,
    .proc_lseek     = seq_lseek,
    .proc_release    = single_release,
    .proc_write      = wdt_lock_write,
};

#endif

static void watchdog_dbg_init(void)
{
	/* Create proc debug commands */
	watchdog_proc_dir = proc_mkdir("luna_watchdog", NULL);
	if (watchdog_proc_dir == NULL) {
		printk("create /proc/luna_watchdog failed!\n");
		return;
	}

	if(! proc_create("watchdog_flag", 0444, watchdog_proc_dir, &bsp_luna_wdt_flag_fops)) {
		printk("create proc wdt/watchdog_flag failed!\n");
		return;
	}

	if(! proc_create("wdg_timeout", 0444, watchdog_proc_dir, &bsp_luna_wdt_timeout_fops)) {
		printk("create proc wdt/wdg_timeout failed!\n");
		return;
	}

	if(! proc_create("kick_wdg_time", 0444, watchdog_proc_dir, &bsp_luna_wdt_kick_fops)) {
		printk("create proc wdt/kick_wdg_time failed!\n");
		return;
	}

	if(! proc_create("registers", 0444, watchdog_proc_dir, &bsp_luna_wdt_regs_fops)) {
		printk("create proc wdt/registers failed!\n");
		return;
	}

#ifdef DEBUG_LOCK_TEST
	if(! proc_create("wdt_lock_test", 0444, watchdog_proc_dir, &bsp_luna_wdt_locktest_fops)) {
		printk("create proc wdt/wdt_lock_test failed!\n");
		return;
	}
#endif
}

static void watchdog_dbg_exit(void)
{
	/* Remove proc debug commands */
}

int __init watchdog_timeout_init(void)
{
	if(CONFIG_WDT_ENABLE) {
		wdt_thread_maintain(1);
		set_rtk_wdt_ph1_threshold(wdg_timeout);
		RTK_WDG_EN();
		watchdog_flag = 1;
	}

	watchdog_dbg_init();

	printk("====   Luna Watchdog Regs   =====\n");
	printk("BSP_WDTCNTRR(0x%08x): 0x%08x\n",(BSP_WDTCNTRR), REG32(BSP_WDTCNTRR));
	printk("BSP_WDTINTRR(0x%08x): 0x%08x\n",(BSP_WDTINTRR), REG32(BSP_WDTINTRR));
	printk("BSP_WDTCTRLR(0x%08x): 0x%08x\n",(BSP_WDTCTRLR), REG32(BSP_WDTCTRLR));
	printk("=================================\n");

	return 0;
}

void __exit watchdog_timeout_exit(void)
{
	printk("[%s-%d] exit!\n", __FILE__, __LINE__);

	RTK_WDG_DIS();
	wdt_thread_maintain(0);
	watchdog_dbg_exit();
}

MODULE_LICENSE("GPL");
MODULE_DESCRIPTION("RealTek watchdog timeout module");
MODULE_AUTHOR("David Chen <david_cw@realtek.com>");
module_init(watchdog_timeout_init);
module_exit(watchdog_timeout_exit);

