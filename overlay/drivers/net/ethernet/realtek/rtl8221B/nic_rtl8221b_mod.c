#include <linux/version.h>
#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/init.h>
#include <linux/slab.h>
#include <linux/mutex.h>
#include <linux/proc_fs.h>
#include <linux/seq_file.h>
#include <linux/kthread.h>
#include <linux/uaccess.h>
#include <linux/gpio.h>
#include <linux/delay.h>	/* mdelay() */
#include <linux/platform_device.h>
#include <linux/leds.h>
#include <fs/proc/internal.h>

#include <common/rt_error.h>
#if IS_BUILTIN(CONFIG_RTK_EXT_GPHY) || IS_MODULE(CONFIG_RTK_EXT_GPHY)
#include <rtk_ext_gphy.h>
#endif
#include <nic_rtl8221.h>
#include <nic_rtl8221_init.h>

struct rtl8221b_led {
	struct	led_classdev cdev;
	uint8	mac;
	uint8	id;
	char	name[32];
};
#define RTK_RTL8221B_LED_NUM	3

typedef struct rtl8221b_s {
	HANDLE	hDevice;
	uint8	mac;
	struct rtl8221b_led leds[RTK_RTL8221B_LED_NUM];
} rtl8221b_t;

#define RTK_MAX_RTL8221B_NUM	4
static rtl8221b_t rtl8221b[RTK_MAX_RTL8221B_NUM];
static int rtl8221b_count	= 0;
static int rtl8221b_init	= 0;
#define INVALID_VALUE		99

/*
 * Default Serdes Mode (write bit [5:0] = 0/1/2/3 SDS mode)
 * 0: 2500Base-X/SGMII
 * 1: HiSGMII/SGMII
 * 2: 2500Base-X
 * 3: HiSGMII
 */
#define DEFAULT_SERDES_MODE	0x1

#define CONFIG_EXTGPHY_8221B_DEFAULT_MDIO_SET	(0)
#ifndef CONFIG_RTL_8221B_DEVICE_0_MDIO_SET
#define CONFIG_RTL_8221B_DEVICE_0_MDIO_SET	CONFIG_EXTGPHY_8221B_DEFAULT_MDIO_SET
#endif
#ifndef CONFIG_RTL_8221B_DEVICE_1_MDIO_SET
#define CONFIG_RTL_8221B_DEVICE_1_MDIO_SET	CONFIG_EXTGPHY_8221B_DEFAULT_MDIO_SET
#endif

#ifdef CONFIG_LUNA_G3_SERIES
#include <aal_phy.h>

#define CA_AAL_MDIO_ST_CODE_C22		1
#define CA_AAL_MDIO_ST_CODE_C45		0
#define RTL8221B_RESET_GPIO_NO		14			/* GPIO4_[1]: 4 * 32 + 1 */
#define RTL8221B_RESET_GPIO_LABEL	"rtl8221b_reset"

extern ca_status_t aal_mdio_write(/*CODEGEN_IGNORE_TAG*/
    CA_IN       ca_device_id_t             device_id,
    CA_IN      ca_uint8_t              st_code,
    CA_IN      ca_uint8_t              phy_port_addr,
    CA_IN      ca_uint8_t              reg_dev_addr,
    CA_IN      ca_uint16_t              addr,
    CA_IN      ca_uint16_t              data);

extern ca_status_t aal_mdio_read(/*CODEGEN_IGNORE_TAG*/
    CA_IN       ca_device_id_t             device_id,
    CA_IN      ca_uint8_t              st_code,
    CA_IN      ca_uint8_t              phy_port_addr,
    CA_IN      ca_uint8_t              reg_dev_addr,
    CA_IN      ca_uint16_t              addr,
    CA_OUT     ca_uint16_t             *data);

#else
#include <rtk/gpio.h>
//#include <rtk/port.h>			/* for rtk_port_serdesMode_get */
#include <rtk/mdio.h>

/* gpio */
//#define IO_GPIO_EN_REG		0xBB000038
#define RTL8221B_DEV0_RESET_PIN		60
#define RTL9607C_SET0_MDC_PIN		6
#define RTL9607C_SET0_MDIO_PIN		7
#define RTL9607C_SET1_MDC_PIN		12
#define RTL9607C_SET1_MDIO_PIN		10
extern int rtk_8221b_reset_gpio_get(unsigned int dev);

/* mdio */
#define IO_MODE_EN_REG			0xBB023014
#define MDIO_MASTER_EN			(1 << 10)
#define EXT_MDX_M_EN			(1 << 11)

#define DEFAULT_MDIO_PORT_NUM		0
#define RTK_MDIO_FMT_C22		0
#define RTK_MDIO_FMT_C45		1

/* Register access macro */
#ifndef REG32
#define REG32(reg)	(*((volatile unsigned int *)(reg)))
#endif
#ifndef REG16
#define REG16(reg)	(*((volatile unsigned short *)(reg)))
#endif
#ifndef REG8
#define REG8(reg)	(*((volatile unsigned char *)(reg)))
#endif
#endif

/* for proc */
struct proc_dir_entry *rtl8221b_proc_dir = NULL;
#define PROC_DIR_RTL8221B		"rtl8221b"
#define PROC_FILE_HELP			"help"
#define PROC_FILE_PHY			"phy"
#define PROC_FILE_LINK_STATUS		"link_status"

#if 0
//spinlock_t lock_mdio;
BOOLEAN
init_mdio_lock(void)
{
	printk("Init mdio lock\n");
	spin_lock_init(&lock_mdio);
	return SUCCESS;
}
#endif

BOOLEAN
MmdPhyRead(
	IN  HANDLE hDevice,
	IN  UINT16 dev,
	IN  UINT16 addr,
	OUT UINT16 *data)
{
#ifdef CONFIG_LUNA_G3_SERIES
	/**aal_mdio_read:
	*@breif: Read MDIO slaver device data with 16bits width
	*@param [in] st_code : Start of Frame (01 for Clause 22;00 for Clause 45)
	*@param [in] phy_port_addr : PHY Address, 0~31
	*@param [in] reg_dev_addr  : Register Address, 0~31
	*@param [in] addr  : extended address for Clause45, normally NOT used
	*@param [out] data : the Data returned from MDIO slaver
	*@return: CA_E_OK if successfully, otherwise return CA_E_PARAM
	*/
	ca_status_t ret = CA_E_OK;

	ret = aal_mdio_read(hDevice.unit, CA_AAL_MDIO_ST_CODE_C45, hDevice.port, dev, addr, data);
	if (ret != CA_E_OK) {
		RTL8221B_ERROR("%s (dev = %d, addr = 0x%04x): data = 0x%04x - ret = %d (FAILED)\n",
			__FUNCTION__, dev, addr, *data, ret);
		return FAILURE;
	}
#else
	int32 ret;

	rtk_gpio_state_set(hDevice.gpio_mdc, DISABLED);
	rtk_gpio_state_set(hDevice.gpio_mdio, DISABLED);
	//spin_lock_bh(&lock_mdio);
	rtk_mdio_cfg_set(hDevice.unit, DEFAULT_MDIO_PORT_NUM, hDevice.port, RTK_MDIO_FMT_C45);
	ret = rtk_mdio_c45_read((uint8)dev, addr, data);
	if (ret == RT_ERR_FAILED)
	{
		RTL8221B_ERROR("%s (dev = %d, addr = 0x%04x): data = 0x%04x - ret = %d (FAILED)\n",
			__FUNCTION__, dev, addr, *data, ret);
		return FAILURE;
	}
	//spin_unlock_bh(&lock_mdio);
	//RTL8221B_DEBUG("%s (dev = %d, addr = 0x%04x): data = 0x%04x - ret = %d (OK)\n",
	//	__FUNCTION__, dev, addr, *data, ret);
#endif
	return SUCCESS;
}

BOOLEAN
MmdPhyWrite(
	IN HANDLE hDevice,
	IN UINT16 dev,
	IN UINT16 addr,
	IN UINT16 data)
{
#ifdef CONFIG_LUNA_G3_SERIES
	/**aal_mdio_write:
	*@breif: Write data to MDIO slaver device with 16bits width
	*@param [in] st_code : Start of Frame (01 for Clause 22;00 for Clause 45)
	*@param [in] phy_port_addr : PHY Address, 0~31
	*@param [in] reg_dev_addr  : Register Address, 0~31
	*@param [in] addr  : extended address for Clause45, normally NOT used
	*@param [in] data  : the Data writen to MDIO slaver
	*@return: CA_E_OK if successfully, otherwise return CA_E_PARAM
	*/
	ca_status_t ret = CA_E_OK;

	ret = aal_mdio_write(hDevice.unit, CA_AAL_MDIO_ST_CODE_C45, hDevice.port, dev, addr, data);
	if (ret != CA_E_OK)
	{
		RTL8221B_ERROR("%s (dev = %d, addr = 0x%04x, data = 0x%04x): - ret = %d (FAILED)\n",
			__FUNCTION__, dev, addr, data, ret);
		return FAILURE;
	}
#else
	int32	ret;

	rtk_gpio_state_set(hDevice.gpio_mdc, DISABLED);
	rtk_gpio_state_set(hDevice.gpio_mdio, DISABLED);
	//spin_lock_bh(&lock_mdio);
	rtk_mdio_cfg_set(hDevice.unit, DEFAULT_MDIO_PORT_NUM, hDevice.port, RTK_MDIO_FMT_C45);
	ret = rtk_mdio_c45_write((uint8)dev, addr, data);
	if (ret == RT_ERR_FAILED)
	{
		RTL8221B_ERROR("%s (dev = %d, addr = 0x%04x, data = 0x%04x): - ret = %d (FAILED)\n",
			__FUNCTION__, dev, addr, data, ret);
		return FAILURE;
	}
	//spin_unlock_bh(&lock_mdio);
	//RTL8221B_DEBUG("%s (dev = %d, addr = 0x%04x, data = 0x%04x): - ret = %d (OK)\n",
	//		__FUNCTION__, dev, addr, data, ret);
#endif
	return SUCCESS;
}

u32 rtl8221b_index_by_mac(u8 mac) {
	int i;
	for (i = 0 ; i < rtl8221b_count ; i++)
		if (mac == rtl8221b[i].mac)	return i;

	RTL8221B_ERROR("mac %d is not found", mac);
	return INVALID_VALUE;	/* invalid value */
}
u32 rtl8221b_index_by_addr(u8 phy_addr) {
	int i;
	for (i = 0 ; i < rtl8221b_count ; i++)
		if (phy_addr == rtl8221b[i].hDevice.port)	return i;

	RTL8221B_ERROR("phy_addr %d is not found", phy_addr);
	return INVALID_VALUE;	/* invalid value */
}

BOOLEAN
Rtl8221_enable_get(
    IN HANDLE hDevice,
    OUT BOOL *pEnable
)
{
    BOOL status = FAILURE;
    UINT16 phydata = 0;

    status = MmdPhyRead(hDevice, MMD_PMAPMD, 0, &phydata);
    if (status != SUCCESS)
        goto exit;

    *pEnable = (phydata & BIT_11) ? (FALSE) : (TRUE);

exit:
    return status;
}

void rtl8221b_usage(const char *filename)
{
	RTL8221B_MSG("rtl8221b Usage");

	if((strcasecmp(filename, PROC_FILE_PHY) == 0) || (strcasecmp(filename, PROC_FILE_HELP) == 0)) {
		RTL8221B_PRINT("echo [$action] [$phyad] > /proc/%s/%s", PROC_DIR_RTL8221B, PROC_FILE_PHY);
		RTL8221B_PRINT("\t$action: {init,reset}");
		RTL8221B_PRINT("\t$phyad: rtl8221b phy address");
		RTL8221B_PRINT("echo mmdr [mdio_set] [phy_id] [vender_id] [reg] > /proc/%s/%s", PROC_DIR_RTL8221B, PROC_FILE_PHY);
		RTL8221B_PRINT("echo mmdw [mdio_set] [phy_id] [vender_id] [reg] [data] > /proc/%s/%s", PROC_DIR_RTL8221B, PROC_FILE_PHY);
		RTL8221B_PRINT("echo power_up > /proc/%s/%s: Power Up ALL rtl8221b", PROC_DIR_RTL8221B, PROC_FILE_PHY);
		RTL8221B_PRINT("echo power_down > /proc/%s/%s: Power Down ALL rtl8221b", PROC_DIR_RTL8221B, PROC_FILE_PHY);
		if(strcasecmp(filename, PROC_FILE_PHY) == 0)	return;
	}

	if((strcasecmp(filename, PROC_FILE_LINK_STATUS) == 0) || (strcasecmp(filename, PROC_FILE_HELP) == 0)) {
		RTL8221B_PRINT("cat /proc/%s/%s", PROC_DIR_RTL8221B, PROC_FILE_LINK_STATUS);
		if(strcasecmp(filename, PROC_FILE_LINK_STATUS) == 0)	return;
	}

	return;
}

/*
 * rtl8221b proc function
 */
#define MAX_COMMAND_LEN	32

static int help_fops(struct seq_file *s, void *v)
{
	rtl8221b_usage(PROC_FILE_HELP);
	return SUCCESS;
}

static int str_valid(const char *s)
{
	const char *p;

	for (p = s; *p != '\0' && *p != '\r' && *p != '\n'; p++) {
		if ((*p < 32) || (*p > 126))
			return 0;
	}
	return 1;
}

static int Rtl8221b_phy_fops(struct file *file, const char __user *buffer, size_t count, loff_t *ppos)
{
	int i = 0, index = 0;
	char tmpbuf[MAX_COMMAND_LEN] = {0};
	size_t len;
	static PHY_LINK_ABILITY phylinkability;
	int phyAddr;
	int xmdio,xvend;
	HANDLE xDevice;
	UINT16 xreg, phyData = 0;
	BOOL status = FAILURE;

	RTL8221B_DEBUG("%s - %s: %s (%d) - %s", file->f_path.dentry->d_iname, __FUNCTION__, buffer, count);

	if (buffer) {
		char *strptr, *split_str;

		len = min(count, sizeof(tmpbuf));
		/* copy data to the buffer */
		strscpy(tmpbuf, buffer, len);
		//tmpbuf[sizeof(tmpbuf) - 1] = '\0';
		if (!str_valid(tmpbuf))
			return -EINVAL;

		strptr = tmpbuf;
		RTL8221B_DEBUG("strptr: [%s]", strptr);

		/*parse command*/
		split_str = strsep(&strptr," ");
		RTL8221B_DEBUG("split_str [%s]", split_str);

		if(strcasecmp(split_str, "init") == 0) {
			BOOL singlephy = 1;
			split_str = strsep(&strptr," ");

			if (split_str == NULL)		goto phy_error;
			phyAddr = simple_strtol(split_str, NULL, 0);

			index = rtl8221b_index_by_addr(phyAddr);
			if (index == INVALID_VALUE)	goto phy_error;

			status = Rtl8221_phy_init(rtl8221b[i].hDevice, &phylinkability,singlephy);
			if (status != SUCCESS)		goto phy_error;

			status = Rtl8221_enable_set(rtl8221b[i].hDevice, ENABLED);
			if (status != SUCCESS)		goto phy_error;
		}
		else if(strcasecmp(split_str, "reset") == 0) {
			split_str = strsep(&strptr," ");
			if (split_str == NULL)		goto phy_error;
			phyAddr = simple_strtol(split_str, NULL, 0);

			index = rtl8221b_index_by_addr(phyAddr);
			if (index == INVALID_VALUE)	goto phy_error;

			status = Rtl8221_phy_reset(rtl8221b[i].hDevice);
			if (status != SUCCESS)		goto phy_error;

		}
		else if(strcasecmp(split_str, "mmdr") == 0) {
			split_str = strsep(&strptr," ");
			xmdio = simple_strtol(split_str, NULL, 0);

			split_str = strsep(&strptr," ");
			if (split_str == NULL)		goto phy_error;
			phyAddr = simple_strtol(split_str, NULL, 0);

			split_str = strsep(&strptr," ");
			if (split_str == NULL)		goto phy_error;
			xvend = simple_strtol(split_str, NULL, 0);

			split_str = strsep(&strptr," ");
			if (split_str == NULL)		goto phy_error;
			xreg = simple_strtol(split_str, NULL, 16);

			xDevice.unit = xmdio;
			xDevice.port = phyAddr;
#ifndef CONFIG_LUNA_G3_SERIES
			if (xmdio == 0) {
				xDevice.gpio_mdc = RTL9607C_SET0_MDC_PIN;
				xDevice.gpio_mdio = RTL9607C_SET0_MDIO_PIN;
			}
			else {
				xDevice.gpio_mdc = RTL9607C_SET1_MDC_PIN;
				xDevice.gpio_mdio = RTL9607C_SET1_MDIO_PIN;
			}
#endif
			status = MmdPhyRead(xDevice, xvend, xreg, &phyData);
			if (status != SUCCESS)		goto phy_error;
			else				RTL8221B_PRINT(" => MmdPhyRead: reg 0x%x, data 0x%x\n", xreg, phyData);
		}
		else if(strcasecmp(split_str, "mmdw") == 0) {
			split_str = strsep(&strptr," ");
			xmdio = simple_strtol(split_str, NULL, 0);

			split_str = strsep(&strptr," ");
			if (split_str == NULL)		goto phy_error;
			phyAddr = simple_strtol(split_str, NULL, 0);

			split_str = strsep(&strptr," ");
			if (split_str == NULL)		goto phy_error;
			xvend = simple_strtol(split_str, NULL, 0);

			split_str = strsep(&strptr," ");
			if (split_str == NULL)		goto phy_error;
			xreg = simple_strtol(split_str, NULL, 16);

			split_str = strsep(&strptr," ");
			if (split_str == NULL)		goto phy_error;
			phyData = simple_strtol(split_str, NULL, 16);

			xDevice.unit = xmdio;
			xDevice.port = phyAddr;
#ifndef CONFIG_LUNA_G3_SERIES
			if (xmdio == 0) {
				xDevice.gpio_mdc = RTL9607C_SET0_MDC_PIN;
				xDevice.gpio_mdio = RTL9607C_SET0_MDIO_PIN;
			}
			else {
				xDevice.gpio_mdc = RTL9607C_SET1_MDC_PIN;
				xDevice.gpio_mdio = RTL9607C_SET1_MDIO_PIN;
			}
#endif
			status = MmdPhyWrite(xDevice, xvend, xreg, phyData);
			if (status != SUCCESS)		goto phy_error;
			else				RTL8221B_PRINT(" => MmdPhyWrite: reg 0x%x, data 0x%x\n", xreg, phyData);
		}
		else if(strcasecmp(split_str, "power_up") == 0)
			for (i = 0 ; i < rtl8221b_count ; i++)		Rtl8221_enable_set(rtl8221b[i].hDevice, ENABLED);
		else if(strcasecmp(split_str, "power_down") == 0)
			for (i = 0 ; i < rtl8221b_count ; i++)		Rtl8221_enable_set(rtl8221b[i].hDevice, DISABLED);
		else
			goto phy_error;
	}

	return count;

phy_error:
	RTL8221B_ERROR("%s() FAIL ......", __FUNCTION__);
	rtl8221b_usage(file->f_path.dentry->d_iname);
	return -EFAULT;
}

static int Rtl8221_link_status_fops(struct seq_file *s, void *v)
{
	int i = 0;
	BOOL status = FAILURE, linkok = INVALID_VALUE, enable = FALSE, serdesLink = PHY_SERDES_MODE_NO_SDS;
	UINT16 pSpeed;
	PHY_SERDES_MODE SerdesMode;
	char sDesc[16];

	for (i = 0 ; i < rtl8221b_count ; i++) {
		RTL8221B_PRINT("--------------------------------");
		if (rtl8221b[i].mac == INVALID_VALUE)
			RTL8221B_PRINT("[Addr %d]", rtl8221b[i].hDevice.port);
		else
			RTL8221B_PRINT("Port %d  [Addr %d]", rtl8221b[i].mac, rtl8221b[i].hDevice.port);

		status = Rtl8221_enable_get(rtl8221b[i].hDevice, &enable);
		if (status != SUCCESS)		goto link_status_error;
		RTL8221B_PRINT("\tPower\t: %s", enable ? "UP" : "DOWN");

		status = Rtl8221_is_link(rtl8221b[i].hDevice, &linkok);
		if (status != SUCCESS)		goto link_status_error;

		RTL8221B_PRINT("\tLink\t: %s", linkok ? "UP" : "DOWN");

		status = Rtl8221_speed_get(rtl8221b[i].hDevice, &pSpeed);
		if (status != SUCCESS)		goto link_status_error;

		switch (pSpeed) {
		case LINK_SPEED_10M:
			strncpy(sDesc, "10Mbps\0", 7);
			break;
		case LINK_SPEED_100M:
			strncpy(sDesc, "100Mbps\0", 8);
			break;
		case LINK_SPEED_500M:
			strncpy(sDesc, "500Mbps\0", 8);
			break;
		case LINK_SPEED_1G:
			strncpy(sDesc, "1Gbps\0", 6);
			break;
		case LINK_SPEED_2P5G:
			strncpy(sDesc, "2.5Gbps\0", 8);
			break;
		case NO_LINK:
			strncpy(sDesc, "nolink\0", 7);
			break;
		default:
			strncpy(sDesc, "unknown\0", 8);
			break;
		}
		RTL8221B_PRINT("\tSpeed\t: %s (%d)", sDesc, pSpeed);

		status = Rtl8221_duplex_get(rtl8221b[i].hDevice, &enable);
		if (status != SUCCESS)		goto link_status_error;
		RTL8221B_PRINT("\tDuplex\t: %s", enable ? "FULL": "HALF");

		status = Rtl8221_serdes_link_get(rtl8221b[i].hDevice, &serdesLink, &SerdesMode);
		if (status != SUCCESS)		goto link_status_error;
		switch (SerdesMode) {
		case PHY_SERDES_MODE_SGMII:
			strncpy(sDesc, "SGMII\0", 6);
			break;
		case PHY_SERDES_MODE_USXGMII:
			strncpy(sDesc, "USXGMII\0", 8);
			break;
		case PHY_SERDES_MODE_HiSGMII:
			strncpy(sDesc, "HiSGMII\0", 8);
			break;
		case PHY_SERDES_MODE_2500BASEX:
			strncpy(sDesc, "2500BASEX\0", 10);
			break;
		case PHY_SERDES_MODE_NO_SDS:
		case PHY_SERDES_MODE_OTHER:
		default:
			strncpy(sDesc, "unknown\0", 8);
			break;
		}
		RTL8221B_PRINT("\tSerdes\t: %s / %s (%d)", serdesLink ? "UP" : "DOWN", sDesc, SerdesMode);
	}
	RTL8221B_PRINT("--------------------------------");
	return SUCCESS;

link_status_error:
	RTL8221B_ERROR("%s() FAIL ......", __FUNCTION__);
	rtl8221b_usage(PROC_FILE_LINK_STATUS);
	return -EFAULT;
}

/*
 * rtl8221b proc definition
 */
typedef enum rtk_rtl8221b_procDir_e
{
	PROC_DIR_RTL8221B_ROOT,
	PROC_DIR_RTL8221B_LEEF //this field must put at last.
} rtk_rtl8221b_procDir_t;

typedef struct rtk_rtl8221b_proc_s
{
	char *name;
	int (*get) (struct seq_file *s, void *v);
	int (*set) (struct file *file, const char __user *buffer, size_t count, loff_t *ppos);
	unsigned int inode_id;
	unsigned char unlockBefortWrite;
#if LINUX_VERSION_CODE >= KERNEL_VERSION(5,6,0)
	struct proc_ops proc_fops;
#else
	struct file_operations proc_fops;
#endif
	rtk_rtl8221b_procDir_t dir;
} rtk_rtl8221b_proc_t;

rtk_rtl8221b_proc_t rtl8221bProc[]=
{
	{
		.name= PROC_FILE_HELP,
		.get = help_fops,
		.set = NULL,
		.dir = PROC_DIR_RTL8221B_ROOT,
	},
	{
		.name= PROC_FILE_PHY,
		.get = NULL,
		.set = Rtl8221b_phy_fops,
		.dir = PROC_DIR_RTL8221B_ROOT,
	},
	{
		.name= PROC_FILE_LINK_STATUS,
		.get = Rtl8221_link_status_fops,
		.set = NULL,
		.dir = PROC_DIR_RTL8221B_ROOT,
	},
};

/*
 * rtl8221b common proc function
 */
static void *rtk_rtl8221b_single_start(struct seq_file *p, loff_t *pos)
{
    return NULL + (*pos == 0);
}

static void *rtk_rtl8221b_single_next(struct seq_file *p, void *v, loff_t *pos)
{
    ++*pos;
    return NULL;
}

static void rtk_rtl8221b_single_stop(struct seq_file *p, void *v)
{
}

int rtk_rtl8221b_seq_open(struct file *file, const struct seq_operations *op)
{
	seq_open(file, op);

    return 0;
}

int rtk_rtl8221b_single_open(struct file *file, int (*show) (struct seq_file *m, void *v), void *data)
{
    struct seq_operations *op = kmalloc(sizeof(*op), GFP_ATOMIC);
    int res = -ENOMEM;

    if (op)
    {
        op->start = rtk_rtl8221b_single_start;
        op->next = rtk_rtl8221b_single_next;
        op->stop = rtk_rtl8221b_single_stop;
        op->show = show;
        res = rtk_rtl8221b_seq_open(file, op);
        if (!res)
            ((struct seq_file *)file->private_data)->private = data;
        else
            kfree(op);
    }
    return res;
}

static int rtk_rtl8221b_nullDebugGet(struct seq_file *s, void *v)
{
    return 0;
}

static int rtk_rtl8221b_nullDebugSingleOpen(struct inode *inode, struct file *file)
{
    return(single_open(file, rtk_rtl8221b_nullDebugGet, NULL));
}

static int rtk_rtl8221b_commonDebugSingleOpen(struct inode *inode, struct file *file)
{
    int i, ret = -1;

    //rtl8221b_spin_lock_bh(rtl8221bSysdb.lock_rtl8221b);
    //========================= Critical Section Start =========================//
    for( i = 0; i < (sizeof(rtl8221bProc) / sizeof(rtk_rtl8221b_proc_t)) ; i++)
    {
        //RTL8221B_MSG("common_single_open inode_id=%u i_ino=%u", rtl8221bProc[i].inode_id,(unsigned int)inode->i_ino);

        if(rtl8221bProc[i].inode_id == (unsigned int)inode->i_ino)
        {
            ret = rtk_rtl8221b_single_open(file, rtl8221bProc[i].get, NULL);
            break;
        }
    }
    //========================= Critical Section End =========================//
    //rtl8221b_spin_unlock_bh(rtl8221bSysdb.lock_rtl8221b);

    return ret;
}

#define CPY_BUF_SIZE_MAX (128)
// coverity[ +taint_sanitize : arg-*0 ]
bool rtk_rtl8221b_coverity_sanitize_string(const char *str) 
{
	//just avoid coverity issue	
	//checking string len, we want to sanitize procBuffer for coverity
	int i;
	int ret=true;

	for(i=0;i<CPY_BUF_SIZE_MAX;i++)
	{
		if((str[i]<0x20 ||  str[i]==0x7f))
			ret=false;
	}
	if(!(strlen(str) < (CPY_BUF_SIZE_MAX-1)))
		ret=false;

	return ret;
}

static ssize_t rtk_rtl8221b_commonDebugSingleWrite(struct file * file, const char __user * userbuf,
        size_t count, loff_t * off)
{
    int i, ret = -1;
	char procBuffer[CPY_BUF_SIZE_MAX] = {0};
    char *pBuffer = NULL;
	int len = (count >= (CPY_BUF_SIZE_MAX) ) ? (CPY_BUF_SIZE_MAX) : count;

	if(len==0)
		return count;
    /* write data to the buffer */
	if (!userbuf || copy_from_user(procBuffer, userbuf, len))
		return -EFAULT;

	procBuffer[len-1] = '\0';	//echo string from user is  abc	=> 'a''b''c''\n' len=4 and no char '\0' , replace '\n' to '\0'

	rtk_rtl8221b_coverity_sanitize_string(procBuffer);

    //rtl8221b_spin_lock_bh(rtl8221bSysdb.lock_rtl8221b);
    //========================= Critical Section Start =========================//
    for( i = 0; i < (sizeof(rtl8221bProc) / sizeof(rtk_rtl8221b_proc_t)) ; i++)
    {
        //RTL8221B_MSG("common_single_write inode_id=%u i_ino=%u",rtl8221bProc[i].inode_id,(unsigned int)file->f_dentry->d_inode->i_ino);

        if(rtl8221bProc[i].inode_id == (unsigned int)file->f_inode->i_ino)
        {
            //if(rtl8221bProc[i].unlockBefortWrite)
            //	rtl8221b_spin_unlock_bh(rtl8221bSysdb.lock_rtl8221b);
            ret = rtl8221bProc[i].set(file, procBuffer, len, off);
            break;
        }
    }
    //========================= Critical Section End =========================//
    //if((i!=(sizeof(rtl8221bProc)/sizeof(rtk_rtl8221b_proc_t))) && !rtl8221bProc[i].unlockBefortWrite)
    //	rtl8221b_spin_unlock_bh(rtl8221bSysdb.lock_rtl8221b);

    return ret;
}

BOOLEAN rtk_rtl8221b_proc_init(void)
{
	int i = 0;
	struct proc_dir_entry *p = NULL;

	rtl8221b_proc_dir = proc_mkdir(PROC_DIR_RTL8221B, NULL);
	if (rtl8221b_proc_dir == NULL) {
		RTL8221B_ERROR("create /proc/%s failed!", PROC_DIR_RTL8221B);
		return FAILURE;
	}

	for(i = 0; i < (sizeof(rtl8221bProc)/sizeof(rtk_rtl8221b_proc_t)) ; i++) {
		struct proc_dir_entry *parentDir = NULL;

#if LINUX_VERSION_CODE >= KERNEL_VERSION(5,6,0)
		if(rtl8221bProc[i].get == NULL)
			rtl8221bProc[i].proc_fops.proc_open = rtk_rtl8221b_nullDebugSingleOpen;
		else
			rtl8221bProc[i].proc_fops.proc_open = rtk_rtl8221b_commonDebugSingleOpen;

		if(rtl8221bProc[i].set == NULL)
			rtl8221bProc[i].proc_fops.proc_write = NULL;
		else
			rtl8221bProc[i].proc_fops.proc_write = rtk_rtl8221b_commonDebugSingleWrite;

		rtl8221bProc[i].proc_fops.proc_read = seq_read;
		rtl8221bProc[i].proc_fops.proc_lseek = seq_lseek;
		rtl8221bProc[i].proc_fops.proc_release = single_release;
#else
		if(rtl8221bProc[i].get == NULL)
			rtl8221bProc[i].proc_fops.open = rtk_rtl8221b_nullDebugSingleOpen;
		else
			rtl8221bProc[i].proc_fops.open = rtk_rtl8221b_commonDebugSingleOpen;

		if(rtl8221bProc[i].set == NULL)
			rtl8221bProc[i].proc_fops.write = NULL;
		else
			rtl8221bProc[i].proc_fops.write = rtk_rtl8221b_commonDebugSingleWrite;

		rtl8221bProc[i].proc_fops.read = seq_read;
		rtl8221bProc[i].proc_fops.llseek = seq_lseek;
		rtl8221bProc[i].proc_fops.release = single_release;
#endif

		switch(rtl8221bProc[i].dir) {
		case PROC_DIR_RTL8221B_ROOT:
			parentDir = rtl8221b_proc_dir;
			break;
		default:
			break;
		}

		p = proc_create_data(rtl8221bProc[i].name, S_IRUGO, parentDir, &(rtl8221bProc[i].proc_fops),NULL);
		if(!p)	RTL8221B_ERROR("create proc %s failed!", rtl8221bProc[i].name);
		else	rtl8221bProc[i].inode_id = p->low_ino;
	}

	RTL8221B_MSG("Creat %d proc entry.", i);
	return SUCCESS;
}

void rtk_rtl8221b_proc_exit(void)
{
	int i = 0;

	for(i = 0 ; i < (sizeof(rtl8221bProc) / sizeof(rtk_rtl8221b_proc_t)) ; i++) {
		struct proc_dir_entry *parentDir = NULL;

		switch(rtl8221bProc[i].dir) {
		case PROC_DIR_RTL8221B_ROOT:
			parentDir = rtl8221b_proc_dir;
			break;
		default:
			break;
		}
		remove_proc_entry(rtl8221bProc[i].name, parentDir);
	}
	proc_remove(rtl8221b_proc_dir);
}

static void rtl8221b_led_device_release(struct device *dev)
{
	RTL8221B_INFO("%s(%d)", __FUNCTION__, __LINE__);
	return;
}

static struct platform_device rtl8221b_led_device = {
	.name	= "leds-rtl8221b",
	.id	= -1,
	.dev.release = rtl8221b_led_device_release,
};

#if LINUX_VERSION_CODE < KERNEL_VERSION(4,11,0)
#define LED_ON (1)
#endif
static DEFINE_SPINLOCK(value_lock);
static void rtl8221b_led_brightness_set(struct led_classdev *led_cdev,
		enum led_brightness value)
{
	struct rtl8221b_led *led = container_of(led_cdev, struct rtl8221b_led, cdev);
	unsigned long flags;
	u32 index;
	UINT16 phyData = 0;

	index = rtl8221b_index_by_mac(led->mac);
	if (index == INVALID_VALUE) {
		RTL8221B_ERROR("Unknown mac%d", led->mac);
		return;
	}
	RTL8221B_DEBUG("%s(%d): Set mac%d addr%d led%d LED_%s", __FUNCTION__, __LINE__,
		led->mac, rtl8221b[index].hDevice.port, led->id, (value == LED_OFF) ? "OFF" : "ON");

	spin_lock_irqsave(&value_lock, flags);

	if (value == LED_OFF) {
		switch(led->id) {
		case 0:
			if (MmdPhyRead(rtl8221b[index].hDevice, MMD_VEND2, 0xD044, &phyData) == SUCCESS) {
				phyData &= ~(1 << 4);	/* LED0 Disable */
				phyData &= ~(1 << 0);	/* LED0 polarity - Active low */
				(void)MmdPhyWrite(rtl8221b[index].hDevice, MMD_VEND2, 0xD044, phyData);
			}
			break;
		case 1:
			if (MmdPhyRead(rtl8221b[index].hDevice, MMD_VEND2, 0xD044, &phyData) == SUCCESS) {
				phyData &= ~(1 << 5);	/* LED1 Disable */
				phyData &= ~(1 << 1);	/* LED1 polarity - Active low */
				(void)MmdPhyWrite(rtl8221b[index].hDevice, MMD_VEND2, 0xD044, phyData);
			}
			break;
		case 2:
			if (MmdPhyRead(rtl8221b[index].hDevice, MMD_VEND2, 0xD044, &phyData) == SUCCESS) {
				phyData &= ~(1 << 6);	/* LED2 Disable */
				phyData &= ~(1 << 2);	/* LED2 polarity - Active low */
				(void)MmdPhyWrite(rtl8221b[index].hDevice, MMD_VEND2, 0xD044, phyData);
			}
			break;
		default:
			RTL8221B_ERROR("Unsupported LED%d", led->id);
			break;
		}
	}
	else if (value == LED_ON) {
		switch(led->id) {
		case 0:
			if (MmdPhyRead(rtl8221b[index].hDevice, MMD_VEND2, 0xD044, &phyData) == SUCCESS) {
				phyData &= ~(1 << 4);	/* LED0 Disable */
				phyData |= (1 << 0);	/* LED0 polarity - Active high */
				(void)MmdPhyWrite(rtl8221b[index].hDevice, MMD_VEND2, 0xD044, phyData);
			}
			break;
		case 1:
			if (MmdPhyRead(rtl8221b[index].hDevice, MMD_VEND2, 0xD044, &phyData) == SUCCESS) {
				phyData &= ~(1 << 5);	/* LED1 Disable */
				phyData |= (1 << 1);	/* LED1 polarity - Active high */
				(void)MmdPhyWrite(rtl8221b[index].hDevice, MMD_VEND2, 0xD044, phyData);
			}
			break;
		case 2:
			if (MmdPhyRead(rtl8221b[index].hDevice, MMD_VEND2, 0xD044, &phyData) == SUCCESS) {
				phyData &= ~(1 << 6);	/* LED2 Disable */
				phyData |= (1 << 2);	/* LED2 polarity - Active high */
				(void)MmdPhyWrite(rtl8221b[index].hDevice, MMD_VEND2, 0xD044, phyData);
			}
			break;
		default:
			RTL8221B_ERROR("Unsupported LED%d", led->id);
			break;
		}
	}
	else {
		/* Set to default: Controlled by HW */
		switch(led->id) {
		case 0:
			if (MmdPhyRead(rtl8221b[index].hDevice, MMD_VEND2, 0xD044, &phyData) == SUCCESS) {
				phyData |= (1 << 4);	/* LED0 Enable */
				phyData &= ~(1 << 0);	/* LED0 polarity - Active low */
				(void)MmdPhyWrite(rtl8221b[index].hDevice, MMD_VEND2, 0xD044, phyData);
			}
			break;
		case 1:
			if (MmdPhyRead(rtl8221b[index].hDevice, MMD_VEND2, 0xD044, &phyData) == SUCCESS) {
				phyData |= (1 << 5);	/* LED1 Enable */
				phyData &= ~(1 << 1);	/* LED1 polarity - Active low */
				(void)MmdPhyWrite(rtl8221b[index].hDevice, MMD_VEND2, 0xD044, phyData);
			}
			break;
		case 2:
			if (MmdPhyRead(rtl8221b[index].hDevice, MMD_VEND2, 0xD044, &phyData) == SUCCESS) {
				phyData |= (1 << 6);	/* LED2 Enable */
				phyData &= ~(1 << 2);	/* LED2 polarity - Active low */
				(void)MmdPhyWrite(rtl8221b[index].hDevice, MMD_VEND2, 0xD044, phyData);
			}
			break;
		default:
			RTL8221B_ERROR("Unsupported LED%d", led->id);
			break;
		}
	}

	spin_unlock_irqrestore(&value_lock, flags);
	return;
}

static int rtl8221b_led_probe(struct platform_device *pdev)
{
	int i, j;
	int ret = 0;

	RTL8221B_MSG("%s(%d)", __FUNCTION__, __LINE__);
	for (i = 0 ; i < rtl8221b_count ; i++) {
		for (j = 0 ; j < RTK_RTL8221B_LED_NUM ; j++) {
			rtl8221b[i].leds[j].cdev.name = rtl8221b[i].leds[j].name;
			rtl8221b[i].leds[j].cdev.brightness_set = rtl8221b_led_brightness_set;

			ret = devm_led_classdev_register(&pdev->dev, &rtl8221b[i].leds[j].cdev);
			if (ret < 0)	RTL8221B_ERROR("led_classdev_register Fail");
		}
	}

	return ret;
}

static int rtl8221b_led_remove(struct platform_device *pdev)
{
	int ret = 0;

	RTL8221B_MSG("%s(%d)", __FUNCTION__, __LINE__);

	return ret;
}

static struct platform_driver rtl8221b_led_driver = {
	.driver	= {
		.name = "leds-rtl8221b",
	},
	.probe = rtl8221b_led_probe,
	.remove = rtl8221b_led_remove,
};
//module_platform_driver(rtl8221b_led_driver);

static struct task_struct *rtl8221b_device_init_task;
#define RTL8221B_DEVICE_INIT_KTHREAD_CPU	0

static int rtk_rtl8221b_device_init_thread (void *data)
{
	int i, j;
	BOOL status = FAILURE;
	BOOL singlephy = 1;
	static PHY_LINK_ABILITY phylinkability;

	for (i = 0 ; i < rtl8221b_count ; i++) {
		RTL8221B_DEBUG("Init Device %d - mac %d ; unit %d ; port %d", i, rtl8221b[i].mac, rtl8221b[i].hDevice.unit, rtl8221b[i].hDevice.port);
		status = Rtl8221_phy_init(rtl8221b[i].hDevice, &phylinkability, singlephy);
		if (status == SUCCESS) {
			RTL8221B_INFO("Init PHY mac%d [unit%d addr%d] Done", rtl8221b[i].mac, rtl8221b[i].hDevice.unit, rtl8221b[i].hDevice.port);
			/* SDS mode */
			RTL8221B_MSG("default serdes mode: %d", DEFAULT_SERDES_MODE);
			if ((status = Rtl8221_serdes_option_set_for_init(rtl8221b[i].hDevice, DEFAULT_SERDES_MODE /* HiSGMII/SGMII */)) != SUCCESS)
				RTL8221B_ERROR("Device %d mac %d unit %u port %u RTL8221b Rtl8221_serdes_option_set_for_init status = 0x%x", i,
					rtl8221b[i].mac, rtl8221b[i].hDevice.unit, rtl8221b[i].hDevice.port, status);
			else
				RTL8221B_DEBUG("Device %d mac %d unit %u port %u RTL8221b Rtl8221_serdes_option_set_for_init status = 0x%x", i,
					rtl8221b[i].mac, rtl8221b[i].hDevice.unit, rtl8221b[i].hDevice.port, status);
			Rtl8221_port_type_prefer_set(rtl8221b[i].hDevice, PHY_MASTER_MODE);
#ifdef CONFIG_SWITCH_INIT_LINKDOWN
			Rtl8221_enable_set(rtl8221b[i].hDevice, DISABLED);
#endif
			/* LEDs */
			for (j = 0 ; j < RTK_RTL8221B_LED_NUM ; j++) {
				rtl8221b[i].leds[j].mac = rtl8221b[i].mac;
				rtl8221b[i].leds[j].id = j;
				sprintf(rtl8221b[i].leds[j].name, "LED_8221B_P%d_%d", rtl8221b[i].mac, j);
				RTL8221B_MSG("Add [/sys/class/leds/%s]", rtl8221b[i].leds[j].name);
			}
		}
		else {
			RTL8221B_ERROR("Init PHY mac%d: [unit%d addr%d] Fail. (status = 0x%x)", rtl8221b[i].mac, rtl8221b[i].hDevice.unit, rtl8221b[i].hDevice.port, status);
			return -1;
		}
	}

	platform_device_register(&rtl8221b_led_device);
	platform_driver_register(&rtl8221b_led_driver);
	return 0;
}

#if IS_BUILTIN(CONFIG_RTK_EXT_GPHY) || IS_MODULE(CONFIG_RTK_EXT_GPHY)
int __eth_phy_rtl8226b_add_phy(u8 port, u8 phy_addr)
{
	RTL8221B_DEBUG("%s(%d): port = %d, phy_dev = %d", __FUNCTION__, __LINE__, port, phy_addr);
	if (rtl8221b_count < RTK_MAX_RTL8221B_NUM) {
		rtl8221b[rtl8221b_count].mac = port;
		rtl8221b[rtl8221b_count].hDevice.unit = CONFIG_RTL_8221B_DEVICE_0_MDIO_SET;
		rtl8221b[rtl8221b_count].hDevice.port = phy_addr;
#ifndef CONFIG_LUNA_G3_SERIES
		if(CONFIG_RTL_8221B_DEVICE_0_MDIO_SET == 0) {
			rtl8221b[rtl8221b_count].hDevice.gpio_mdc = RTL9607C_SET0_MDC_PIN;
			rtl8221b[rtl8221b_count].hDevice.gpio_mdio = RTL9607C_SET0_MDIO_PIN;
		}
		else if(CONFIG_RTL_8221B_DEVICE_0_MDIO_SET == 1) {
			rtl8221b[rtl8221b_count].hDevice.gpio_mdc = RTL9607C_SET1_MDC_PIN;
			rtl8221b[rtl8221b_count].hDevice.gpio_mdio = RTL9607C_SET1_MDIO_PIN;
		}
#endif
		rtl8221b_count++;
	}
	else
		RTL8221B_ERROR("Reach Support rtl8221B number for port %u ; phyAddr %u", port, phy_addr);

	return 0;
}
EXPORT_SYMBOL(__eth_phy_rtl8226b_add_phy);

int __eth_phy_rtl8226b_init(u8 phy_dev)
{
	RTL8221B_DEBUG("%s(%d)", __FUNCTION__, __LINE__);

	if (rtl8221b_init) {
		RTL8221B_MSG("All RTL8221b are init.");
		return 1;
	}

	/* Create rtl8221b phy init kthread */
	rtl8221b_device_init_task = kthread_create(rtk_rtl8221b_device_init_thread, NULL, "rtl8221b_device_init/%d", RTL8221B_DEVICE_INIT_KTHREAD_CPU);
	if (WARN_ON(!rtl8221b_device_init_task))
		RTL8221B_ERROR("Create rtl8221b_device1_init/%d failed!", RTL8221B_DEVICE_INIT_KTHREAD_CPU);
	kthread_bind(rtl8221b_device_init_task, RTL8221B_DEVICE_INIT_KTHREAD_CPU);
	wake_up_process(rtl8221b_device_init_task);

	rtl8221b_init = 1;
	return 0;
}
EXPORT_SYMBOL(__eth_phy_rtl8226b_init);

extern u32 __eth_phy_rtl8226b_admin_enable_set(u8 phy_addr, bool power_up)
{
	BOOL status = FAILURE;
	status = Rtl8221_enable_set(rtl8221b[rtl8221b_index_by_addr(phy_addr)].hDevice, power_up);

	if (status == SUCCESS)	return 0;
	return 1;
}
EXPORT_SYMBOL(__eth_phy_rtl8226b_admin_enable_set);

extern u32 __eth_phy_rtl8226b_admin_enable_get(u8 phy_addr, bool *power_up)
{
	BOOL status = FAILURE;
	status = Rtl8221_enable_get(rtl8221b[rtl8221b_index_by_addr(phy_addr)].hDevice, power_up);

	if (status == SUCCESS)	return 0;
	return 1;
}
EXPORT_SYMBOL(__eth_phy_rtl8226b_admin_enable_get);

extern u32 __eth_phy_rtl8226b_phy_powerDown_set(u8 phy_addr, bool enable)
{
	BOOL status = FAILURE;
	if (enable == ENABLED)//POWER DOWN
		enable = DISABLED;
	else 
		enable = ENABLED;
	status = Rtl8221_enable_set(rtl8221b[rtl8221b_index_by_addr(phy_addr)].hDevice, enable);

	if (status == SUCCESS)	return 0;
	return 1;
}
EXPORT_SYMBOL(__eth_phy_rtl8226b_phy_powerDown_set);

extern u32 __eth_phy_rtl8226b_phy_powerDown_get(u8 phy_addr, bool *enable)
{
	BOOL status = FAILURE;
	status = Rtl8221_enable_get(rtl8221b[rtl8221b_index_by_addr(phy_addr)].hDevice, enable);
	if (*enable == ENABLED)//POWER UP
		*enable = DISABLED;
	else 
		*enable = ENABLED;
	if (status == SUCCESS)	return 0;
	return 1;
}
EXPORT_SYMBOL(__eth_phy_rtl8226b_phy_powerDown_get);


u32 __eth_phy_rtl8226b_auto_neg_set(u8 phy_addr, rtk_extgphy_ability_t *pAbility)
{
	BOOL status = FAILURE;
	PHY_LINK_ABILITY phyLinkAbility;
	RTL8221B_DEBUG("%s(%d): phy_addr = %d", __FUNCTION__, __LINE__, phy_addr);

	phyLinkAbility.Half_10   = pAbility->Half_10;
	phyLinkAbility.Full_10   = pAbility->Full_10;
	phyLinkAbility.Half_100  = pAbility->Half_100;
	phyLinkAbility.Full_100  = pAbility->Full_100;
	phyLinkAbility.Full_1000 = pAbility->Full_1000;
	phyLinkAbility.adv_2_5G  = pAbility->Full_2500;
	phyLinkAbility.FC        = pAbility->FC;
	phyLinkAbility.AsyFC     = pAbility->AsyFC;

	status = Rtl8221_autoNegoAbility_set(rtl8221b[rtl8221b_index_by_addr(phy_addr)].hDevice, &phyLinkAbility);

	if (status == SUCCESS)	return 0;
	return 1;
}
EXPORT_SYMBOL(__eth_phy_rtl8226b_auto_neg_set);

u32 __eth_phy_rtl8226b_auto_neg_get(u8 phy_addr, rtk_extgphy_ability_t *pAbility)
{
	BOOL status = FAILURE;
	PHY_LINK_ABILITY phyLinkAbility;
	RTL8221B_DEBUG("%s(%d): phy_addr = %d", __FUNCTION__, __LINE__, phy_addr);

	status = Rtl8221_autoNegoAbility_get(rtl8221b[rtl8221b_index_by_addr(phy_addr)].hDevice, &phyLinkAbility);

	if (status == SUCCESS) {
		pAbility->Half_10    = phyLinkAbility.Half_10;
		pAbility->Full_10    = phyLinkAbility.Full_10;
		pAbility->Half_100   = phyLinkAbility.Half_100;
		pAbility->Full_100   = phyLinkAbility.Full_100;
		pAbility->Full_1000  = phyLinkAbility.Full_1000;
		pAbility->Full_2500  = phyLinkAbility.adv_2_5G;
		pAbility->FC         = phyLinkAbility.FC;
		pAbility->AsyFC      = phyLinkAbility.AsyFC;
		return 0;
	}
	return 1;
}
EXPORT_SYMBOL(__eth_phy_rtl8226b_auto_neg_get);

u32 __eth_phy_rtl8226b_link_status_get(u8 phy_addr, ext_phy_link_status_t *link_status)
{
	BOOL status = FAILURE;
	link_status->link_up = 0;
	UINT16 speed;
	BOOL enable;

	status = Rtl8221_is_link(rtl8221b[rtl8221b_index_by_addr(phy_addr)].hDevice, &link_status->link_up);
	if (status == SUCCESS) {
		RTL8221B_DEBUG("phy_addr %d link : %s",phy_addr ,link_status->link_up ? "UP" : "DOWN");

		status = Rtl8221_speed_get(rtl8221b[rtl8221b_index_by_addr(phy_addr)].hDevice, &speed);
		if (status == SUCCESS) {
			switch (speed) {
			case LINK_SPEED_10M:
				link_status->speed = EXT_PHY_SPEED_10;
				break;
			case LINK_SPEED_100M:
				link_status->speed = EXT_PHY_SPEED_100;
				break;
			case LINK_SPEED_500M:
				link_status->speed = EXT_PHY_SPEED_500;
				break;
			case LINK_SPEED_1G:
				link_status->speed = EXT_PHY_SPEED_1000;
				break;
			case LINK_SPEED_2P5G:
				link_status->speed = EXT_PHY_SPEED_2500;
				break;
			case NO_LINK:
			default:
				break;
			}
			//RTL8221B_DEBUG("Rtl8221_speed_get(phyAddr %d): %d [%s]", hDevice0.port, speed, sDesc);
		}
		else
			return 1;

		status = Rtl8221_duplex_get(rtl8221b[rtl8221b_index_by_addr(phy_addr)].hDevice, &enable);
		if (status == SUCCESS)
			link_status->duplex = enable;
		else
			return 1;
	}
	else
		return 1;
	return 0;
}
EXPORT_SYMBOL(__eth_phy_rtl8226b_link_status_get);

u32 __eth_phy_rtl8226b_flow_ctrl_get(u8 phy_addr, u8 *tx_fc, u8 *rx_fc)
{
	Rtl8221_fc_sts_get(rtl8221b[rtl8221b_index_by_addr(phy_addr)].hDevice, tx_fc, rx_fc);
	return 0;
}
EXPORT_SYMBOL(__eth_phy_rtl8226b_flow_ctrl_get);

#endif

#ifndef CONFIG_LUNA_G3_SERIES
unsigned int io_mode_en_value_backup;
#endif

static int __init rtk_rtl8221b_moudle_init(void)
{
	int i;
#ifdef CONFIG_LUNA_G3_SERIES
	int ret;
#else
	int gpio;
#endif
	RTL8221B_MSG("%s", __FUNCTION__);

	/* Init. rtl8221b data */
	for (i = 0 ; i < RTK_MAX_RTL8221B_NUM ; i++) {
		rtl8221b[i].mac = INVALID_VALUE;
		rtl8221b[i].hDevice.unit = INVALID_VALUE;
		rtl8221b[i].hDevice.port = INVALID_VALUE;
		rtl8221b[i].hDevice.gpio_mdc = INVALID_VALUE;
		rtl8221b[i].hDevice.gpio_mdio = INVALID_VALUE;
	}
#ifdef CONFIG_LUNA_G3_SERIES
	/* Reset RTL8221B */
	ret = gpio_is_valid(RTL8221B_RESET_GPIO_NO);
	if (ret != 1) {
		RTL8221B_ERROR("GPIO #%d is NOT Valid !!! (ret = %d)", RTL8221B_RESET_GPIO_NO, ret);
		return -EINVAL;
	}
	ret = gpio_request(RTL8221B_RESET_GPIO_NO, RTL8221B_RESET_GPIO_LABEL);
	if (ret < 0) {
		RTL8221B_ERROR("Request GPIO #%d Error !!! (ret = %d)", RTL8221B_RESET_GPIO_NO, ret);
		return -EINVAL;
	}
	ret = gpio_direction_output(RTL8221B_RESET_GPIO_NO, 1);
	if (ret != 0) {
		RTL8221B_ERROR("Set GPIO #%d Output as 1 Error !!! (ret = %d)", RTL8221B_RESET_GPIO_NO, ret);
		return -EINVAL;
	}
	//gpio_set_value(RTL8221B_RESET_GPIO_NO, 0);
	//mdelay(200);
	//gpio_set_value(RTL8221B_RESET_GPIO_NO, 1);
	gpio_free(RTL8221B_RESET_GPIO_NO);
	mdelay(300);		/* Waitting rtl8221b reset ??? */
#else
	/* Reset RTL8221B */
	gpio = rtk_8221b_reset_gpio_get(0);
	if (gpio < 0)	gpio = RTL8221B_DEV0_RESET_PIN;
	rtk_gpio_state_set(gpio, ENABLED);
	rtk_gpio_mode_set(gpio, GPIO_OUTPUT);
	rtk_gpio_databit_set(gpio, 0);
	mdelay(50);
	rtk_gpio_databit_set(gpio, 1);
	mdelay(100);

	/* MDIO init. */
	io_mode_en_value_backup = REG32(IO_MODE_EN_REG);
	if((CONFIG_RTL_8221B_DEVICE_0_MDIO_SET == 0) || (CONFIG_RTL_8221B_DEVICE_1_MDIO_SET == 0)) {
		RTL8221B_INFO("Enable MDIO master PAD_LED0/PAD_LED1");
		REG32(IO_MODE_EN_REG) |= MDIO_MASTER_EN;
		rtk_gpio_state_set(RTL9607C_SET0_MDC_PIN, DISABLED);
		rtk_gpio_state_set(RTL9607C_SET0_MDIO_PIN, DISABLED);
	}
	if((CONFIG_RTL_8221B_DEVICE_0_MDIO_SET == 1) || (CONFIG_RTL_8221B_DEVICE_1_MDIO_SET == 1)) {
		RTL8221B_INFO("Enable MDIO master PAD_LED4/PAD_LED5");
		REG32(IO_MODE_EN_REG) |= EXT_MDX_M_EN;
		rtk_gpio_state_set(RTL9607C_SET1_MDC_PIN, DISABLED);
		rtk_gpio_state_set(RTL9607C_SET1_MDIO_PIN, DISABLED);
	}
	//REG32(IO_GPIO_EN_REG + ((RTL8221B_MDC_SW_GPIO_NO >> 5) << 2)) &= ~(1 << (RTL8221B_MDC_SW_GPIO_NO % 32));

	rtk_mdio_init();
	//init_mdio_lock();
#endif

#if IS_BUILTIN(CONFIG_RTK_EXT_GPHY) || IS_MODULE(CONFIG_RTK_EXT_GPHY)
	RTL8221B_MSG("Phys init. will be done by extGphy");
#else

#if (!defined(CONFIG_RTL_8221B_DEVICE_0) && !defined(CONFIG_RTL_8221B_DEVICE_1))
	RTL8221B_ERROR("No RTL8221B Device Found !!!");
	return -ENODEV;
#endif

#ifdef CONFIG_RTL_8221B_DEVICE_0
	rtl8221b[rtl8221b_count].hDevice.unit = CONFIG_RTL_8221B_DEVICE_0_MDIO_SET;
	rtl8221b[rtl8221b_count].hDevice.port = CONFIG_RTL_8221B_DEVICE_0_PHY_ADDR;
	if(CONFIG_RTL_8221B_DEVICE_0_MDIO_SET == 0) {
		rtl8221b[rtl8221b_count].hDevice.gpio_mdc = RTL9607C_SET0_MDC_PIN;
		rtl8221b[rtl8221b_count].hDevice.gpio_mdio = RTL9607C_SET0_MDIO_PIN;
	}
	else if(CONFIG_RTL_8221B_DEVICE_0_MDIO_SET == 1) {
		rtl8221b[rtl8221b_count].hDevice.gpio_mdc = RTL9607C_SET1_MDC_PIN;
		rtl8221b[rtl8221b_count].hDevice.gpio_mdio = RTL9607C_SET1_MDIO_PIN;
	}
	RTL8221B_MSG("RTL8221B Device %d (mdio set: %d, phyad: %d)", rtl8221b_count, rtl8221b[rtl8221b_count].hDevice.unit, rtl8221b[rtl8221b_count].hDevice.port);
	rtl8221b_count++;
#endif

#ifdef CONFIG_RTL_8221B_DEVICE_1
	rtl8221b[rtl8221b_count].hDevice.unit = CONFIG_RTL_8221B_DEVICE_1_MDIO_SET;
	rtl8221b[rtl8221b_count].hDevice.port = CONFIG_RTL_8221B_DEVICE_1_PHY_ADDR;
	if(CONFIG_RTL_8221B_DEVICE_1_MDIO_SET == 0) {
		rtl8221b[rtl8221b_count].hDevice.gpio_mdc = RTL9607C_SET0_MDC_PIN;
		rtl8221b[rtl8221b_count].hDevice.gpio_mdio = RTL9607C_SET0_MDIO_PIN;
	}
	else if(CONFIG_RTL_8221B_DEVICE_1_MDIO_SET == 1) {
		rtl8221b[rtl8221b_count].hDevice.gpio_mdc = RTL9607C_SET1_MDC_PIN;
		rtl8221b[rtl8221b_count].hDevice.gpio_mdio = RTL9607C_SET1_MDIO_PIN;
	}
	RTL8221B_MSG("RTL8221B Device %d (mdio set: %d, phyad: %d)", rtl8221b_count, rtl8221b[rtl8221b_count].hDevice.unit, rtl8221b[rtl8221b_count].hDevice.port);
	rtl8221b_count++;
#endif

#if (defined(CONFIG_RTL_8221B_DEVICE_0) && defined(CONFIG_RTL_8221B_DEVICE_1))
	if ((rtl8221b[0].hDevice.unit == rtl8221b[1].hDevice.unit) && (rtl8221b[0].hDevice.port == rtl8221b[1].hDevice.port)) {
		RTL8221B_ERROR("RTL8221B Device 0 and Device 1 are using same MDIO Set (%d) with Same PHY Add (%d)",
			rtl8221b[rtl8221b_count].hDevice[0].unit, rtl8221b[rtl8221b_count].hDevice[0].port);
		return -EINVAL;
	}
#endif
	/* Create rtl8221b phy init kthread */
	rtl8221b_device_init_task = kthread_create(rtk_rtl8221b_device_init_thread, NULL, "rtl8221b_device_init/%d", RTL8221B_DEVICE_INIT_KTHREAD_CPU);
	if (WARN_ON(!rtl8221b_device_init_task))
		RTL8221B_ERROR("Create rtl8221b_device1_init/%d failed!", RTL8221B_DEVICE_INIT_KTHREAD_CPU);
	kthread_bind(rtl8221b_device_init_task, RTL8221B_DEVICE_INIT_KTHREAD_CPU);
	wake_up_process(rtl8221b_device_init_task);
#endif

	rtk_rtl8221b_proc_init();

	return 0;
}

static void __exit rtk_rtl8221b_module_exit(void)
{
#ifndef CONFIG_LUNA_G3_SERIES
	REG32(IO_MODE_EN_REG) = io_mode_en_value_backup;
#endif
	platform_driver_unregister(&rtl8221b_led_driver);
	platform_device_unregister(&rtl8221b_led_device);
	rtk_rtl8221b_proc_exit();

	RTL8221B_MSG("%s\n", __FUNCTION__);
}

MODULE_LICENSE("GPL");
MODULE_DESCRIPTION("RTL8221B Module");
MODULE_AUTHOR("Realtek.com");

module_init(rtk_rtl8221b_moudle_init);
module_exit(rtk_rtl8221b_module_exit);

