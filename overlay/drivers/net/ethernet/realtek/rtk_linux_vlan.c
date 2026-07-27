#include <linux/module.h>
#include <linux/init.h>
#include <linux/skbuff.h>
#include <linux/delay.h>
#include <linux/timer.h>
#include <linux/netdevice.h>
#include <linux/if_ether.h>
#include <linux/if_vlan.h>
#include <linux/proc_fs.h>
#include <linux/seq_file.h>
#include <linux/inetdevice.h>
#include <soc/cortina/rtk_vlan_common.h>
#include <soc/cortina/rtk_common_utility.h>
#include <soc/cortina/rtk_multi_wan_vlan.h>
#include <soc/cortina/rtk_lan_vlan.h>

#include "rtk_linux_vlan.h"

#if defined(CONFIG_RTK_LINUX_VLAN_SUPPORT)
rtk_linux_vlan_ctl_t rtk_vlan_ctl;
rtk_linux_vlan_ctl_t *rtk_vlan_ctl_p=&rtk_vlan_ctl;
static unsigned short rtk_vlan_group_index = 0;
int rtk_linux_vlan_enable = 0;
int rtk_linux_vlan_pvid_untag_enable;
EXPORT_SYMBOL(rtk_vlan_ctl_p);

static int rtk_vlan_state_read(struct seq_file *s, void *v);
static int rtk_vlan_state_write(struct file *filp, const char *buff,unsigned long len,void *data);
static int rtk_vlan_pvid_tagstate_read(struct seq_file *s, void *v);
static int rtk_vlan_pvid_tagstate_write(struct file *filp, const char *buff, unsigned long len, void *data);
static int rtk_vlan_pvid_read(struct seq_file *s, void *v);
static int rtk_vlan_pvid_write(struct file *filp, const char *buff,unsigned long len,void *data);
static int rtk_vlan_index_read(struct seq_file *s, void *v);
static int rtk_vlan_index_write(struct file *filp, const char *buff,unsigned long len,void *data);
static int rtk_vlan_group_read(struct seq_file *s, void *v);
static int rtk_vlan_group_write(struct file *filp, const char *buff,unsigned long len,void *data);

int vlan_pvid_single_open(struct inode *inode, struct file *file)
{
        return(single_open(file, rtk_vlan_pvid_read, pde_data(inode)));
}

static ssize_t vlan_pvid_single_write(struct file * file, const char __user * userbuf,
		     size_t count, loff_t * off)
{
	    return rtk_vlan_pvid_write(file, userbuf,count, off);
}

#if LINUX_VERSION_CODE >= KERNEL_VERSION(5,10,0)
struct proc_ops vlan_pvid_proc_fops= {
        .proc_open           = vlan_pvid_single_open,
        .proc_write		    = vlan_pvid_single_write,
        .proc_read           = seq_read,
        .proc_lseek         = seq_lseek,
        .proc_release        = single_release,
};
#else
struct file_operations vlan_pvid_proc_fops= {
		.owner	 = THIS_MODULE,
        .open           = vlan_pvid_single_open,
        .write		    = vlan_pvid_single_write,
        .read           = seq_read,
        .llseek         = seq_lseek,
        .release        = single_release,
};
#endif

int vlan_state_single_open(struct inode *inode, struct file *file)
{
        return(single_open(file, rtk_vlan_state_read, pde_data(inode)));
}

static ssize_t vlan_state_single_write(struct file * file, const char __user * userbuf,
		     size_t count, loff_t * off)
{
	    return  rtk_vlan_state_write(file, userbuf,count, off);
}

#if LINUX_VERSION_CODE >= KERNEL_VERSION(5,10,0)
struct proc_ops vlan_state_proc_fops= {
        .proc_open           = vlan_state_single_open,
        .proc_write		    = vlan_state_single_write,
        .proc_read           = seq_read,
        .proc_lseek         = seq_lseek,
        .proc_release        = single_release,
};
#else
struct file_operations vlan_state_proc_fops= {
		.owner	 = THIS_MODULE,
        .open           = vlan_state_single_open,
        .write		    = vlan_state_single_write,
        .read           = seq_read,
        .llseek         = seq_lseek,
        .release        = single_release,
};
#endif
int vlan_pvid_tagstate_single_open(struct inode *inode, struct file *file)
{
	return single_open(file, rtk_vlan_pvid_tagstate_read, pde_data(inode));
}

static ssize_t vlan_pvid_tagstate_single_write(struct file *file, const char __user *userbuf,
		     size_t count, loff_t *off)
{
	return rtk_vlan_pvid_tagstate_write(file, userbuf, count, off);
}

#if LINUX_VERSION_CODE >= KERNEL_VERSION(5, 10, 0)
struct proc_ops vlan_pvid_tagstate_proc_fops = {
		.proc_open			= vlan_pvid_tagstate_single_open,
		.proc_write			= vlan_pvid_tagstate_single_write,
		.proc_read			= seq_read,
		.proc_lseek			= seq_lseek,
		.proc_release		= single_release,
};
#else
struct file_operations vlan_pvid_tagstate_proc_fops = {
		.owner			= THIS_MODULE,
		.open			= vlan_pvid_tagstate_single_open,
		.write			= vlan_pvid_tagstate_single_write,
		.read			= seq_read,
		.llseek			= seq_lseek,
		.release		= single_release,
};
#endif

int vlan_index_single_open(struct inode *inode, struct file *file)
{
        return(single_open(file, rtk_vlan_index_read, pde_data(inode)));
}
static ssize_t vlan_index_single_write(struct file *file, const char __user *userbuf,
		     size_t count, loff_t *off)
{
	return rtk_vlan_index_write(file, userbuf, count, off);
}

#if LINUX_VERSION_CODE >= KERNEL_VERSION(5, 10, 0)
struct proc_ops vlan_index_proc_fops = {
		.proc_open			= vlan_index_single_open,
		.proc_write			= vlan_index_single_write,
		.proc_read			= seq_read,
		.proc_lseek			= seq_lseek,
		.proc_release		= single_release,
};
#else
struct file_operations vlan_index_proc_fops = {
		.owner			= THIS_MODULE,
		.open			= vlan_index_single_open,
		.write			= vlan_index_single_write,
		.read			= seq_read,
		.llseek			= seq_lseek,
		.release		= single_release,
};
#endif

int vlan_gorup_single_open(struct inode *inode, struct file *file)
{
        return(single_open(file, rtk_vlan_group_read, pde_data(inode)));
}

static ssize_t vlan_group_single_write(struct file * file, const char __user * userbuf,
		     size_t count, loff_t * off)
{
	    return  rtk_vlan_group_write(file, userbuf, count, off);
}

#if LINUX_VERSION_CODE >= KERNEL_VERSION(5, 10, 0)
struct proc_ops vlan_group_proc_fops = {
		.proc_open			= vlan_gorup_single_open,
		.proc_write			= vlan_group_single_write,
		.proc_read			= seq_read,
		.proc_lseek			= seq_lseek,
		.proc_release		= single_release,
};
#else
struct file_operations vlan_group_proc_fops= {
		.owner			= THIS_MODULE,
		.open			= vlan_gorup_single_open,
		.write			= vlan_group_single_write,
		.read			= seq_read,
		.llseek			= seq_lseek,
		.release		= single_release,
};
#endif

static unsigned int rtk_get_all_mac_portmask_without_cpu(void)
{
	unsigned int portmask = 0;
		
#if defined(CONFIG_FC_RTL9607C_SERIES)
			portmask = ((1<< RTK_MAC_PORT0)|(1<< RTK_MAC_PORT1)|(1<< RTK_MAC_PORT2)|(1<< RTK_MAC_PORT3)|(1<< RTK_MAC_PORT4)|(1<< RTK_MAC_PORT_PON));
#elif defined(CONFIG_FC_RTL8198F_SERIES)
#if defined(CONFIG_RTL_8367S_SUPPORT) && defined(CONFIG_RTL_8226_SUPPORT)
			portmask = ((1<< RTK_MAC_PORT0)|(1<< RTK_MAC_PORT1)|(1<< RTK_MAC_PORT2)|(1<< RTK_MAC_PORT3)|(1<< RTK_MAC_PORT4)|(1<< RTK_MAC_PORT_PON));
#else
			portmask = ((1<< RTK_MAC_PORT0)|(1<< RTK_MAC_PORT1)|(1<< RTK_MAC_PORT2)|(1<< RTK_MAC_PORT3)|(1<< RTK_MAC_PORT_PON));
#endif
#elif defined(CONFIG_FC_CAG3_SERIES) || defined(CONFIG_FC_RTL9607F_SERIES)
			portmask = ((1<< RTK_MAC_PORT0)|(1<< RTK_MAC_PORT1)|(1<< RTK_MAC_PORT2)|(1<< RTK_MAC_PORT3)|(1<< RTK_MAC_PORT6)|(1<< RTK_MAC_PORT_PON));
#endif

	return portmask;
}


static unsigned int rtk_get_lan_dev_index(unsigned int  portmask)
{
	unsigned int lan_index = 0;

	lan_index = portmask;

	return lan_index;	
}

unsigned short rtk_get_vid_by_port(unsigned int  port_index)
{
    //get pvid
    if(port_index<=RTK_PVID_ARRAY_SIZE && port_index>=0)
    	return rtk_vlan_ctl_p->pvid[port_index];
	else
		return 0;
}

int rtk_linux_vlan_rx(struct sk_buff **pskb, unsigned int portmask)
{
	unsigned short vid = 0,skb_vid = 0;
	int ret =0;
	struct sk_buff *skb = *pskb;
	unsigned int all_lan_portmask = 0;
	unsigned int pvidIndex = 0;

	if(!skb){
		printk("parameter error! pskb or *psbk is NULL!");
		return FAIL;
	}

	skb_vid = rtk_get_skb_vid(skb);

	//untag pkts insert pvid
	if(!skb_vid && rtk_linux_vlan_enable)
	{		
		all_lan_portmask = rtk_get_all_mac_portmask_without_cpu();

		if(all_lan_portmask & (1<<portmask))
		{
			//lan port
			pvidIndex = rtk_get_lan_dev_index(portmask);
		}
		
		vid = rtk_get_vid_by_port(pvidIndex);

		if(vid)
			ret = rtk_insert_vlan_tag_and_priority(vid, 0, pskb);	
	}

	return ret;

}

int rtk_linux_vlan_tx(struct sk_buff **pskb, unsigned int portmask)
{
	struct sk_buff *skb = *pskb;
	unsigned short vid = 0,skb_vid = 0;
	int ret =0;
	unsigned int all_lan_portmask = 0;
	unsigned int pvidIndex = 0;

	if(!skb){
		printk("parameter error! pskb or *psbk is NULL!");
		return FAIL;
	}
				
	if(rtk_linux_vlan_enable)
	{
		//if packet with tag and vlanid==pvid, remove vlan tag.
		skb_vid =rtk_get_skb_vid(skb);
		all_lan_portmask = rtk_get_all_mac_portmask_without_cpu();
		
		if(all_lan_portmask & (portmask))
		{
			//lan port
			pvidIndex = rtk_get_lan_dev_index(portmask>>1);	
		}
		
		vid = rtk_get_vid_by_port(pvidIndex);

		//remove eth pkts pvid
		if (rtk_linux_vlan_pvid_untag_enable) {
			if (skb_vid != 0 && vid == skb_vid) {
				ret = remove_vlan_tag(pskb);
			}
		}
	}

	return ret;
}

#if defined(CONFIG_RTL_83XX_SUPPORT) && defined(CONFIG_RTL_MULTI_ETH_WAN)
int rtk_set_lan_port_pvid(void)
{
	int idx = 0, vid = 0,ret = 0;
	unsigned int pvid_tmp = 0, pri_tmp = 0;
	unsigned int portmask = 0, tagmask = 0;
	unsigned int pri = 0;
	
	for(idx = 0; idx < RTK_LAN_MAX_ETH_NUM; idx++)
	{	
		vid = rtk_vlan_ctl_p->pvid[idx];
		//dev mask
		portmask = tagmask = 1 << idx;
		rtl_change_dev_mask_to_port_mask(&portmask, &tagmask);

		if (rtl83xx_pvid_get_hook){
			ret = (*rtl83xx_pvid_get_hook)(portmask, &pvid_tmp, &pri_tmp);
			VLAN_TRACE("i=%d, pvid=%u, port_pri=%u, ret=%d\n",idx, pvid_tmp, pri_tmp, ret);
			if (ret != SUCCESS)
				return ret;
		}
		else{
			VLAN_ERROR("rtl83xx_pvid_get_hook is NULL!\n");
			return FAIL;
		}
	
		if(pri_tmp!= 0){
			pri = pri_tmp;
		}

		if(pvid_tmp == vid)
			continue;
		
		if (rtl83xx_pvid_set_hook){
			ret = (*rtl83xx_pvid_set_hook)(portmask, vid, pri);
			if (ret != SUCCESS)
				return ret;
		}
		else{
			return FAIL;
		}
	}

	return 0;
}

int rtk_set_cpu_port_pvid(void)
{
	int ret = 0,i =0, vid = 0;
	unsigned int ext_cpu_mask = 0;
	unsigned int pvid_tmp = 0, pri_tmp = 0;

	if (rtl83xx_get_cpu_portmask_hook){
		ext_cpu_mask = (*rtl83xx_get_cpu_portmask_hook)();
		VLAN_TRACE("%s:%d ext_cpu_mask=%x \n", __FUNCTION__, __LINE__, ext_cpu_mask);
	}
	else{
		VLAN_ERROR("rtl83xx_get_cpu_portmask_hook is NULL!\n");
		return FAIL;
	}

	for(i=0;i<9;i++){
		if((1<<i) & ext_cpu_mask){
			if (rtl83xx_pvid_get_hook){
				ret = (*rtl83xx_pvid_get_hook)(i, &pvid_tmp, &pri_tmp);
				VLAN_TRACE("i=%d, pvid=%u, port_pri=%u, ret=%d\n",i, pvid_tmp, pri_tmp, ret);
				if (ret != SUCCESS)
					return ret;
			}
			else{
				VLAN_ERROR("rtl83xx_pvid_get_hook is NULL!\n");
				return FAIL;
			}

			vid = rtk_vlan_ctl_p->pvid[RTK_CPU_PORT_BIT];

			if(pvid_tmp == vid)
				continue;
			
			if (rtl83xx_pvid_set_hook){
				ret = (*rtl83xx_pvid_set_hook)(i, vid, pri_tmp);
				if (ret != SUCCESS)
					return ret;
			}
			else{
				return FAIL;
			}
		}
	}

	return SUCCESS;
}

int rtk_add_83xx_vlan(int vid, unsigned int port_mask, unsigned int tag_mask, int pri, int fid)
{
	unsigned int board_type = 0;
	unsigned int membermsk = 0, untagmsk = 0, tmp_pvid = 0, tmp_vid = 0;
	unsigned int ext_cpu_mask = 0;
	int ret = 0;

	VLAN_TRACE("vid:%d portmask:%x tagmask:%x\n",vid,port_mask,tag_mask);
	tmp_vid = vid;
	tmp_pvid = tmp_vid;
	port_mask &= RTL_ETH_LAN1_BIND_MASK | RTL_ETH_LAN2_BIND_MASK | RTL_ETH_LAN3_BIND_MASK | RTL_ETH_LAN4_BIND_MASK | RTL_ETH_LAN5_BIND_MASK;

	if (rtl_get_board_type_hook){
		board_type = (*rtl_get_board_type_hook)();
		VLAN_TRACE("board_type=0x%x \n", board_type);
	}
	else{
		VLAN_ERROR("rtl_get_board_type_hook is NULL!\n");
		return FAIL;
	}
	//v600, exclude lan1(bit0), because lan1 is not external switch port
	if ((board_type & 0xF00) == 0x600){
		port_mask &= ~RTL_ETH_LAN1_BIND_MASK;
	}

	VLAN_TRACE("%s:%d port_mask=0x%x, tag_mask=0x%x\n", __FUNCTION__, __LINE__,port_mask, tag_mask);
	//change for external switch
	rtl_change_dev_mask_to_port_mask(&port_mask, &tag_mask);
	VLAN_TRACE("%s:%d port_mask=0x%x, tag_mask=0x%x\n", __FUNCTION__, __LINE__,port_mask, tag_mask);
	
	if (rtl83xx_vlan_get_hook){
		ret = (*rtl83xx_vlan_get_hook)(tmp_vid, &membermsk, &untagmsk, &pri, &fid);
		if (ret != SUCCESS){
			VLAN_ERROR("get tmp_vid=%u\n", tmp_vid);
			return ret;
		}
	}
	else{
		VLAN_ERROR("rtl83xx_vlan_get_hook is NULL!");
		return FAIL;
	}

	untagmsk |= port_mask & (~tag_mask);
	membermsk |= port_mask;

	if (rtl83xx_get_cpu_portmask_hook){
		ext_cpu_mask = (*rtl83xx_get_cpu_portmask_hook)();
		VLAN_TRACE("%s:%d ext_cpu_mask=%x \n", __FUNCTION__, __LINE__, ext_cpu_mask);
	}
	else{
		VLAN_ERROR("rtl83xx_get_cpu_portmask_hook is NULL!\n");
		return FAIL;
	}

	//add cpu port
	membermsk |= ext_cpu_mask;
	
	if (rtl83xx_vlan_set_hook){
		ret = (*rtl83xx_vlan_set_hook)(tmp_vid, membermsk, untagmsk, pri, fid);
		VLAN_TRACE("%s:%d tmp_vid=%u, membermsk=0x%x, untagmsk=0x%x, pri=%u, fid=%u ret=%d\n",__FUNCTION__, __LINE__, tmp_vid, membermsk, untagmsk, pri, fid, ret);
		if (ret != SUCCESS)
			return ret;
	}
	else{
		VLAN_ERROR("rtl83xx_vlan_set_hook is NULL!\n");
		return FAIL;
	}

	return SUCCESS;

}

int rtk_remove_83xx_vlan(int vid)
{
	unsigned int board_type = 0;
	unsigned int membermsk = 0, untagmsk = 0, tmp_pvid = 0, tmp_vid = 0;
	unsigned short pri = 0, fid = 0;
	unsigned int port_mask = 0, tag_mask = 0,ext_cpu_mask = 0;
	int ret = 0;

	tmp_vid = vid;
	tmp_pvid = RTL_LANVLANID;

	if (rtl83xx_vlan_get_hook){
		ret = (*rtl83xx_vlan_get_hook)(tmp_vid, &membermsk, &untagmsk, &pri, &fid);
		if (ret != SUCCESS){
			VLAN_ERROR("get tmp_vid=%u failed! \n", tmp_vid);
			return ret;
		}
	}
	else{
		VLAN_ERROR("rtl83xx_vlan_get_hook is NULL!");
		return FAIL;
	}
		
	pri = membermsk = fid = untagmsk = 0;
	
	//remove selected vlan 
	if (rtl83xx_vlan_set_hook){
		ret = (*rtl83xx_vlan_set_hook)(tmp_vid, membermsk, untagmsk, pri, fid);
		VLAN_TRACE("tmp_vid=%u, membermsk=0x%x, untagmsk=0x%x, pri=%u, fid=%u ret=%d\n",tmp_vid, membermsk, untagmsk, pri, fid, ret);
		if (ret != SUCCESS)
			return ret;
	}
	else{
		VLAN_ERROR("rtl83xx_vlan_set_hook is NULL!\n");
		return FAIL;
	}

	return SUCCESS;
}
#endif


int rtk_init_vlan_tbl(void)
{
    int i = 0;
	rtk_linux_vlan_group_t *vlan_group_p=NULL;	

     memset(&rtk_vlan_ctl,0,sizeof(rtk_linux_vlan_ctl_t));

	//default lan vlan
	vlan_group_p=&rtk_vlan_ctl_p->group[RTK_LINUX_LAN_VLAN_ID];								

	vlan_group_p->vid=(unsigned short)RTK_LINUX_LAN_VLAN_ID;	
	vlan_group_p->memberMask= RTK_LANPORT_MASK;			
	vlan_group_p->tagMemberMask= 0 ;					
	vlan_group_p->vlanType = RTK_VLAN_TYPE_NAT;
	vlan_group_p->flag = 1;

     for( i=0; i< RTK_PHY_PORT_NUM ; i++)
    	rtk_vlan_ctl_p->pvid[i] = RTK_LINUX_LAN_VLAN_ID;


#if defined(CONFIG_RTL_83XX_SUPPORT) && defined(CONFIG_RTL_MULTI_ETH_WAN)
	rtk_add_83xx_vlan(vlan_group_p->vid, vlan_group_p->memberMask, vlan_group_p->tagMemberMask, 0, 0);
#endif

	return 0;

}

static int rtk_vlan_state_read(struct seq_file *s, void *v)
{
	seq_printf(s, "linux vlan enable:%d\n",rtk_linux_vlan_enable);
	return 0;
}

static int rtk_vlan_state_write(struct file *filp, const char *buff,unsigned long len,void *data)
{
	char tmpbuf[32];
	int flag = 0 , num = 0;

	if (buff && !copy_from_user(tmpbuf, buff, len))
	{
		num=sscanf(tmpbuf,"%d",&flag);
		
		if(num!=1){
			return len;
		}

		if(flag!= 0 && flag!= 1){
			printk("[Error]Invalid state, please check input state number:%d\n",flag);
			return len;
		}
		else{
			rtk_linux_vlan_enable = flag;
			
			if(flag)
				rtk_init_vlan_tbl();
				
			printk("set state successful! state:%hu\n",rtk_linux_vlan_enable);
			//return len;
		}
	}

	return len;

}

static int rtk_vlan_pvid_tagstate_read(struct seq_file *s, void *v)
{
	seq_printf(s, "linux vlan pvid untag enable:%d\n", rtk_linux_vlan_pvid_untag_enable);
	return 0;
}

static int rtk_vlan_pvid_tagstate_write(struct file *filp, const char *buff, unsigned long len, void *data)
{
	char tmpbuf[32];
	int flag = 0, num = 0;

	if (buff && !copy_from_user(tmpbuf, buff, len)) {
		num = sscanf(tmpbuf, "%d", &flag);

		if (num != 1) {
			return len;
		}

		if (flag != 0 && flag != 1) {
			printk("[Error]Invalid state, please check input state number:%d\n", flag);
			return len;
		} else {
			rtk_linux_vlan_pvid_untag_enable = flag;
			printk("set state successful! state:%hu\n", rtk_linux_vlan_pvid_untag_enable);
		}
	}

	return len;
}

static int rtk_vlan_pvid_read(struct seq_file *s, void *v)
{

	seq_printf(s, "pvid:%d,%d,%d,%d,%d,%d,%d,%d,%d,%d,%d,%d,%d,%d,%d,%d,%d,%d,%d,%d,%d,%d,%d,%d %d\n",
	rtk_vlan_ctl_p->pvid[0],rtk_vlan_ctl_p->pvid[1],rtk_vlan_ctl_p->pvid[2],rtk_vlan_ctl_p->pvid[3],
	rtk_vlan_ctl_p->pvid[4],rtk_vlan_ctl_p->pvid[5],rtk_vlan_ctl_p->pvid[6],rtk_vlan_ctl_p->pvid[7],
	rtk_vlan_ctl_p->pvid[8],rtk_vlan_ctl_p->pvid[9],rtk_vlan_ctl_p->pvid[10],rtk_vlan_ctl_p->pvid[11],
	rtk_vlan_ctl_p->pvid[12],rtk_vlan_ctl_p->pvid[13],rtk_vlan_ctl_p->pvid[14],rtk_vlan_ctl_p->pvid[15],
	rtk_vlan_ctl_p->pvid[16],rtk_vlan_ctl_p->pvid[17],rtk_vlan_ctl_p->pvid[18],rtk_vlan_ctl_p->pvid[19],
	rtk_vlan_ctl_p->pvid[20],rtk_vlan_ctl_p->pvid[21],rtk_vlan_ctl_p->pvid[22],rtk_vlan_ctl_p->pvid[23],rtk_vlan_ctl_p->pvid[24]);

	return 0;
}

static int rtk_vlan_pvid_write(struct file *filp, const char *buff,unsigned long len,void *data)
{
	char tmpbuf[100];
	unsigned short pvid[RTK_PVID_ARRAY_SIZE] = {0};
	char *strptr = NULL,*split_str = NULL;
	int i = 0;

	if (buff && !copy_from_user(tmpbuf, buff, len))
	{
		strptr = tmpbuf;

		while (1) {
			split_str = strsep(&strptr, " ");
			pvid[i] = kstrtol(split_str, NULL, 0);
			i++;

			if (strptr == NULL || i > RTK_PVID_ARRAY_SIZE)
				break;
			}
	}

	for (i = 0; i < RTK_PVID_ARRAY_SIZE; i++) {
		
		if (pvid[i] > 4096 || pvid[i] < 0) {
			printk("[Error]Invalid pvid, please check pvid! pvid[%d]:%d\n",i,pvid[i]);
			return len;
		}
			
		rtk_vlan_ctl_p->pvid[i] = pvid[i];
	}


#if defined(CONFIG_RTL_83XX_SUPPORT) && defined(CONFIG_RTL_MULTI_ETH_WAN)
	//update pvid
	rtk_set_lan_port_pvid();
	rtk_set_cpu_port_pvid();
#endif

	return len;
}

static int rtk_vlan_index_read(struct seq_file *s, void *v)
{
	seq_printf(s, "%d\n",rtk_vlan_group_index);
	return 0;
}

static int rtk_vlan_index_write( struct file *filp, const char *buff,unsigned long len,void *data)
{
	char tmpbuf[20];
	int num = 0;
	unsigned int index = 0;

	if (buff && !copy_from_user(tmpbuf, buff, len))
	{
		num = sscanf(tmpbuf, "%d", &index);
		
		if (num != 1) {
			return len;
		}
		
		rtk_vlan_group_index=index&0xfff;		
	}
	return len;
}

static int rtk_vlan_group_read(struct seq_file *s, void *v)
{
	int vlan_id = 0;
	
	for(vlan_id=0; vlan_id<RTK_VLAN_GROUP_NUM; vlan_id++)
	{
		if(rtk_vlan_ctl_p->group[vlan_id].flag)
			seq_printf(s, "%d, %x, %x, %s, %u\n", 
			rtk_vlan_ctl_p->group[vlan_id].vid,
			rtk_vlan_ctl_p->group[vlan_id].memberMask,
			rtk_vlan_ctl_p->group[vlan_id].tagMemberMask,
			rtk_vlan_ctl_p->group[vlan_id].vlanType==RTK_VLAN_TYPE_NAT?"NAT":"Bridge",
			rtk_vlan_ctl_p->group[vlan_id].priority
			);
	}
	
	return 0;
}

static int rtk_vlan_group_write( struct file *filp, const char *buff,unsigned long len,void *data)
{
	
	char tmpbuf[100];
	int num = 0;
	unsigned int flag = 0;
	unsigned int mask = 0;
	unsigned int tagMask = 0;
	unsigned int vid = 0;
	unsigned int vlanType = 0;
	rtk_linux_vlan_group_t *vlan_group_p=NULL;
	//int priority = 0, fid = 0;
	
	if(!rtk_linux_vlan_enable)
	{
		printk("rtk 11q vlan support not enabled!!!\n");
		return len;
	}
	
	if (buff && !copy_from_user(tmpbuf, buff, len))
	{
		num=sscanf(tmpbuf,"%d,%x,%x,%d,%d",&flag,&mask,&tagMask,&vid,&vlanType);
		if(num < 5){
			return len;
		}
		
		if(rtk_vlan_group_index==(vid&0xfff)){
			vlan_group_p=&rtk_vlan_ctl_p->group[rtk_vlan_group_index];
			
			/*clear the old one and delete old vlan group*/
			if(vlan_group_p->vid && vlan_group_p->flag)
			{
				#if defined(CONFIG_RTL_83XX_SUPPORT) && defined(CONFIG_RTL_MULTI_ETH_WAN)
					rtk_remove_83xx_vlan(vid);
				#endif
			}

			memset(vlan_group_p,0,sizeof(rtk_linux_vlan_group_t));
			
			vlan_group_p->flag = (unsigned short)flag;
			vlan_group_p->memberMask = mask;
			vlan_group_p->tagMemberMask = tagMask;
			vlan_group_p->vid = (unsigned short)vid;
			vlan_group_p->vlanType = (unsigned short)vlanType;

			if(vlan_group_p->flag){
#if defined(CONFIG_RTL_83XX_SUPPORT) && defined(CONFIG_RTL_MULTI_ETH_WAN)
				rtk_add_83xx_vlan(vlan_group_p->vid, vlan_group_p->memberMask = mask, vlan_group_p->tagMemberMask, 0,0);
#endif
			}
		}
	}
	
	return len;
}

EXPORT_SYMBOL(vlan_pvid_proc_fops);
EXPORT_SYMBOL(vlan_state_proc_fops);
EXPORT_SYMBOL(vlan_pvid_tagstate_proc_fops);
EXPORT_SYMBOL(vlan_index_proc_fops);
EXPORT_SYMBOL(vlan_group_proc_fops);
EXPORT_SYMBOL(rtk_linux_vlan_rx);
EXPORT_SYMBOL(rtk_linux_vlan_tx);
#endif
