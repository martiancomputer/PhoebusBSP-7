
#define FUCNAMSIZ 32

typedef struct usb_func_s
{
	unsigned char name[FUCNAMSIZ];
}usb_func_t;

const usb_func_t usb_func[] = {
#if IS_ENABLED(CONFIG_USB_RTL8152)
	{"rtl8152_open"},
#endif
#if IS_ENABLED(CONFIG_USB_USBNET)
	{"usbnet_open"},
#endif
};

int usb_idx = 0;

void rtk_fc_wlan_set_usbname(struct net_device *dev)
{
	//unsigned long long wlan_devmask = 0;
	int i = 0, idx = 0;
	size_t usb_func_size = sizeof(usb_func) / sizeof(usb_func_t);
	char funcname[FUCNAMSIZ];
#if defined(CONFIG_ARM64)
	snprintf(funcname, FUCNAMSIZ, "%ps", dev->netdev_ops->ndo_open);
#else
	snprintf(funcname, FUCNAMSIZ, "%pf", dev->netdev_ops->ndo_open);
#endif

	for (idx = 0; idx < usb_func_size; idx++) {
		if (strncmp(usb_func[idx].name, funcname, strlen(usb_func[idx].name)) == 0) {
			for(i = 0; i <= wlanInitMap_size; i++) {
				if (wlanInitMap[i].wlanDevIdx == RTK_FC_WLANx_USB_INTF) {
					memset(wlanInitMap[i].ifname, 0 ,IFNAMSIZ);
					strscpy((char *)wlanInitMap[i].ifname, dev->name, IFNAMSIZ);
					usb_idx = i;
					//wlan_devmask = (1LL << RTK_FC_WLANx_USB_INTF);
					return;
				}
			}
		}
	}
}
