#include <linux/version.h>

#include <linux/kernel.h>
#include <linux/netdevice.h>
#include <linux/etherdevice.h>
#include <linux/ip.h>
#include <linux/in.h>
#include <linux/inetdevice.h>
#include <net/checksum.h>
#include <net/udp.h>
#include <linux/ctype.h>

#include <linux/proc_fs.h>
#include <linux/seq_file.h>


#include <soc/cortina/rtk_dns_trap.h>

#ifdef CONFIG_IPV6
#include <linux/in6.h>
#include <net/if_inet6.h>
#include <net/addrconf.h>
#endif

#define PROC_DOMAIN_NAME "domain_name"
#ifdef CONFIG_CMCC
#define PROC_EXTRA_DOMAIN_NAME "extra_domain_name"
#endif

#define PROC_ENABLE "dns_trap_enable"
#ifdef SUPPORT_TRAP_ALL
#define PROC_TRAP_ALL "dns_trap_all"
#endif
#define PROC_SKB_MARK  "dns_skb_mark"
#define PROC_CONTROLLER_INFO  "controller_info"

extern struct proc_dir_entry *rtk_proc_dir;

int skb_mark;
static int use_controller_info=0;
static unsigned char controller_ip[128]={0};
unsigned char domain_name[80];
#ifdef CONFIG_CMCC
unsigned char extra_domain_name[80];
#endif
unsigned char dns_answer[] = { 0xC0, 0x0C, 0x00, 0x01, 0x00, 0x01, 0x00, 0x00, 0x00, 0x00, 0x00, 0x04 };
#ifdef CONFIG_IPV6
unsigned char dns_answer_v6[] = { 0xC0, 0x0C, 0x00, 0x1C, 0x00, 0x01, 0x00, 0x00, 0x00, 0x00, 0x00, 0x10 };
#endif
int dns_trap_enable = 1;
#ifdef SUPPORT_TRAP_ALL
int dns_trap_all = 1;
#endif

#define DEFAULT_HOSTNAME "wifi.example.com"

#define LANIF		"br0"
#define LAN_ALIAS		"br0:0"    // alias for secondary IP

extern int in4_pton(const char *src, int srclen, u8 *dst, int delim, const char **end);

void str_to_lower(char *s)
{
	if(s!=NULL)
	{
	while (*s != '\0')
		{
	*s = __tolower(*s);
	++s;
	}
	}
}

#ifdef CONFIG_IPV6
extern struct inet6_ifaddr *ipv6_get_ifaddr(struct net * net,const struct in6_addr * addr,struct net_device * dev,int strict);
#define EXTEND_LEN_V6	28
#endif
#define EXTEND_LEN_V4	16

#ifdef CONFIG_IPV6

struct udphdr *ipv6_find_udp_hdr(struct sk_buff *skb,int offset, struct udphdr *udph)
{
	int ret=0;
	unsigned int uhoff=0;
	int offset_bak=0;
	int udp_target=17;

	offset_bak = skb_network_offset(skb);
	skb_set_network_header(skb,offset);

	ret = ipv6_find_hdr(skb, &uhoff, udp_target, NULL, NULL);
	if(ret<0)
	{
		skb_set_network_header(skb,offset_bak);
		return NULL;
	}
	else
	{
		udph = skb_header_pointer(skb, uhoff,sizeof(struct udphdr), udph);
		return udph;
	}
	udph = NULL;
	return NULL;


}
#endif

#ifdef CONFIG_IPV6
int rtk_dns_packet_recap(struct sk_buff *skb,short type,short ip_ver)
#else
int rtk_dns_packet_recap(struct sk_buff *skb,short type)
#endif
{
	struct iphdr *iph;
	struct udphdr *udph;
	struct udphdr udph_in;
	struct net_device *br0_dev;
	struct in_device *br0_in_dev;
	dnsheader_t *dns_pkt;
	//unsigned char mac[ETH_ALEN];
	unsigned int ip;
	unsigned short port;
	unsigned char *ptr = NULL;
	int extend_len;
	int offset=0;
#ifdef CONFIG_IPV6
	struct ipv6hdr *ipv6h;
	struct in6_addr ip6addr;
	struct inet6_dev *idev;
	struct inet6_ifaddr *ifa;
#endif

	if(skb->protocol==htons(ETH_P_8021Q))
		offset+=4;

	if(type == 0x01)
	{
		extend_len = EXTEND_LEN_V4;
	}
#ifdef CONFIG_IPV6
	else if(type == 0x1c)
	{
		extend_len = EXTEND_LEN_V6;
	}
#endif
	else
	{
		DBGP_DNS_TRAP("[%s:%d]Invalid type!\n",__FUNCTION__,__LINE__);
		return -1;
	}

	br0_dev = dev_get_by_name(&init_net,"br0");
	br0_in_dev = in_dev_get(br0_dev);

	DBGP_DNS_TRAP("[%s:%d] br0_in_dev->ifa_list->ifa_address=0x%08x\n",__FUNCTION__,__LINE__,br0_in_dev->ifa_list->ifa_address);
#ifdef CONFIG_IPV6
	if(type == 0x1c)//IPV6 address
	{
		idev = in6_dev_get(br0_dev);
		if(idev != NULL){
			list_for_each_entry(ifa, &idev->addr_list, if_list) {
				in6_dev_put(idev);
				break;
			}
		}
	}
#endif

	if(!br0_dev || !br0_in_dev)
	{
		if(br0_in_dev)
			in_dev_put(br0_in_dev);
		if(br0_dev)
			dev_put(br0_dev);
		return -1;
	}

#ifdef CONFIG_IPV6
	if (ip_ver==1) {
	//ipv6h = ipv6_hdr(skb);
	ipv6h=(struct ipv6hdr *)(skb->data+offset);
	udph  = ipv6_find_udp_hdr(skb,offset,&udph_in);

		if (NULL==udph){
			DBGP_DNS_TRAP("[%s:%d]can't find udp header in ipv6 pkt! \n",__FUNCTION__,__LINE__);
			if(br0_in_dev)
				in_dev_put(br0_in_dev);
			if(br0_dev)
				dev_put(br0_dev);
			return -1;
		}
	} else
#endif
	{
	//iph = ip_hdr(skb);
	iph=(struct iphdr *)(skb->data+offset);
	udph = (void *)iph + iph->ihl*4;
	}

	dns_pkt = (void *)udph + sizeof(struct udphdr);
	ptr = (void *)udph + ntohs(udph->len);
	skb_put(skb,extend_len);
	/* swap mac address */
#if 0
	memcpy(mac, eth_hdr(skb)->h_dest, ETH_ALEN);
	memcpy(eth_hdr(skb)->h_dest, eth_hdr(skb)->h_source, ETH_ALEN);
	memcpy(eth_hdr(skb)->h_source, mac, ETH_ALEN);
#else
	memcpy(eth_hdr(skb)->h_dest, eth_hdr(skb)->h_source, ETH_ALEN);
	memcpy(eth_hdr(skb)->h_source, br0_dev->dev_addr, ETH_ALEN);
#endif
	/*swap ip address */
#ifdef CONFIG_IPV6
	if (ip_ver==1) {
		memcpy(&ip6addr, &ipv6h->saddr, sizeof(ip6addr));
		memcpy(&ipv6h->saddr, &ipv6h->daddr, sizeof(ip6addr));
		memcpy(&ipv6h->daddr, &ip6addr, sizeof(ip6addr));
		ipv6h->payload_len = htons(ntohs(ipv6h->payload_len)+extend_len);
	} else
#endif
	{
	ip = iph->saddr;
	iph->saddr = iph->daddr;
	iph->daddr = ip;
	iph->tot_len = htons(ntohs(iph->tot_len)+extend_len);
	DBGP_DNS_TRAP("[%s]iph->tot_len:%d\n",__FUNCTION__,iph->tot_len);
    }

	/* swap udp port */
	port = udph->source;
	udph->source = udph->dest;
	udph->dest = port;
	udph->len = htons(ntohs(udph->len)+extend_len);
	dns_pkt->u = htons(0x8180);
	dns_pkt->qdcount = htons(1);
	dns_pkt->ancount = htons(1);
	dns_pkt->nscount = htons(0);
	dns_pkt->arcount = htons(0);
	DBGP_DNS_TRAP("[%s]udph->len:%d\n",__FUNCTION__,ntohs(udph->len));
	DBGP_DNS_TRAP("[%s]dns_pkt->u:%x\n",__FUNCTION__,ntohs(dns_pkt->u));
	DBGP_DNS_TRAP("[%s]dns_pkt->qdcount:%x\n",__FUNCTION__,ntohs(dns_pkt->qdcount));
	DBGP_DNS_TRAP("[%s]dns_pkt->ancount:%x\n",__FUNCTION__,ntohs(dns_pkt->ancount));
	DBGP_DNS_TRAP("[%s]dns_pkt->nscount:%x\n",__FUNCTION__,ntohs(dns_pkt->nscount));
	DBGP_DNS_TRAP("[%s]dns_pkt->arcount:%x\n",__FUNCTION__,ntohs(dns_pkt->arcount));
	/* pad Answers */
	if(type == 0x01)
	{
#if 0
		memcpy(ptr, dns_answer, 12);
		memcpy(ptr+12, (unsigned char *)&br0_in_dev->ifa_list->ifa_address, 4);
#else
		__be32	ifa_address=0;
		memcpy(ptr, dns_answer, 12);

		if(use_controller_info)
		{
			in4_pton(controller_ip,strlen(controller_ip),(u8*)&ifa_address,'\0',NULL);
			//printk("\n%s:%d controller_ip=%s ifa_address=%08x\n",__FUNCTION__,__LINE__,controller_ip,ifa_address);
		}
		else
		{
			int exist_br0_ip=0;
			struct in_ifaddr *ifap = NULL;

			//ifa_address = br0_in_dev->ifa_list->ifa_address;
			for (ifap=br0_in_dev->ifa_list; ifap!=NULL; ifap=ifap->ifa_next)
			{
				if (strncmp(ifap->ifa_label, LAN_ALIAS, IFNAMSIZ) == 0) {
					ifa_address = ifap->ifa_address;
					exist_br0_ip=1;
					break;
				}
				else if(strncmp(ifap->ifa_label, LANIF, IFNAMSIZ) == 0){
					ifa_address = ifap->ifa_address;
					exist_br0_ip=1;
				}
			}
			if(0==exist_br0_ip)
			{
				DBGP_DNS_TRAP("[%s:%d]can't find br0 IP! \n",__FUNCTION__,__LINE__);
				if(br0_in_dev)
					in_dev_put(br0_in_dev);
				if(br0_dev)
					dev_put(br0_dev);
				return -1;
			}
		}

		memcpy(ptr+12, (unsigned char *)&ifa_address, 4);

#endif
	}
#ifdef CONFIG_IPV6
	else if(type == 0x1c)
	{
		memcpy(ptr,dns_answer_v6,12);
		if(ifa != NULL)
			memcpy(ptr+12,(unsigned char *)&ifa->addr,16);
		else
			memcpy(ptr+12,0,16);
	}
#endif
	/* ip checksum */
	skb->ip_summed = CHECKSUM_NONE;
	//skb->dns_trap=1;

#ifdef CONFIG_IPV6
	if (ip_ver==0)
#endif
	{
	iph->check = 0;
	iph->check = ip_fast_csum((unsigned char *)iph, iph->ihl);
	}

	/* udp checksum */
	udph->check = 0;
#ifdef CONFIG_IPV6
	if (ip_ver==1) {
	udph->check = csum_ipv6_magic(&ipv6h->saddr, &ipv6h->daddr,
						ntohs(udph->len), IPPROTO_UDP,
						csum_partial((char *)udph,
									 ntohs(udph->len), 0));
	} else
#endif
	{
	udph->check = csum_tcpudp_magic(iph->saddr, iph->daddr,
					ntohs(udph->len), IPPROTO_UDP,
					csum_partial((char *)udph,
					             ntohs(udph->len), 0));
	}
    if(br0_in_dev) {
        in_dev_put(br0_in_dev);
    }
    if(br0_dev) {
        dev_put(br0_dev);
    }

	skb_push(skb, ETH_HLEN);
#ifdef CONFIG_RTK_SKB_MARK2
	skb->mark2=((skb->mark2) | (1<<skb_mark));
#else
	skb->mark=((skb->mark) | (1<<skb_mark));
#endif
	dev_queue_xmit(skb);

	return 0;
}

static short get_domain_name(unsigned char* dns_body,char* domain_name,int body_len)
{
	int offset = 0,token_len = 0;
	char token[64] = {0};
	char domain[128] = {0};
	unsigned char* tmp;
	short type;
	if(!dns_body || !domain_name || body_len <= 0){
		return -1;
	}
	while(body_len > 0){
		memset(token,0,sizeof(token));
		token_len = dns_body[offset];
		if( (token_len > 0) && (token_len<=body_len) ){
			memcpy(token, dns_body + offset + 1, token_len);
			if(!domain[0]){
				memcpy(domain, token, sizeof(token) - 1);
			}
			else{
				strncat(domain, ".", (sizeof(domain) - strlen(domain) - 1));
				strncat(domain, token, (sizeof(domain) - strlen(domain) - 1));

			}
		}
		else
		{
			if (token_len > body_len)
				printk("%s[%d], token_len is %d, body_len is %d\n", __FUNCTION__, __LINE__, token_len, body_len);
			break;
		}
		token_len +=1;
		body_len -= token_len;
		offset += token_len;
	}
	tmp = dns_body + offset + 1;
	type = ntohs((*(unsigned short*)tmp));
	memcpy(domain_name, domain, sizeof(domain) - 1);
	return type;
}
static int is_domain_name_equal(char *domain_name1, char * domain_name2)
{
	char temp1[128];
	char temp2[128];
	if(!domain_name1 || !domain_name2)
	{
		return 0;
	}
	str_to_lower(domain_name1);
	str_to_lower(domain_name2);
	if(!strncmp(domain_name1,"www.",4)){
		strcpy(temp1,domain_name1+4);
	}
	else{
		strcpy(temp1,domain_name1);
	}
	if(!strncmp(domain_name2,"www.",4)){
		strcpy(temp2,domain_name2+4);
	}
	else{
		strcpy(temp2,domain_name2);
	}
	if(strcmp(temp1,temp2))
		return 0;
	else
		return 1;
}

static int is_valid_dns_query_header(dnsheader_t *dns_header)
{
	if(dns_header == NULL)
	{
		return 0;
	}
	DBGP_DNS_TRAP("[%s]qdcount:%d\n",__FUNCTION__,dns_header->qdcount);
	if(dns_header->qdcount < 1)
	{
		return 0;
	}
	if(((dns_header->u & 0x8000)>>15)!= 0)/*QR: query should be 0,answer be 1*/
	{
		DBGP_DNS_TRAP("[%s]QR!=0!\n",__FUNCTION__);
		return 0;
	}
	if(((dns_header->u & 0x7100)>>11) != 0)/*opcode: 0:standard,1:reverse,2:server status*/
	{
		DBGP_DNS_TRAP("[%s]opcode!=0!\n",__FUNCTION__);
		return 0;
	}
	if(((dns_header->u & 0x70)>>4) != 0)/*Z: reserved, should be 0*/
	{
		DBGP_DNS_TRAP("[%s]Z!=0!\n",__FUNCTION__);
		return 0;
	}
	return 1;
}
int rtk_dns_trap_enter(struct sk_buff **pskb)
{
	struct iphdr *iph;
	struct udphdr *udph;
	struct udphdr udph_in;
	unsigned char *body = NULL;
	dnsheader_t *dns_hdr = NULL;
	int len = 0;
	char domain[512] = {0};
	short type;
	int is_dns_pkt=0;
#ifdef CONFIG_IPV6
	struct ipv6hdr *ipv6h;
#endif
	short ip_ver=0; //default is ipv4.
	int offset=0;


	struct sk_buff *skb = *pskb;

	// ipv4 path
	//iph = (struct iphdr *)skb_network_header(skb);
	if(skb->protocol==htons(ETH_P_8021Q))
	{
		offset+=4;
		DBGP_DNS_TRAP("[%s:%d] vlan packet!!!\n",__FUNCTION__,__LINE__);
	}

	iph=(struct iphdr *)(skb->data+offset);

	#ifdef CONFIG_IPV6
	/* check IPv6 header information */
	//ipv6h = (struct ipv6hdr *)skb_network_header(skb);
	ipv6h=(struct ipv6hdr *)(skb->data+offset);
	#endif

	if(iph!=NULL && iph->version == 4){ // skb is ipv4
		ip_ver =0;
		if(iph->protocol==IPPROTO_UDP){
			udph = (void *)iph + iph->ihl*4;
			if (udph!=NULL && udph->dest == htons(53) && ((iph->frag_off & htons(0x3FFF))==0) && (ntohs(iph->tot_len-udph->len)>=20)) {
				DBGP_DNS_TRAP("[%s:%d]DNSV4 packet\n",__FUNCTION__,__LINE__);
				is_dns_pkt =1;
			}
		}
	}
	#ifdef CONFIG_IPV6
	else if(ipv6h!=NULL && ipv6h->version == 6){	// skb is ipv6
		ip_ver =1;
		udph = ipv6_find_udp_hdr(skb,offset,&udph_in);

		if (udph == NULL) // find udp header fail
			return -1;
        else if(ntohs(udph->dest) == 53 && (ntohs(ipv6h->payload_len-udph->len)>=0)) {
	        //DBGP_DNS_TRAP("[%s:%d]DNSV6 packet\n",__FUNCTION__,__LINE__);
			is_dns_pkt =1;
		}
	}
	#endif

    if (is_dns_pkt)
	{
		//DBGP_DNS_TRAP("[%s:%d]DNS packet\n",__FUNCTION__,__LINE__);
		len = ntohs(udph->len) - sizeof(struct udphdr) - sizeof(dnsheader_t) - 4;
		if(len <=1 || len > 63)
		{
			return -1;
		}
		dns_hdr = (dnsheader_t*)((void*)udph + sizeof(struct udphdr));
		if(!is_valid_dns_query_header(dns_hdr))
		{
			return -1;
		}
		body = (void *)udph + sizeof(struct udphdr) + sizeof(dnsheader_t);
		//DBGP_DNS_TRAP("[%s:%d]DNS packet urlis[%s]\n",__FUNCTION__,__LINE__,body);
		type = get_domain_name(body,domain,len);
		if(type != 0x01 && type != 0x1c)
		{
			DBGP_DNS_TRAP("[%s:%d]Invalid type!\n",__FUNCTION__,__LINE__);
			return -1;
		}
		DBGP_DNS_TRAP("[%s:%d]domain_name is %s,type:%x\n",__FUNCTION__,__LINE__,domain,type);

#ifdef SUPPORT_TRAP_ALL
		if (dns_trap_all) {
			#ifdef CONFIG_IPV6
			return rtk_dns_packet_recap(skb,type,ip_ver);
			#else
			return rtk_dns_packet_recap(skb,type);
			#endif
		}
#endif
#ifdef CONFIG_CMCC
		if(is_domain_name_equal(domain, domain_name) || is_domain_name_equal(domain, extra_domain_name)) {
#else
		if(is_domain_name_equal(domain, domain_name)) {
#endif
			DBGP_DNS_TRAP("[%s:%d]%s matched!!! skb->dev->name=%s\n",__FUNCTION__,__LINE__,domain,skb->dev->name);
			#ifdef CONFIG_IPV6
			return rtk_dns_packet_recap(skb,type,ip_ver);
            #else
			return rtk_dns_packet_recap(skb,type);
			#endif
		}
	}

	return -1;
}

#if 0
int is_dns_packet(struct sk_buff *skb)
{
	struct iphdr *iph;
	struct udphdr *udph;
#ifdef CONFIG_IPV6
	struct ipv6hdr *ipv6h;
#endif

	iph = (struct iphdr *)skb_network_header(skb);

#ifdef CONFIG_IPV6
	/* check IPv6 header information */
	ipv6h = (struct ipv6hdr *)skb_network_header(skb);
#endif

	if(iph!=NULL && iph->version == 4){	// skb is ipv4
		if(iph->protocol==IPPROTO_UDP){
		udph = (void *)iph + iph->ihl*4;
			if (udph!=NULL && (udph->dest == htons(53) || udph->source == htons(53))) {
			//skb->is_dns_pkt = 1;
			return 1;
			}
		}
	}
#ifdef CONFIG_IPV6
	else if(ipv6h!=NULL && ipv6h->version == 6){	// skb is ipv6
		udph = ipv6_find_udp_hdr(skb);
		if (udph == NULL) // find udp header fail
			return 0;
        else if (udph->dest == htons(53) || udph->source == htons(53)) {
			//skb->is_dns_pkt = 1;
			return 1;
		}
	}
#endif

	return 0;
}
#endif

static int dnstrap_en_read(struct seq_file *s, void *v)
{
	seq_printf(s,"%d\n",dns_trap_enable);
	return 0;
}
static int dnstrap_en_write(struct file *file, const char *buffer,
		      unsigned long count, void *data)
{
	char tmpbuf[80];

	if (count < 2)
		return -EFAULT;

	if (buffer && !copy_from_user(tmpbuf, buffer, count))  {
		tmpbuf[count] = '\0';
		if (tmpbuf[0] == '0')
			dns_trap_enable = 0;
		else if (tmpbuf[0] == '1')
			dns_trap_enable = 1;
		return count;
	}
	return -EFAULT;
}
int dnstrap_en_proc_open(struct inode *inode, struct file *file)
{
	return(single_open(file, dnstrap_en_read,NULL));
}
int dnstrap_en_proc_write(struct file * file, const char __user * userbuf,
		     size_t count, loff_t * off)
{
	return dnstrap_en_write(file,userbuf,count,off);
}

#if LINUX_VERSION_CODE >= KERNEL_VERSION(5,10,0)
struct proc_ops dnstrap_en_proc_fops= {
        .proc_open           = dnstrap_en_proc_open,
        .proc_write		    = dnstrap_en_proc_write,
        .proc_read           = seq_read,
        .proc_lseek         = seq_lseek,
        .proc_release        = single_release,
};
#else
struct file_operations dnstrap_en_proc_fops= {
        .open           = dnstrap_en_proc_open,
        .write		    = dnstrap_en_proc_write,
        .read           = seq_read,
        .llseek         = seq_lseek,
        .release        = single_release,
};
#endif

#ifdef SUPPORT_TRAP_ALL
static int dnstrap_trap_all_read(struct seq_file *s, void *v)
{
	seq_printf(s,"%d\n",dns_trap_all);
	return 0;
}
static int dnstrap_trap_all_write(struct file *file, const char *buffer,
		      unsigned long count, void *data)
{
	char tmpbuf[80];

	if (count < 2)
		return -EFAULT;

	if (buffer && !copy_from_user(tmpbuf, buffer, count))  {
		tmpbuf[count] = '\0';
		if (tmpbuf[0] == '0')
			dns_trap_all = 0;
		else if (tmpbuf[0] == '1')
			dns_trap_all = 1;
		return count;
	}
	return -EFAULT;
}
int dnstrap_trap_all_proc_open(struct inode *inode, struct file *file)
{
	return(single_open(file, dnstrap_trap_all_read,NULL));
}
int dnstrap_trap_all_proc_write(struct file * file, const char __user * userbuf,
		     size_t count, loff_t * off)
{
	return dnstrap_trap_all_write(file,userbuf,count,off);
}

#if LINUX_VERSION_CODE >= KERNEL_VERSION(5,10,0)
struct proc_ops dnstrap_trap_all_proc_fops= {
        .proc_open           = dnstrap_trap_all_proc_open,
        .proc_write		    = dnstrap_trap_all_proc_write,
        .proc_read           = seq_read,
        .proc_lseek         = seq_lseek,
        .proc_release        = single_release,
};
#else
struct file_operations dnstrap_trap_all_proc_fops= {
        .open           = dnstrap_trap_all_proc_open,
        .write		    = dnstrap_trap_all_proc_write,
        .read           = seq_read,
        .llseek         = seq_lseek,
        .release        = single_release,
};
#endif
#endif /*SUPPORT_TRAP_ALL*/
#ifdef CONFIG_CMCC
static int dnstrap_extra_domain_read(struct seq_file *s, void *v)
{
	seq_printf(s,"%s\n", extra_domain_name);
	return 0;
}
static int dnstrap_extra_domain_write(struct file *file, const char *buffer,
		      unsigned long count, void *data)
{
	if (count < 2)
		return -EFAULT;

	if (buffer && !copy_from_user(extra_domain_name, buffer, 80)) {
		extra_domain_name[count-1] = 0;
		str_to_lower(extra_domain_name);
		return count;
	}

	return -EFAULT;
}

int dnstrap_extra_domain_proc_open(struct inode *inode, struct file *file)
{
	return(single_open(file, dnstrap_extra_domain_read,NULL));
}
int dnstrap_extra_domain_proc_write(struct file * file, const char __user * userbuf,
		     size_t count, loff_t * off)
{
	return dnstrap_extra_domain_write(file,userbuf,count,off);
}

#if LINUX_VERSION_CODE >= KERNEL_VERSION(5,10,0)
struct proc_ops dnstrap_extra_domain_proc_fops= {
		.proc_open 		  = dnstrap_extra_domain_proc_open,
		.proc_write		 = dnstrap_extra_domain_proc_write,
		.proc_read 		  = seq_read,
		.proc_lseek		 = seq_lseek,
		.proc_release		  = single_release,
};
#else
struct file_operations dnstrap_extra_domain_proc_fops= {
		.open			 = dnstrap_extra_domain_proc_open,
		.write 		 = dnstrap_extra_domain_proc_write,
		.read			 = seq_read,
		.llseek		 = seq_lseek,
		.release		 = single_release,
};
#endif

#endif
static int dnstrap_domain_read(struct seq_file *s, void *v)
{
	seq_printf(s,"%s\n", domain_name);
	return 0;
}
static int dnstrap_domain_write(struct file *file, const char *buffer,
		      unsigned long count, void *data)
{
	if (count < 2)
		return -EFAULT;

	if (buffer && !copy_from_user(domain_name, buffer, 80)) {
		domain_name[count-1] = 0;
		str_to_lower(domain_name);
		return count;
	}

	return -EFAULT;
}

int dnstrap_domain_proc_open(struct inode *inode, struct file *file)
{
	return(single_open(file, dnstrap_domain_read,NULL));
}
int dnstrap_domain_proc_write(struct file * file, const char __user * userbuf,
		     size_t count, loff_t * off)
{
	return dnstrap_domain_write(file,userbuf,count,off);
}

#if LINUX_VERSION_CODE >= KERNEL_VERSION(5,10,0)
struct proc_ops dnstrap_domain_proc_fops= {
        .proc_open           = dnstrap_domain_proc_open,
        .proc_write		    = dnstrap_domain_proc_write,
        .proc_read           = seq_read,
        .proc_lseek         = seq_lseek,
        .proc_release        = single_release,
};
#else
struct file_operations dnstrap_domain_proc_fops= {
        .open           = dnstrap_domain_proc_open,
        .write		    = dnstrap_domain_proc_write,
        .read           = seq_read,
        .llseek         = seq_lseek,
        .release        = single_release,
};
#endif

static int dnstrap_skbmark_read(struct seq_file *s, void *v)
{
	seq_printf(s,"%d\n", skb_mark);
	return 0;
}
static int dnstrap_skbmark_write(struct file *file, const char *buffer,
		      unsigned long count, void *data)
{
	char tmpbuf[80];

	if (count < 2)
		return -EFAULT;

	if (buffer && !copy_from_user(tmpbuf, buffer, count))  {
		tmpbuf[count] = '\0';
		sscanf(tmpbuf, "%d", &skb_mark);
		//printk("\n%s:%d skb_mark=%d\n",__FUNCTION__,__LINE__,skb_mark);
		return count;
	}
	return -EFAULT;
}

int dnstrap_skbmark_proc_open(struct inode *inode, struct file *file)
{
	return(single_open(file, dnstrap_skbmark_read,NULL));
}
int dnstrap_skbmark_proc_write(struct file * file, const char __user * userbuf,
		     size_t count, loff_t * off)
{
	return dnstrap_skbmark_write(file,userbuf,count,off);
}

#if LINUX_VERSION_CODE >= KERNEL_VERSION(5,10,0)
struct proc_ops dnstrap_skbmark_proc_fops= {
        .proc_open           = dnstrap_skbmark_proc_open,
        .proc_write		    = dnstrap_skbmark_proc_write,
        .proc_read           = seq_read,
        .proc_lseek         = seq_lseek,
        .proc_release        = single_release,
};
#else
struct file_operations dnstrap_skbmark_proc_fops= {
        .open           = dnstrap_skbmark_proc_open,
        .write		    = dnstrap_skbmark_proc_write,
        .read           = seq_read,
        .llseek         = seq_lseek,
        .release        = single_release,
};
#endif

static int dnstrap_controller_info_read(struct seq_file *s, void *v)
{
	seq_printf(s,"%d %s\n", use_controller_info,controller_ip);
	return 0;
}
static int dnstrap_controller_info_write(struct file *file, const char *buffer,
		      unsigned long count, void *data)
{
	char tmpbuf[80];

	if (count < 2)
		return -EFAULT;

	if (buffer && !copy_from_user(tmpbuf, buffer, count))  {
		tmpbuf[count] = '\0';
		sscanf(tmpbuf, "%d %s", &use_controller_info, controller_ip);
		//printk("\n%s:%d use_controller_info=%d controller_ip=%s\n",__FUNCTION__,__LINE__,use_controller_info,controller_ip);
		return count;
	}
	return -EFAULT;
}

int dnstrap_controller_info_proc_open(struct inode *inode, struct file *file)
{
	return(single_open(file, dnstrap_controller_info_read,NULL));
}
int dnstrap_controller_info_proc_write(struct file * file, const char __user * userbuf,
		     size_t count, loff_t * off)
{
	return dnstrap_controller_info_write(file,userbuf,count,off);
}


#if LINUX_VERSION_CODE >= KERNEL_VERSION(5,10,0)
struct proc_ops dnstrap_controller_info_proc_fops= {
        .proc_open           = dnstrap_controller_info_proc_open,
        .proc_write		    = dnstrap_controller_info_proc_write,
        .proc_read           = seq_read,
        .proc_lseek         = seq_lseek,
        .proc_release        = single_release,
};
#else
struct file_operations dnstrap_controller_info_proc_fops= {
        .open           = dnstrap_controller_info_proc_open,
        .write		    = dnstrap_controller_info_proc_write,
        .read           = seq_read,
        .llseek         = seq_lseek,
        .release        = single_release,
};
#endif

#if defined(CONFIG_PROC_FS)
static void dnstrap_create_proc(void)
{
	if(rtk_proc_dir==NULL)
		rtk_proc_dir=proc_mkdir("driver/realtek", NULL);


	if(rtk_proc_dir)
	{
		proc_create(PROC_DOMAIN_NAME, 0, rtk_proc_dir, &dnstrap_domain_proc_fops);
#ifdef CONFIG_CMCC
		proc_create(PROC_EXTRA_DOMAIN_NAME, 0, rtk_proc_dir, &dnstrap_extra_domain_proc_fops);
#endif
		proc_create(PROC_ENABLE,0,rtk_proc_dir,&dnstrap_en_proc_fops);
#ifdef SUPPORT_TRAP_ALL
		proc_create(PROC_TRAP_ALL,0,rtk_proc_dir,&dnstrap_trap_all_proc_fops);
#endif
		proc_create(PROC_SKB_MARK,0,rtk_proc_dir,&dnstrap_skbmark_proc_fops);
		proc_create(PROC_CONTROLLER_INFO,0,rtk_proc_dir,&dnstrap_controller_info_proc_fops);
	}
}
static void dnstrap_destroy_proc(void)
{
	if(rtk_proc_dir)
	{
		remove_proc_entry(PROC_DOMAIN_NAME, rtk_proc_dir);
#ifdef CONFIG_CMCC
		remove_proc_entry(PROC_EXTRA_DOMAIN_NAME, rtk_proc_dir);
#endif
		remove_proc_entry(PROC_ENABLE, rtk_proc_dir);
#ifdef SUPPORT_TRAP_ALL
		remove_proc_entry(PROC_TRAP_ALL, rtk_proc_dir);
#endif
		remove_proc_entry(PROC_SKB_MARK, rtk_proc_dir);
		remove_proc_entry(PROC_CONTROLLER_INFO, rtk_proc_dir);
	}
}
#endif
int __init rtk_dns_trap_init(void)
{
#if defined(CONFIG_PROC_FS)
	dnstrap_create_proc();
#endif
	memset(domain_name,0,sizeof(domain_name));
	strcpy(domain_name,DEFAULT_HOSTNAME);
	str_to_lower(domain_name);
	return 0;
}

void __exit rtk_dns_trap_exit(void)
{
#if defined(CONFIG_PROC_FS)
	dnstrap_destroy_proc();
#endif
}
