
#include <linux/igmp.h>
#include <net/mld.h>
//#include <uapi/linux/igmp.h>
//#include <linux/if_bridge.h>
#include <br_private.h>

#include <rtk_igmp_snooping.h>
#include <rtk_igmp_hook.h>
#include <rtk_igmp_debug.h>
#include <rtk_igmp_struct.h>
#include <rt_igmp_ext.h>
//#include <rtk_igmp_proc.h>

#if defined(CONFIG_RTL_NEW_IGMP_REPORT_BEHAVIOR)|| defined(CONFIG_RTL_LAN_COMPATIBILITY) || defined (CONFIG_RTL_PER_PORT_CLIENT_NUM)
#include <rtk_igmp_nec_snooping.h>
#endif

#if defined(CONFIG_RTL_NEW_IGMP_REPORT_BEHAVIOR)|| defined(CONFIG_RTL_LAN_COMPATIBILITY) || defined (CONFIG_RTL_PER_PORT_CLIENT_NUM)
extern struct rtl_mCastSnoopingGlobalConfig mCastSnoopingGlobalConfig;
#if defined(CONFIG_RTL_LAN_COMPATIBILITY)
extern int igmpcompatibilityStatus ;
extern uint32   oldVersionHostPresentTimeout;
extern uint32   oldVersionHostPresentTimeoutIpv6;
extern uint32   igmpV2Timer;
extern uint32  igmpV1Timer;
#endif
#if defined(CONFIG_RTL_PER_PORT_CLIENT_NUM)
extern uint8 enableMaxClientNumCheck ;
extern uint8 enableIpv6MaxClientNumCheck;
extern uint32 maxClientNumPerPort[2] ;
extern uint32  currentClientNum[2][IGMP_MAX_DEV_NUM];
#endif
#endif

#if defined(CONFIG_RTK_IGMP_MLD_REDUCETABLE_SIZE)

#define IGMP_MAX_FLOW_COUNT		512
#define IGMP_MAX_GROUP_COUNT	256	/* default max group entry count **/
#define IGMP_MAX_SOURCE_COUNT	256	/* default max source entry count*/
#define IGMP_MAX_CLIENT_COUNT	512

#define IGMP_MAX_USER_REV2PS_GROUP_COUNT 64
#define IGMP_MAX_USER_GROUP_WEIGHT	512

#else

#define IGMP_MAX_FLOW_COUNT		512
#define IGMP_MAX_GROUP_COUNT	512	/* default max group entry count **/
#define IGMP_MAX_SOURCE_COUNT	512	/* default max source entry count*/
#define IGMP_MAX_CLIENT_COUNT	512

#define IGMP_MAX_USER_REV2PS_GROUP_COUNT 256
#define IGMP_MAX_USER_GROUP_WEIGHT	1024

#endif

#define IGMP_HASH_TBL_SIZE 	(1<<IGMP_HASH_TBL_BIT)
#define IGMP_HASH_TBL_BIT 	5

#define IGMP_MCAST_FLOW_EXPIRE_TIME 	140
#define SYS_EXPIRED_NORMAL 	0


rtk_igmp_hwCbEvt_t hwCb;
rtk_igmp_globalConfdb_t gloConf;

//hardware dependency callback event


struct rtk_igmp_multicastModule rtk_igmp_mCastModuleArray[IGMP_MAX_BR_MODULE_NUM];
rtk_igmp_multicastDeviceInfo_t  rtk_igmp_wanDevInfo[IGMP_MAX_WAN_DEV_NUM];
rtk_igmp_multicastDeviceInfo_t  rtk_igmp_lanDevInfo[IGMP_MAX_LAN_DEV_NUM];

static struct rtk_igmp_groupEntry *rtk_igmp_groupEntryPool=NULL;
static struct rtk_igmp_clientEntry *rtk_igmp_clientEntryPool=NULL;
static struct rtk_igmp_sourceEntry *rtk_igmp_sourceEntryPool=NULL;
static struct rtk_igmp_mcastFlowEntry *rtk_igmp_mcastFlowEntryPool=NULL;

struct rtk_igmp_clientEntry clientEntryPool[IGMP_MAX_CLIENT_COUNT];
struct rtk_igmp_groupEntry groupEntryPool[IGMP_MAX_GROUP_COUNT];
struct rtk_igmp_sourceEntry sourceEntryPool[IGMP_MAX_SOURCE_COUNT];
struct rtk_igmp_mcastFlowEntry mcastFlowEntryPool[IGMP_MAX_FLOW_COUNT];

rtk_igmp_whiteListEntry_t whiteListHeadEntry;
#define whiteListHead whiteListHeadEntry.entry  /* point to rtk_igmp_whiteListEntry_t*/

rtk_igmp_blackListEntry_t blackListHeadEntry;
#define blackListHead blackListHeadEntry.entry  /* point to rtk_igmp_blackListEntry_t*/

/*rtk_igmp_blackListEntry_t blackList[IGMP_MAX_BLACK_COUNT];*/
//struct list_head blackListFreeListHead;

rtk_igmp_userResvGroup_t gorup2Ps[IGMP_MAX_USER_REV2PS_GROUP_COUNT]; //user define reserved group to protocol-stack table
struct list_head gorup2PsFreeListHead;
struct list_head gorup2PsListHead;

rtk_igmp_groupWeight_t groupWeight[IGMP_MAX_USER_GROUP_WEIGHT];
struct list_head groupWeightFreeListHead;
struct list_head groupWeightListHead;



static uint8 igmpSnoopingCounterVer_1=0; ////: add
static uint8 igmpSnoopingCounterVer_2=0;
static uint8 igmpSnoopingCounterVer_3=0;
static uint8 MLDCounterVer_1=0;
static uint8 MLDCounterVer_2=0;

/*the system up time*/
#if defined(CONFIG_RTL_NEW_IGMP_REPORT_BEHAVIOR)|| defined(CONFIG_RTL_LAN_COMPATIBILITY) || defined (CONFIG_RTL_PER_PORT_CLIENT_NUM)
uint32 rtk_igmp_startTime;
uint32 rtk_igmp_sysUpSeconds;
#else
static uint32 rtk_igmp_startTime;
static uint32 rtk_igmp_sysUpSeconds;
#endif 
//static unsigned long int last_query_jiffies;	/*record the system jiffie of last query send*/

struct timer_list igmpSysTimer;	/*igmp timer*/
struct timer_list mCastQuerytimer;	/*igmp query timer*/
struct timer_list mCastForceReporttimer;	/*igmp mld force response timer*/
static unsigned long lastjiffies=0;
static uint32 lastTimer=0; //start for 0 sec


atomic_t groupIncIdx=ATOMIC_INIT(1);


/*******************************internal function declaration*****************************/
/**************************
	resource managment
**************************/

static  struct rtk_igmp_groupEntry* rtk_igmp_initGroupEntryPool(uint32 poolSize);
static  struct rtk_igmp_groupEntry* rtk_igmp_allocateGroupEntry(void);
static  void rtk_igmp_freeGroupEntry(struct rtk_igmp_groupEntry* groupEntryPtr) ;


static  struct rtk_igmp_clientEntry* rtk_igmp_initClientEntryPool(uint32 poolSize);
static  struct rtk_igmp_clientEntry* rtk_igmp_allocateClientEntry(uint32 *clientAddr,uint8 *clientMac,int32 ifidx,rtk_igmp_pktHdr_t *pPkthdr);
static  void rtk_igmp_freeClientEntry(struct rtk_igmp_clientEntry* clientEntryPtr) ;

static  struct rtk_igmp_sourceEntry* rtk_igmp_initSourceEntryPool(uint32 poolSize);
static  struct rtk_igmp_sourceEntry* rtk_igmp_allocateSourceEntry(void);
static  void rtk_igmp_freeSourceEntry(struct rtk_igmp_sourceEntry* sourceEntryPtr) ;

static  struct rtk_igmp_mcastFlowEntry* rtk_igmp_initMcastFlowEntryPool(uint32 poolSize);
static  struct rtk_igmp_mcastFlowEntry* rtk_igmp_allocateMcastFlowEntry(void);
static  void rtk_igmp_freeMcastFlowEntry(struct rtk_igmp_mcastFlowEntry* mcastFlowEntry);

/**********************************Structure Maintenance*************************/

//Group
struct rtk_igmp_groupEntry* rtk_igmp_searchGroupEntry(int32 moduleIndex,uint32 ipVersion,uint32 *multicastAddr, uint16 vlanId, uint16 svlanId);
static void rtk_igmp_linkGroupEntry(struct rtk_igmp_groupEntry* entryNode ,  struct rtk_igmp_groupEntry ** hashTable);
static void rtk_igmp_unlinkGroupEntry(struct rtk_igmp_groupEntry* entryNode,  struct rtk_igmp_groupEntry ** hashTable);
static void rtk_igmp_clearGroupEntry(struct rtk_igmp_groupEntry* groupEntryPtr);
static void rtk_igmp_deleteGroupEntry(int32 moduleIndex ,struct rtk_igmp_groupEntry* groupEntry,struct rtk_igmp_groupEntry ** hashTable);

//Client
static struct rtk_igmp_clientEntry* rtk_igmp_searchClientEntry(uint32 ipVersion,struct rtk_igmp_groupEntry* groupEntry, uint32 inIfidx, uint32 *clientAddr,rtk_igmp_pktHdr_t *pPkthdr);
static void rtk_igmp_linkClientEntry(struct rtk_igmp_groupEntry *groupEntry, struct rtk_igmp_clientEntry* clientEntry);
static void rtk_igmp_unlinkClientEntry(struct rtk_igmp_groupEntry *groupEntry, struct rtk_igmp_clientEntry* clientEntry);
static void rtk_igmp_clearClientEntry(struct rtk_igmp_clientEntry* clientEntryPtr);
static void rtk_igmp_deleteClientEntry(struct rtk_igmp_groupEntry * groupEntry, struct rtk_igmp_clientEntry * clientEntry);
static void rtk_igmp_deleteClientList(struct rtk_igmp_groupEntry* groupEntry);
static int32 rtk_igmp_getClientFwdIfIdx(struct rtk_igmp_clientEntry * clientEntry, uint32 sysTime);

//Client-Source
static struct rtk_igmp_sourceEntry* rtk_igmp_searchSourceEntry(uint32 ipVersion, uint32 *sourceAddr, struct rtk_igmp_clientEntry *clientEntry);
static void rtk_igmp_linkSourceEntry(struct rtk_igmp_clientEntry *clientEntry,  struct rtk_igmp_sourceEntry* entryNode);
static void rtk_igmp_unlinkSourceEntry(struct rtk_igmp_clientEntry *clientEntry, struct rtk_igmp_sourceEntry* entryNode);
static void rtk_igmp_clearSourceEntry(struct rtk_igmp_sourceEntry* sourceEntryPtr);
static void rtk_igmp_deleteSourceEntry(struct rtk_igmp_clientEntry *clientEntry, struct rtk_igmp_sourceEntry* sourceEntry);
static void rtk_igmp_deleteSourceList(struct rtk_igmp_clientEntry* clientEntry);

//Flow
static struct rtk_igmp_mcastFlowEntry* rtk_igmp_searchMcastFlowEntry(int32 moduleIndex, uint32 ipVersion, uint32 *serverAddr,uint32 *groupAddr,int32 sport,int32 dport,int16 ingressCvid,int16 ingressSvid,int16 l4proto);
static void  rtk_igmp_linkMcastFlowEntry(struct rtk_igmp_mcastFlowEntry* mcastFlowEntry ,  struct rtk_igmp_mcastFlowEntry ** hashTable);
static void rtk_igmp_unlinkMcastFlowEntry(struct rtk_igmp_mcastFlowEntry* mcastFlowEntry,  struct rtk_igmp_mcastFlowEntry ** hashTable);
static void rtk_igmp_clearMcastFlowEntry(struct rtk_igmp_mcastFlowEntry* mcastFlowEntry);
static void rtk_igmp_deleteMcastFlowEntry( struct rtk_igmp_mcastFlowEntry* mcastFlowEntry, struct rtk_igmp_mcastFlowEntry ** hashTable);
static void rtk_igmp_invalidateMCastGroupFlow(int32 moduleIndex,uint32 ipVersion, uint32 *groupAddr);
static  struct rtk_igmp_mcastFlowEntry* rtk_igmp_initMcastFlowEntryPool(uint32 poolSize);
static int32 rtk_igmp_flow_reflush_update(int32 moduleIndex,uint32* groupAddr);
static int _rtk_igmp_getMulticastDataFwdInfo(int32 moduleIndex,uint32 searchCacheFlowFirst, struct rtk_igmp_multicastDataInfo *multicastDataInfo, struct rtk_igmp_multicastFwdInfo *multicastFwdInfo);
int rtk_igmp_agingMcastFlow(int32 moduleIndex);
static int rtk_igmp_deleteUnknownFloodMacstFlow(int32 moduleIndex);
void rtk_igmp_deleteFloodMcastFlowEntry( struct rtk_igmp_mcastFlowEntry* mcastFlowEntry, struct rtk_igmp_mcastFlowEntry ** hashTable);


//Timer
static void rtk_igmp_multicastSysTimerDestroy(void);
void rtk_igmp_multicastSysTimerInit(void);


rtk_igmp_nf_ret_e_t rtk_igmp_group_limitCheck(rtk_igmp_pktHdr_t *pPkthdr,uint32 ipVersion, uint32 *groupAddr , uint32 *clientAddr ,uint32 *mcSourceAddr,uint8 *saMac,uint32 vlanId ,uint32 svlanId ,uint32 portNum,rtk_igmp_nf_ret_e_t overLimitAction);
static int rtk_igmp_netdev_event(struct notifier_block *this, unsigned long event, void *ptr);
rtk_igmp_multicastDeviceInfo_t* _rtk_igmp_devIfidx_to_devInfo(int32 devIfindex);
struct rtk_igmp_multicastModule* _rtk_igmp_devIfidx_to_BrDevInfo(int32 brIfindex);
int32 rtk_igmp_clearDataBaseByDev(int32 devIdx);

//white list
int rtk_igmp_whiteEntry_free(rtk_igmp_whiteListEntry_t* WhilteEntry);
int rtk_igmp_whiteEntry_del(rtk_igmp_whiteListEntry_t* WhilteEntry);

int rtk_igmp_blackEntry_free(rtk_igmp_blackListEntry_t* blackEntry);

unsigned int _rtk_igmp_checkGroupWeightTbl(uint32 ifIdx,uint32 ipVersion,uint32 *_groupIp);


static inline int
iprange_ipv6_lt(const uint32 *a, const uint32 *b)
{
	unsigned int i;

	for (i = 0; i < 4; ++i) {
		if (*(a+i) != *(b+i))
			return ntohl(*(a+i)) < ntohl(*(b+i));
	}

	return 0;
}


unsigned int igmpTotalAlloc=0;
void* rtk_igmp_kmalloc(size_t size)
{
	void *p=NULL;
	p=kmalloc(size, GFP_ATOMIC |__GFP_ZERO);
	if(p==NULL)
		IGMP_WARNING("memory allocate fail size %d", (int)size);
	else
		igmpTotalAlloc+=size;
	return p;
}
int rtk_igmp_kfree(const void *ptr,size_t size)
{
       kfree(ptr);
	   igmpTotalAlloc-=size;
       return SUCCESS;
}



#define RE_INIT

//flush Dev multicast data base
//inidx fail to flush all
int rtk_igmp_flush_Br_mdb(int brifidx ,int isIpv6)
{
	int32 moduleIndex,hashIndex;
	struct rtk_igmp_groupEntry *groupEntryPtr;
	struct rtk_igmp_groupEntry *tmp_groupEntryPtr;
	uint32 gip[4]={0};

	for( moduleIndex=0 ; moduleIndex< IGMP_MAX_BR_MODULE_NUM; moduleIndex++)
	{

		if(brifidx!=FAIL && rtk_igmp_mCastModuleArray[moduleIndex].brDevInfo.ifindex!=brifidx)
			continue;

		if(rtk_igmp_mCastModuleArray[moduleIndex].validBit==0)
			continue;

		if(isIpv6==0)
		{
			/* IPv4 */
			for (hashIndex=0;hashIndex<IGMP_HASH_TBL_SIZE;hashIndex++)
			{
				groupEntryPtr=rtk_igmp_mCastModuleArray[moduleIndex].rtk_igmp_ipv4HashTable[hashIndex];
				while (groupEntryPtr!=NULL)
				{
					tmp_groupEntryPtr=groupEntryPtr->next;
					rtk_igmp_deleteGroupEntry(moduleIndex,groupEntryPtr,rtk_igmp_mCastModuleArray[moduleIndex].rtk_igmp_ipv4HashTable);
					groupEntryPtr=tmp_groupEntryPtr;
				}

			}
		}
		else
		{
			/* IPv6 */
			for (hashIndex=0;hashIndex<IGMP_HASH_TBL_SIZE;hashIndex++)
			{
				groupEntryPtr=rtk_igmp_mCastModuleArray[moduleIndex].rtk_igmp_ipv6HashTable[hashIndex];
				while (groupEntryPtr!=NULL)
				{
					tmp_groupEntryPtr=groupEntryPtr->next;
					rtk_igmp_deleteGroupEntry(moduleIndex,groupEntryPtr,rtk_igmp_mCastModuleArray[moduleIndex].rtk_igmp_ipv6HashTable);
					groupEntryPtr=tmp_groupEntryPtr;
				}

			}
		}
		//clear all flow by group ip zero
		rtk_igmp_invalidateMCastGroupFlow(moduleIndex,isIpv6?IP_VERSION6:IP_VERSION4, gip);
	}


	return SUCCESS;
}


int rtk_igmp_reset_devConfig(int ifidx,int isIpv6)
{

	rtk_igmp_multicastDeviceInfo_t* devInfo = _rtk_igmp_devIfidx_to_devInfo(ifidx);
	rtk_igmp_idxEntry_t *IdxEntry=NULL;
	rtk_igmp_idxEntry_t *IdxEntry_tmp=NULL;

	if(devInfo)
	{
		if(isIpv6)
		{
			list_for_each_entry_safe(IdxEntry,IdxEntry_tmp,&devInfo->devConf.igmp6.white,entry)
			{
				rtk_igmp_whiteListDel_byIdx(devInfo->ifindex,IdxEntry->entryIdx);
			}

			list_for_each_entry_safe(IdxEntry,IdxEntry_tmp,&devInfo->devConf.igmp6.white,entry)
			{
				rtk_igmp_blackListDel_byIdx(devInfo->ifindex,IdxEntry->entryIdx);
			}			
			memset(&devInfo->devConf.igmp6,0,sizeof(devInfo->devConf.igmp6));
			devInfo->devConf.igmp6.igmp6McMemberAgingInterval = IGMP6_MEMBER_AGING;
			devInfo->devConf.igmp6.igmp6McLastMemberAgingInterval = IGMP6_LAST_MEMBER_AGING;
			INIT_LIST_HEAD(&devInfo->devConf.igmp6.white);
			INIT_LIST_HEAD(&devInfo->devConf.igmp6.black);

		}
		else
		{
			list_for_each_entry_safe(IdxEntry,IdxEntry_tmp,&devInfo->devConf.igmp.white,entry)
			{
				rtk_igmp_whiteListDel_byIdx(devInfo->ifindex,IdxEntry->entryIdx);
			}
			list_for_each_entry_safe(IdxEntry,IdxEntry_tmp,&devInfo->devConf.igmp.white,entry)
			{
				rtk_igmp_blackListDel_byIdx(devInfo->ifindex,IdxEntry->entryIdx);
			}				
			memset(&devInfo->devConf.igmp,0,sizeof(devInfo->devConf.igmp));
			devInfo->devConf.igmp.igmpMcMemberAgingInterval= IGMP_MEMBER_AGING;
			devInfo->devConf.igmp.igmpMcLastMemberAgingInterval= IGMP_LAST_MEMBER_AGING;
			INIT_LIST_HEAD(&devInfo->devConf.igmp.white);
			INIT_LIST_HEAD(&devInfo->devConf.igmp.black);
		}
	}


	return SUCCESS;
}



#define WHITE_LIST
atomic_t whiteLisIcrIdx=ATOMIC_INIT(0);

rtk_igmp_whiteListEntry_t * rtk_igmp_whiteEntry_malloc(void)
{

	rtk_igmp_whiteListEntry_t *pWhite_entry;
	pWhite_entry=rtk_igmp_kmalloc(sizeof(rtk_igmp_whiteListEntry_t));
	if(pWhite_entry)
	{
		memset(pWhite_entry,0,sizeof(rtk_igmp_whiteListEntry_t));
		INIT_LIST_HEAD(&pWhite_entry->entry);
		list_add_tail(&pWhite_entry->entry, &whiteListHead);
		pWhite_entry->whiteListIdx=atomic_read(&whiteLisIcrIdx);
		atomic_inc(&whiteLisIcrIdx);
	}

	return pWhite_entry;
	
}



int rtk_igmp_whiteEntry_free(rtk_igmp_whiteListEntry_t* whilteEntry)
{

	list_del_init(&whilteEntry->entry);
	rtk_igmp_kfree(whilteEntry,sizeof(rtk_igmp_whiteListEntry_t));
	return (SUCCESS);
}

int rtk_igmp_whiteEntry_del(rtk_igmp_whiteListEntry_t* whilteEntry)
{
	if(whilteEntry)
	{
		whilteEntry->whiteRefCnt--;
		if(whilteEntry->whiteRefCnt==0)
			rtk_igmp_whiteEntry_free(whilteEntry);
	}

	return (SUCCESS);
}



int rtk_igmp_whiteEntry_flashInit(void)
{
	memset(&whiteListHeadEntry,0,sizeof(whiteListHeadEntry));
	INIT_LIST_HEAD(&whiteListHead);
	return (SUCCESS);
}

rtk_igmp_idxEntry_t * rtk_igmp_idxEntry_malloc(void)
{

	rtk_igmp_idxEntry_t *pIdx_entry;
	pIdx_entry=rtk_igmp_kmalloc(sizeof(rtk_igmp_idxEntry_t));
	if(pIdx_entry)
	{
		memset(pIdx_entry,0,sizeof(rtk_igmp_idxEntry_t));
		INIT_LIST_HEAD(&pIdx_entry->entry);
	}

	return pIdx_entry;
	
}


int rtk_igmp_idxEntry_free(rtk_igmp_idxEntry_t* pIdx_entry)
{

	list_del_init(&pIdx_entry->entry);
	rtk_igmp_kfree(pIdx_entry,sizeof(rtk_igmp_idxEntry_t));
	return (SUCCESS);
}

/* insert befort this entry*/
/*copy from  __list_add */
void __igmp_list_add(struct list_head *new,
                  struct list_head *prev,
                  struct list_head *next)
{
    next->prev = new;
    new->next = next;
    new->prev = prev;
    prev->next = new;
}



int rtk_igmp_whiteListAdd( rt_igmpHook_whiteList_t *patten,int32 ifidx , int32 *index)
{

	rtk_igmp_multicastDeviceInfo_t *devInfo =_rtk_igmp_devIfidx_to_devInfo(ifidx);
	rtk_igmp_whiteListEntry_t *white_entry =NULL;
	rtk_igmp_whiteListEntry_t *white_entry_tmp =NULL;
	struct list_head *p_insertBefortThisEntry=NULL;
	struct list_head *p_devSearchListHdr=NULL;
	rtk_igmp_idxEntry_t *idxEntry=NULL;
	rtk_igmp_idxEntry_t *idxEntry_tmp=NULL;


	if(devInfo==NULL)
		return FAIL;

	if(patten->isIpv6==0 )
	{
		//Error checking
		if((patten->wlSipEnd[0]>0) &&((patten->wlSipEnd[0] < patten->wlSipStart[0]) ))
			return FAIL;
		if((patten->wlGroupIpEnd[0]>0) &&((patten->wlGroupIpEnd[0] < patten->wlGroupIpStart[0]) ))
			return FAIL;
		if((patten->wlMcSipEnd[0]>0) &&((patten->wlMcSipEnd[0] < patten->wlMcSip[0])))
			return FAIL;
	}


	//check whiteListHead same entry
	list_for_each_entry(white_entry_tmp,&whiteListHead,entry)
	{
		if(memcmp(&white_entry_tmp->patten,patten,sizeof(*patten))==0)
		{
			white_entry=white_entry_tmp;
			break;
		}
	}

	if(white_entry==NULL)
	{
		white_entry=rtk_igmp_whiteEntry_malloc();
		memcpy(&white_entry->patten,patten,sizeof(*patten));
		
		if(white_entry==NULL)
			{IGMP_CTRL("alloc fail");return FAIL;}
		idxEntry=rtk_igmp_idxEntry_malloc();
		if(idxEntry==NULL)
		{
			rtk_igmp_whiteEntry_free(white_entry);
			{IGMP_CTRL("alloc fail");return FAIL;}
		}
	}


	if(patten->isIpv6)
		p_devSearchListHdr=&devInfo->devConf.igmp6.white;
	else
		p_devSearchListHdr=&devInfo->devConf.igmp.white;


	list_for_each_entry(idxEntry_tmp,p_devSearchListHdr,entry)
	{
		if(idxEntry_tmp->entryIdx==white_entry->whiteListIdx)
		{
			*index=idxEntry_tmp->entryIdx;
			if(idxEntry)
				rtk_igmp_idxEntry_free(idxEntry);
			IGMP_CTRL("hit same entry");
			return SUCCESS;
		}
		else if(idxEntry_tmp->entryIdx > white_entry->whiteListIdx)
		{
			IGMP_CTRL("insert whiteListIdx=%d  before  entryIdx=%d ",white_entry->whiteListIdx,idxEntry_tmp->entryIdx);
			p_insertBefortThisEntry = &idxEntry_tmp->entry;
			break;
		}
		if(idxEntry_tmp->entry.next == p_devSearchListHdr)
		{
			IGMP_CTRL("List insert before Head");
			p_insertBefortThisEntry=p_devSearchListHdr;	
			break;
		}
	}

	//first entry in list
	if(p_insertBefortThisEntry==NULL)
	{
		IGMP_CTRL("List First Entry");
		p_insertBefortThisEntry=p_devSearchListHdr;
	}

	if(idxEntry==NULL)
	{
		idxEntry=rtk_igmp_idxEntry_malloc();
		if(idxEntry==NULL)
			{IGMP_CTRL("alloc fail");return FAIL;}
	}
	idxEntry->entryIdx=white_entry->whiteListIdx;
	__igmp_list_add(&idxEntry->entry,p_insertBefortThisEntry->prev,p_insertBefortThisEntry);

	white_entry->whiteRefCnt++;
	*index=white_entry->whiteListIdx;
	return SUCCESS;


	
}


int rtk_igmp_whiteListDel_byIdx(int32 ifidx,int32 index)
{
	rtk_igmp_idxEntry_t *idxEntry=NULL;
	uint32 hit=0;
	rtk_igmp_idxEntry_t *idxEntry_tmp=NULL;
	rtk_igmp_multicastDeviceInfo_t *devInfo =_rtk_igmp_devIfidx_to_devInfo(ifidx);
	rtk_igmp_whiteListEntry_t *white_entry =NULL;
	rtk_igmp_whiteListEntry_t *white_entry_tmp =NULL;

	if(devInfo==NULL)
	{
		IGMP_CTRL("whiteList ifidx=%d Del Idx[%d]  devInfo not found",ifidx,index);
		return FAIL;
	}

	list_for_each_entry_safe(idxEntry,idxEntry_tmp,&devInfo->devConf.igmp.white,entry)
	{	
		if(idxEntry->entryIdx==index)
		{
			hit=1;
			rtk_igmp_idxEntry_free(idxEntry);
			break;
		}	
	}
	if(hit==0)
	{
		list_for_each_entry_safe(idxEntry,idxEntry_tmp,&devInfo->devConf.igmp6.white,entry)
		{
			if(idxEntry->entryIdx==index)
			{
				hit=1;
				rtk_igmp_idxEntry_free(idxEntry);
				break;
			}
		}
	}

	if(!hit)
	{
		IGMP_CTRL("whiteList ifidx=%d Del Idx[%d]  DEL FAIL NOT FOUND",ifidx,index);
		return FAIL;
	}

	//check whiteListHead free
	list_for_each_entry_safe(white_entry,white_entry_tmp,&whiteListHead,entry)
	{
		if(white_entry->whiteListIdx == index)
		{
			hit=1;
			IGMP_CTRL("whiteList ifidx=%d Del Idx[%d]",ifidx,index);
			rtk_igmp_whiteEntry_del(white_entry);
			break;
		}
	}

	
	return SUCCESS;


}


int rtk_igmp_whiteListDel(rt_igmpHook_whiteList_t *patten,int32 ifidx,int32 index)
{

	if(patten==NULL)
	{
		return rtk_igmp_whiteListDel_byIdx(ifidx,index);
	}
	else
	{
		rtk_igmp_multicastDeviceInfo_t *devInfo =_rtk_igmp_devIfidx_to_devInfo(ifidx);
		struct list_head *p_devSearchListHdr=NULL;
		rtk_igmp_whiteListEntry_t *white_entry =NULL;
		rtk_igmp_whiteListEntry_t *white_entry_tmp =NULL;
		rtk_igmp_idxEntry_t *whiteIdx;
		rtk_igmp_idxEntry_t *whiteIdx_tmp;
		int32 ipversionHit=0,gipHit=0,sipHit=0,smacHit=0,mcsipHit=0;

		if(devInfo==NULL)
			return FAIL;

		if(patten->isIpv6)
			p_devSearchListHdr=&devInfo->devConf.igmp6.white;
		else
			p_devSearchListHdr=&devInfo->devConf.igmp.white;

		white_entry= &whiteListHeadEntry;

		list_for_each_entry_safe(whiteIdx,whiteIdx_tmp,p_devSearchListHdr,entry)
		{
			list_for_each_entry_continue(white_entry,&whiteListHead,entry)
			{
				white_entry_tmp= list_prev_entry(white_entry, entry);
				if(whiteIdx->entryIdx!=white_entry->whiteListIdx)
					continue;

				ipversionHit=0,gipHit=0,sipHit=0,smacHit=0,mcsipHit=0;

				if(patten->isIpv6 == white_entry->patten.isIpv6)
					ipversionHit=1;
				if(patten->gipChk)
				{
					if(white_entry->patten.gipChk &&memcmp(&white_entry->patten.wlGroupIpStart,&patten->wlGroupIpStart,sizeof(patten->wlGroupIpStart))==0 && memcmp(&white_entry->patten.wlGroupIpEnd,&patten->wlGroupIpEnd,sizeof(patten->wlGroupIpEnd))==0)
						gipHit=1;
				}
				else
					gipHit=1;

				if(patten->sipChk)
				{
					if(white_entry->patten.sipChk &&memcmp(&white_entry->patten.wlSipStart,&patten->wlSipStart,sizeof(patten->wlSipStart))==0 && memcmp(&white_entry->patten.wlSipEnd,&patten->wlSipEnd,sizeof(patten->wlSipEnd))==0)
						sipHit=1;
				}
				else
					sipHit=1;

				if(patten->mcSipChk)
				{
					if(white_entry->patten.mcSipChk &&memcmp(&white_entry->patten.wlMcSip,&patten->wlMcSip,sizeof(patten->wlMcSip))==0 && memcmp(&white_entry->patten.wlMcSipEnd,&patten->wlMcSipEnd,sizeof(patten->wlMcSipEnd))==0)
						mcsipHit=1;
				}
				else
					mcsipHit=1;

				if(patten->smacChk)
				{
					if(white_entry->patten.smacChk &&memcmp(&white_entry->patten.smac,&patten->smac,sizeof(patten->smac))==0 && memcmp(&white_entry->patten.smacMask,&patten->smacMask,sizeof(patten->smacMask))==0)
						smacHit=1;
				}
				else
					smacHit=1;

				if(ipversionHit && gipHit && sipHit && smacHit && mcsipHit)
				{
					IGMP_CTRL("Delete whiteList[%d] on %s",white_entry->whiteListIdx,devInfo->devName);
					rtk_igmp_idxEntry_free(whiteIdx);
					rtk_igmp_whiteEntry_del(white_entry);
					//point to list_prev_entry and break;
					white_entry = white_entry_tmp;
					break;
				}

				break;
			}
		}
		

		return SUCCESS;
	}
}


int rtk_igmp_filterWhiteListCheck(rtk_igmp_multicastDeviceInfo_t *devInfo,uint32 ipVersion,uint32 is_igmpv3_mldv2, uint32 *groupAddr , uint32 *clientAddr,uint32 *mcSourceAddr,uint8 *saMac)
{

	int ret=FAIL;
	int whiteListEnable=0;
	int smacHit=0,sipHit=0,gipHit=0,mcSipHit=0;
	rtk_igmp_whiteListEntry_t *white_entry=NULL;
	rtk_igmp_idxEntry_t *idxEntry=NULL;

	unsigned char _smac[6]={0};
	unsigned char *p_smac=NULL;

	if(saMac && memcmp(_smac,saMac,sizeof(_smac))==0)
	{
		//saMac all zero , means fc not assign sa value set to a null
		p_smac=NULL;
	}
	else
		p_smac=saMac;

	white_entry= &whiteListHeadEntry;

	if(ipVersion==IP_VERSION4)
	{
		list_for_each_entry(idxEntry,&devInfo->devConf.igmp.white,entry)
		{
			list_for_each_entry_continue(white_entry,&whiteListHead,entry)
			{

				//not this entry continue
				if(white_entry->whiteListIdx !=idxEntry->entryIdx)
					continue;
				
				smacHit=0,sipHit=0,gipHit=0,mcSipHit=0; 
				whiteListEnable=1;
				IGMP_CTRL("check whiteList[%d]",idxEntry->entryIdx);
				if(white_entry->patten.smacChk)
				{
					if(p_smac==NULL)//ingore this rule because we don't have this info
						break;
					IGMP_CTRL("check smac");
					if(   p_smac && 
						((white_entry->patten.smac[0]&white_entry->patten.smacMask[0])== (p_smac[0]&white_entry->patten.smacMask[0])) &&
						((white_entry->patten.smac[1]&white_entry->patten.smacMask[1])== (p_smac[1]&white_entry->patten.smacMask[1])) &&
						((white_entry->patten.smac[2]&white_entry->patten.smacMask[2])== (p_smac[2]&white_entry->patten.smacMask[2])) &&
						((white_entry->patten.smac[3]&white_entry->patten.smacMask[3])== (p_smac[3]&white_entry->patten.smacMask[3])) &&
						((white_entry->patten.smac[4]&white_entry->patten.smacMask[4])== (p_smac[4]&white_entry->patten.smacMask[4])) &&
						((white_entry->patten.smac[5]&white_entry->patten.smacMask[5])== (p_smac[5]&white_entry->patten.smacMask[5])) )
					{
						smacHit=1;
					}
				}
				else
					smacHit=1;


				if(white_entry->patten.gipChk)
				{
					if(groupAddr==NULL)//ingore this rule because we don't have this info
						break;
					
					IGMP_CTRL("check group");
					if(groupAddr && (white_entry->patten.wlGroupIpStart[0] <= groupAddr[0])  &&  (groupAddr[0] <=white_entry->patten.wlGroupIpEnd[0]))
						gipHit=1;
					
				}
				else
					gipHit=1;

				if(white_entry->patten.sipChk)
				{
					if(clientAddr==NULL)//ingore this rule because we don't have this info
						break;
					
					IGMP_CTRL("check clientAddr");			
					if(clientAddr && (white_entry->patten.wlSipStart[0] <= clientAddr[0])  &&  (clientAddr[0] <=white_entry->patten.wlSipEnd[0]))
						sipHit=1;
				}
				else
					sipHit=1;

				if(white_entry->patten.mcSipChk)
				{
					if(white_entry->patten.mcSipChkIgnoreNonIGMPv3MLDv2 && is_igmpv3_mldv2==0)
					{
						//should be treat as hit
						IGMP_CTRL("mcSipChkIgnoreNonIGMPv3MLDv2 and recv is_igmpv3_mldv2==0");
						mcSipHit=1;
					}
					else
					{
						if(mcSourceAddr==NULL)//ingore this rule because we don't have this info
							break;				
						IGMP_CTRL("check mcSourceAddr");
						if(mcSourceAddr && (white_entry->patten.wlMcSip[0] <= mcSourceAddr[0]) && (mcSourceAddr[0] <= white_entry->patten.wlMcSipEnd[0]))
							mcSipHit=1;
					}
				}
				else
					mcSipHit=1;

				IGMP_CTRL("check rule[%d] [smacHit:%d] [sipHit:%d] [mcSipHit:%d] [gipHit:%d]",white_entry->whiteListIdx,smacHit,sipHit,mcSipHit,gipHit);
				if(smacHit && sipHit && mcSipHit && gipHit)
				{
					IGMP_CTRL("Hit v4 WhiteList[%d]",white_entry->whiteListIdx);
					ret= SUCCESS;
					goto WHITWLIST_HIT;
				}

				//unhit break second loop;
				break;
			}
		}
	}
	else if(ipVersion==IP_VERSION6)
	{
		list_for_each_entry(idxEntry,&devInfo->devConf.igmp6.white,entry)
		{
			list_for_each_entry_continue(white_entry,&whiteListHead,entry)
			{
				if(white_entry->whiteListIdx !=idxEntry->entryIdx)
					continue;
				
				smacHit=0,sipHit=0,gipHit=0,mcSipHit=0; 
				whiteListEnable=1;


				if(white_entry->patten.smacChk)
				{
					if(p_smac==NULL)//ingore this rule because we don't have this info
						break;
					if( p_smac&&
						((white_entry->patten.smac[0]&white_entry->patten.smacMask[0])== (p_smac[0]&white_entry->patten.smacMask[0])) &&
						((white_entry->patten.smac[1]&white_entry->patten.smacMask[1])== (p_smac[1]&white_entry->patten.smacMask[1])) &&
						((white_entry->patten.smac[2]&white_entry->patten.smacMask[2])== (p_smac[2]&white_entry->patten.smacMask[2])) &&
						((white_entry->patten.smac[3]&white_entry->patten.smacMask[3])== (p_smac[3]&white_entry->patten.smacMask[3])) &&
						((white_entry->patten.smac[4]&white_entry->patten.smacMask[4])== (p_smac[4]&white_entry->patten.smacMask[4])) &&
						((white_entry->patten.smac[5]&white_entry->patten.smacMask[5])== (p_smac[5]&white_entry->patten.smacMask[5])) )
					{
						smacHit=1;
					}
				}
				else
					smacHit=1;


				if(white_entry->patten.gipChk)
				{

					if(groupAddr)
					{

						if(!((iprange_ipv6_lt(groupAddr, white_entry->patten.wlGroupIpStart) ||
							iprange_ipv6_lt(white_entry->patten.wlGroupIpEnd, groupAddr))))
							gipHit=1;

					}
					else
						break;
				}
				else
					gipHit=1;

				if(white_entry->patten.sipChk)
				{
					if(clientAddr)
					{
						if(!((iprange_ipv6_lt(clientAddr, white_entry->patten.wlSipStart) ||
							iprange_ipv6_lt(white_entry->patten.wlSipEnd, clientAddr))))
							sipHit=1;
					}
					else
						break;
				}
				else
					sipHit=1;

				if(white_entry->patten.mcSipChk)
				{
					if(white_entry->patten.mcSipChkIgnoreNonIGMPv3MLDv2 && is_igmpv3_mldv2==0)
					{
						//should be treat as hit
						IGMP_CTRL("mcSipChkIgnoreNonIGMPv3MLDv2 and recv is_igmpv3_mldv2==0");
						mcSipHit=1;
					}
					else				
					{
						if(mcSourceAddr)
						{
							if(!((iprange_ipv6_lt(mcSourceAddr, white_entry->patten.wlMcSip) ||
								iprange_ipv6_lt(white_entry->patten.wlMcSipEnd, mcSourceAddr))))
								mcSipHit=1;
						}
						else
							break;
					}
				}
				else
					mcSipHit=1;

				if(smacHit && sipHit && gipHit && mcSipHit)
				{
					IGMP_CTRL("Hit v6 WhiteList[%d]",white_entry->whiteListIdx);
					ret= SUCCESS;
					goto WHITWLIST_HIT;
				}
				//unhit break second loop;
				break;

			}
		}

	}

WHITWLIST_HIT:
	if(whiteListEnable==0)
		ret=SUCCESS;


	return ret;
}



#define BLACK_LIST
atomic_t blackLisIcrIdx=ATOMIC_INIT(0);

rtk_igmp_blackListEntry_t * rtk_igmp_blackEntry_malloc(void)
{
	rtk_igmp_blackListEntry_t *pBlack_entry;
	pBlack_entry=rtk_igmp_kmalloc(sizeof(rtk_igmp_blackListEntry_t));
	if(pBlack_entry)
	{
		memset(pBlack_entry,0,sizeof(rtk_igmp_blackListEntry_t));
		INIT_LIST_HEAD(&pBlack_entry->entry);
		list_add_tail(&pBlack_entry->entry, &blackListHead);
		pBlack_entry->blackListIdx=atomic_read(&blackLisIcrIdx);
		atomic_inc(&blackLisIcrIdx);
	}

	return pBlack_entry;
}


int rtk_igmp_blackEntry_free(rtk_igmp_blackListEntry_t* blackEntry)
{
	list_del_init(&blackEntry->entry);
	rtk_igmp_kfree(blackEntry,sizeof(rtk_igmp_blackListEntry_t));
	return (SUCCESS);
}

int rtk_igmp_blackEntry_del(rtk_igmp_blackListEntry_t* blackEntry)
{
	if(blackEntry)
	{
		blackEntry->blackRefCnt--;
		if(blackEntry->blackRefCnt==0)
			rtk_igmp_blackEntry_free(blackEntry);
	}

	return (SUCCESS);
}


int rtk_igmp_blackEntry_flashInit(void)
{
	memset(&blackListHeadEntry,0,sizeof(blackListHeadEntry));
	INIT_LIST_HEAD(&blackListHead);
	return (SUCCESS);

}

int _rtk_igmp_blackList_updateFlow(rt_igmpHook_blackList_t *patten,int32 ifidx )
{
	int32 moduleIndex;
	int32 hashIndex;
	struct rtk_igmp_groupEntry *groupEntryPtr;
	struct rtk_igmp_clientEntry* clientEntry=NULL;

	/* IPv4 */
	for( moduleIndex=0 ; moduleIndex< IGMP_MAX_BR_MODULE_NUM; moduleIndex++)
	{
		if(rtk_igmp_mCastModuleArray[moduleIndex].validBit==0)
			continue;	
		if( patten->isIpv6==0)
		{
			for (hashIndex=0;hashIndex<IGMP_HASH_TBL_SIZE;hashIndex++)
			{
				groupEntryPtr=rtk_igmp_mCastModuleArray[moduleIndex].rtk_igmp_ipv4HashTable[hashIndex];
				while (groupEntryPtr!=NULL)
				{
					clientEntry=groupEntryPtr->clientList;

					while (clientEntry!=NULL)
					{
						if(clientEntry->inIfidx==ifidx )
						{
							if( patten->smacChk &&
								((patten->smac[0]&patten->smacMask[0]) == (clientEntry->clientMacAddr[0]&patten->smacMask[0])) &&
								((patten->smac[1]&patten->smacMask[1]) == (clientEntry->clientMacAddr[1]&patten->smacMask[1])) &&
								((patten->smac[2]&patten->smacMask[2]) == (clientEntry->clientMacAddr[2]&patten->smacMask[2])) &&
								((patten->smac[3]&patten->smacMask[3]) == (clientEntry->clientMacAddr[3]&patten->smacMask[3])) &&	
								((patten->smac[4]&patten->smacMask[4]) == (clientEntry->clientMacAddr[4]&patten->smacMask[4])) &&
								((patten->smac[5]&patten->smacMask[5] )== (clientEntry->clientMacAddr[5]&patten->smacMask[5])) )
							{
								//hit 
								IGMP_CTRL("update delete Client;%pI4  by MacBlackList %pM",clientEntry->clientAddr,clientEntry->clientMacAddr);
								rtk_igmp_deleteClientEntry(groupEntryPtr, clientEntry);
								rtk_igmp_flow_reflush_update(moduleIndex,groupEntryPtr->groupAddr);
								break;
							}
						}

						clientEntry = clientEntry->next;
					}
					groupEntryPtr=groupEntryPtr->next;
				}
				
			}
		}
		else
		{
			for (hashIndex=0;hashIndex<IGMP_HASH_TBL_SIZE;hashIndex++)
			{
				groupEntryPtr=rtk_igmp_mCastModuleArray[moduleIndex].rtk_igmp_ipv6HashTable[hashIndex];
				while (groupEntryPtr!=NULL)
				{
					clientEntry=groupEntryPtr->clientList;

					while (clientEntry!=NULL)
					{
						if(clientEntry->inIfidx==ifidx )
						{
							if( patten->smacChk &&
								((patten->smac[0]&patten->smacMask[0]) == (clientEntry->clientMacAddr[0]&patten->smacMask[0])) &&
								((patten->smac[1]&patten->smacMask[1]) == (clientEntry->clientMacAddr[1]&patten->smacMask[1])) &&
								((patten->smac[2]&patten->smacMask[2]) == (clientEntry->clientMacAddr[2]&patten->smacMask[2])) &&
								((patten->smac[3]&patten->smacMask[3]) == (clientEntry->clientMacAddr[3]&patten->smacMask[3])) &&	
								((patten->smac[4]&patten->smacMask[4]) == (clientEntry->clientMacAddr[4]&patten->smacMask[4])) &&
								((patten->smac[5]&patten->smacMask[5] )== (clientEntry->clientMacAddr[5]&patten->smacMask[5])) )
							{
								//hit 
								IGMP_CTRL("update delete Client;%pI6  by MacBlackList %pM",clientEntry->clientAddr,clientEntry->clientMacAddr);
								rtk_igmp_deleteClientEntry(groupEntryPtr, clientEntry);
								rtk_igmp_flow_reflush_update(moduleIndex,groupEntryPtr->groupAddr);
								break;
							}
						}

						clientEntry = clientEntry->next;
					}
					groupEntryPtr=groupEntryPtr->next;
				}
				
			}
		}
	}


	return SUCCESS;
}



int rtk_igmp_blackListAdd( rt_igmpHook_blackList_t *patten,int32 ifidx , int32 *index)
{

	rtk_igmp_multicastDeviceInfo_t *devInfo =_rtk_igmp_devIfidx_to_devInfo(ifidx);
	rtk_igmp_blackListEntry_t *black_entry =NULL;
	rtk_igmp_blackListEntry_t *black_entry_tmp =NULL;
	struct list_head *p_insertBefortThisEntry=NULL;
	struct list_head *p_devSearchListHdr=NULL;
	rtk_igmp_idxEntry_t *idxEntry=NULL;
	rtk_igmp_idxEntry_t *idxEntry_tmp=NULL;

	if(devInfo==NULL)
		return FAIL;


	//check whiteListHead same entry
	list_for_each_entry(black_entry_tmp,&blackListHead,entry)
	{
		if(memcmp(&black_entry_tmp->patten,patten,sizeof(*patten))==0)
		{
			black_entry=black_entry_tmp;
			break;
		}
	}


	if(black_entry==NULL)
	{
		black_entry=rtk_igmp_blackEntry_malloc();
		memcpy(&black_entry->patten,patten,sizeof(*patten));
		
		if(black_entry==NULL)
			{IGMP_CTRL("alloc fail");return FAIL;}
		idxEntry=rtk_igmp_idxEntry_malloc();
		if(idxEntry==NULL)
		{
			rtk_igmp_blackEntry_free(black_entry);
			{IGMP_CTRL("alloc fail");return FAIL;}
		}
	}

	if(patten->isIpv6)
		p_devSearchListHdr=&devInfo->devConf.igmp6.black;
	else
		p_devSearchListHdr=&devInfo->devConf.igmp.black;


	list_for_each_entry(idxEntry_tmp,p_devSearchListHdr,entry)
	{
		if(idxEntry_tmp->entryIdx==black_entry->blackListIdx)
		{
			*index=idxEntry_tmp->entryIdx;
			if(idxEntry)
				rtk_igmp_idxEntry_free(idxEntry);			
			IGMP_CTRL("hit same entry");
			return SUCCESS;
		}
		else if(idxEntry_tmp->entryIdx > black_entry->blackListIdx)
		{
			IGMP_CTRL("insert blackListIdx=%d  before  entryIdx=%d ",black_entry->blackListIdx,idxEntry_tmp->entryIdx);
			p_insertBefortThisEntry = &idxEntry_tmp->entry;
			break;
		}
		if(idxEntry_tmp->entry.next == p_devSearchListHdr)
		{
			IGMP_CTRL("List insert before Head");
			p_insertBefortThisEntry=p_devSearchListHdr;	
			break;
		}
	}

	//first entry in list
	if(p_insertBefortThisEntry==NULL)
	{
		IGMP_CTRL("List First Entry");
		p_insertBefortThisEntry=p_devSearchListHdr;
	}


	if(idxEntry==NULL)
	{
		idxEntry=rtk_igmp_idxEntry_malloc();
		if(idxEntry==NULL)
			{IGMP_CTRL("alloc fail");return FAIL;}
	}
	idxEntry->entryIdx=black_entry->blackListIdx;
	__igmp_list_add(&idxEntry->entry,p_insertBefortThisEntry->prev,p_insertBefortThisEntry);


	black_entry->blackRefCnt++;
	*index=black_entry->blackListIdx;

	//update data base : remove this mac Client from database
	_rtk_igmp_blackList_updateFlow(patten,ifidx);

	
	return SUCCESS;


}


int rtk_igmp_blackListDel_byIdx(int32 ifidx,int32 index)
{
	rtk_igmp_idxEntry_t *idxEntry=NULL;
	uint32 hit=0;
	rtk_igmp_idxEntry_t *idxEntry_tmp=NULL;
	rtk_igmp_multicastDeviceInfo_t *devInfo =_rtk_igmp_devIfidx_to_devInfo(ifidx);
	rtk_igmp_blackListEntry_t *black_entry =NULL;
	rtk_igmp_blackListEntry_t *black_entry_tmp =NULL;

	if(devInfo==NULL)
	{
		IGMP_CTRL("blackList ifidx=%d Del Idx[%d]  DevInfo not found",ifidx,index);
		return FAIL;
	}


	list_for_each_entry_safe(idxEntry,idxEntry_tmp,&devInfo->devConf.igmp.black,entry)
	{	
		if(idxEntry->entryIdx==index)
		{
			hit=1;
			rtk_igmp_idxEntry_free(idxEntry);
			break;
		}	
	}
	if(hit==0)
	{
		list_for_each_entry_safe(idxEntry,idxEntry_tmp,&devInfo->devConf.igmp6.black,entry)
		{
			if(idxEntry->entryIdx==index)
			{
				hit=1;
				rtk_igmp_idxEntry_free(idxEntry);
				break;
			}
		}
	}

	if(!hit)
	{
		IGMP_CTRL("blackList ifidx=%d Del Idx[%d]  DEL FAIL NOT FOUND",ifidx,index);
		return FAIL;
	}


	//check blackListHead free
	list_for_each_entry_safe(black_entry,black_entry_tmp,&blackListHead,entry)
	{
		if(black_entry->blackListIdx == index)
		{
			hit=1;
			IGMP_CTRL("blackList ifidx=%d Del Idx[%d]",ifidx,index);
			rtk_igmp_blackEntry_del(black_entry);
			break;
		}
	}
	
	
	return SUCCESS;


}


int rtk_igmp_blackListDel(rt_igmpHook_blackList_t *patten,int32 ifidx,int32 index)
{

	if(patten==NULL)
	{
		return rtk_igmp_blackListDel_byIdx(ifidx,index);
	}
	else
	{
		rtk_igmp_multicastDeviceInfo_t *devInfo =_rtk_igmp_devIfidx_to_devInfo(ifidx);
		struct list_head *p_devSearchListHdr=NULL;
		rtk_igmp_blackListEntry_t *black_entry =NULL;
		rtk_igmp_blackListEntry_t *black_entry_tmp =NULL;
		rtk_igmp_idxEntry_t *blackIdx;
		rtk_igmp_idxEntry_t *blackIdx_tmp;		
		int32 ipversionHit=0,smacHit=0;
		int32 hit=0;

		if(devInfo==NULL)
			return FAIL;

		if(patten->isIpv6)
			p_devSearchListHdr=&devInfo->devConf.igmp6.black;
		else
			p_devSearchListHdr=&devInfo->devConf.igmp.black;

		black_entry= &blackListHeadEntry;

		list_for_each_entry_safe(blackIdx,blackIdx_tmp,p_devSearchListHdr,entry)
		{
			list_for_each_entry_continue(black_entry,&blackListHead,entry)
			{
				black_entry_tmp = list_prev_entry(black_entry, entry);
				if(blackIdx->entryIdx!=black_entry->blackListIdx)
					continue;

				ipversionHit=0,smacHit=0;

				if(patten->isIpv6 == black_entry->patten.isIpv6)
					ipversionHit=1;


				if(patten->smacChk)
				{
					if(black_entry->patten.smacChk &&memcmp(&black_entry->patten.smac,&patten->smac,sizeof(patten->smac))==0 && memcmp(&black_entry->patten.smacMask,&patten->smacMask,sizeof(patten->smacMask))==0)
						smacHit=1;
				}
				else
					smacHit=1;
				if(ipversionHit  && smacHit )
				{
					hit=1;
					IGMP_CTRL("Delete blackList[%d] on %s",black_entry->blackListIdx,devInfo->devName);
					rtk_igmp_idxEntry_free(blackIdx);
					rtk_igmp_blackEntry_del(black_entry);
					//point to list_prev_entry and break;
					black_entry = black_entry_tmp;
					break;
				}
				break;
			}
		}

		if(!hit)
			IGMP_CTRL(" %s PATTEN DEL NOT FOUND",devInfo->devName);

		return SUCCESS;
	}

}


//return SUCCESS(0) hit black list
int rtk_igmp_filterblackListCheck(rtk_igmp_multicastDeviceInfo_t *devInfo,uint32 ipVersion,uint8 *saMac)
{
	int ret=FAIL;
	rtk_igmp_blackListEntry_t *black_entry;
	rtk_igmp_idxEntry_t *idxEntry=NULL;
	int smacHit=0;
	unsigned char _smac[6]={0};
	unsigned char *p_smac=NULL;

	if(saMac && memcmp(_smac,saMac,sizeof(_smac))==0)
	{
		//saMac all zero , means fc not assign sa value set to a null
		p_smac=NULL;
	}
	else
		p_smac=saMac;

	black_entry= &blackListHeadEntry;

	if(ipVersion==IP_VERSION4)
	{
		list_for_each_entry(idxEntry,&devInfo->devConf.igmp.black,entry)
		{
			list_for_each_entry_continue(black_entry,&blackListHead,entry)
			{

				//not this entry continue
				if(black_entry->blackListIdx !=idxEntry->entryIdx)
					continue;
				
				smacHit=0;
				IGMP_CTRL("check blackList[%d]",idxEntry->entryIdx);

				if(black_entry->patten.smacChk)
				{
					if(p_smac==NULL)//ingore this rule because we don't have this info
						break;
					IGMP_CTRL("check smac");		
					if( ((black_entry->patten.smac[0]&black_entry->patten.smacMask[0]) == (p_smac[0]&black_entry->patten.smacMask[0])) &&
						((black_entry->patten.smac[1]&black_entry->patten.smacMask[1]) == (p_smac[1]&black_entry->patten.smacMask[1])) &&
						((black_entry->patten.smac[2]&black_entry->patten.smacMask[2]) == (p_smac[2]&black_entry->patten.smacMask[2])) &&
						((black_entry->patten.smac[3]&black_entry->patten.smacMask[3]) == (p_smac[3]&black_entry->patten.smacMask[3])) &&	
						((black_entry->patten.smac[4]&black_entry->patten.smacMask[4]) == (p_smac[4]&black_entry->patten.smacMask[4])) &&
						((black_entry->patten.smac[5]&black_entry->patten.smacMask[5] )== (p_smac[5]&black_entry->patten.smacMask[5])) )
					{
						smacHit=1;
					}
				}
				else
					smacHit=1;


				IGMP_CTRL("check rule[%d] [smacHit:%d] ",black_entry->blackListIdx,smacHit);
				if(smacHit)
				{
					IGMP_CTRL("Hit v4 BLACK List[%d]",black_entry->blackListIdx);
					ret= SUCCESS;
					goto BLACKLIST_HIT;
				}

				//unhit break second loop;
				break;

			}

		}
	}
	else if(ipVersion==IP_VERSION6)
	{
		list_for_each_entry(idxEntry,&devInfo->devConf.igmp6.black,entry)
		{
			list_for_each_entry_continue(black_entry,&blackListHead,entry)
			{

				if(black_entry->blackListIdx !=idxEntry->entryIdx)
					continue;
				
				smacHit=0;

				if(black_entry->patten.smacChk)
				{
					IGMP_CTRL("check smac");

					if(p_smac==NULL)//ingore this rule because we don't have this info
						break;
					if( ((black_entry->patten.smac[0]&black_entry->patten.smacMask[0]) == (p_smac[0]&black_entry->patten.smacMask[0])) &&
						((black_entry->patten.smac[1]&black_entry->patten.smacMask[1]) == (p_smac[1]&black_entry->patten.smacMask[1])) &&
						((black_entry->patten.smac[2]&black_entry->patten.smacMask[2]) == (p_smac[2]&black_entry->patten.smacMask[2])) &&
						((black_entry->patten.smac[3]&black_entry->patten.smacMask[3]) == (p_smac[3]&black_entry->patten.smacMask[3])) &&	
						((black_entry->patten.smac[4]&black_entry->patten.smacMask[4]) == (p_smac[4]&black_entry->patten.smacMask[4])) &&
						((black_entry->patten.smac[5]&black_entry->patten.smacMask[5] )== (p_smac[5]&black_entry->patten.smacMask[5])) )
					{
						smacHit=1;
					}
				}
				else
					smacHit=1;


				IGMP_CTRL("check rule[%d] [smacHit:%d] ",black_entry->blackListIdx,smacHit);
				if(smacHit)
				{
					IGMP_CTRL("Hit v6 BLACK List[%d]",black_entry->blackListIdx);
					ret= SUCCESS;
					goto BLACKLIST_HIT;
				}
				//unhit break second loop;
				break;

			}
		}
	}

BLACKLIST_HIT:

	return ret;
}



#define RESERVED_GROUP
int rtk_igmp_groupToPsTbl_flashInit(void)
{
	int i;

	INIT_LIST_HEAD(&gorup2PsFreeListHead);
	INIT_LIST_HEAD(&gorup2PsListHead);
	
	for(i=0 ; i<IGMP_MAX_USER_REV2PS_GROUP_COUNT;i++ )
	{
		memset(&gorup2Ps[i],0,sizeof(rtk_igmp_userResvGroup_t));
		gorup2Ps[i].resvGrpListIdx = i;
		INIT_LIST_HEAD(&gorup2Ps[i].entry);
		list_add_tail(&gorup2Ps[i].entry, &gorup2PsFreeListHead);
	}
	return (SUCCESS);
}


int rtk_igmp_groupToPsTbl_free(rtk_igmp_userResvGroup_t* rsvEntry)
{

	list_del_init(&rsvEntry->entry);
	list_add(&rsvEntry->entry, &gorup2PsFreeListHead);
	rsvEntry->valid=0;
	return (SUCCESS);
}


rtk_igmp_userResvGroup_t * rtk_igmp_groupToPsTbl_malloc(void)
{
	rtk_igmp_userResvGroup_t *group2Psentry;
	rtk_igmp_userResvGroup_t *group2Psentry_tmp;

	if(list_empty(&gorup2PsFreeListHead))
	{
		IGMP_WARNING("all free IGMP rtk_igmp_groupToPsTbl_malloc are allocated...");
		return (NULL);
	}
	list_for_each_entry_safe(group2Psentry,group2Psentry_tmp,&gorup2PsFreeListHead,entry)		//just return the first entry right behind of head
	{
		list_del_init(&group2Psentry->entry);
		break;
	}
	memset(&group2Psentry->patten,0,sizeof(group2Psentry->patten));
	group2Psentry->valid=1;
	group2Psentry->cbKnownGrpToHw=0;
	return group2Psentry;
}


int rtk_igmp_groupToPsTblAdd(rt_igmpHook_ignoreGroup_t* patten,uint32 *index)
{
	rtk_igmp_userResvGroup_t *group2Psentry;
	uint8 multigroup=0;
	uint8 resvGroup=0;
	if(index==NULL)
		return FAIL;
	if(memcmp(patten->ignGroupIpStart,patten->ignGroupIpEnd,sizeof(patten->ignGroupIpStart))!=0)
	{
		IGMP_CTRL("Not support multi-group to hard please add acl by yoru self %pI6 != %pI6",patten->ignGroupIpStart,patten->ignGroupIpEnd);
		multigroup=1;
	}
	else
	{

		if(patten->isIpv6 && IN_MULTICAST6_RESV1_FFOX(patten->ignGroupIpStart[0]))
			resvGroup = 1;
		if(!patten->isIpv6 && IN_MULTICAST_RESV_SPECIAL_IPV4(patten->ignGroupIpStart[0]))
			resvGroup = 1;
	}
	
	
	list_for_each_entry(group2Psentry,&gorup2PsListHead,entry)
	{
		if(memcmp(&group2Psentry->patten,patten,sizeof(rt_igmpHook_ignoreGroup_t))==0)
		{
			*index = group2Psentry->resvGrpListIdx;
			return SUCCESS;
		}
	}

	group2Psentry = rtk_igmp_groupToPsTbl_malloc();
	if(group2Psentry)
	{
		list_add(&group2Psentry->entry,&gorup2PsListHead);
		memcpy(&group2Psentry->patten,patten,sizeof(group2Psentry->patten));
		*index = group2Psentry->resvGrpListIdx;
	}
	else
		return FAIL;

	if( multigroup || resvGroup)
	{
		//do not add hardware to known group
	}
	else if(hwCb.groupAddCbEvt)
	{
		rtk_igmp_groupCbEvt_t groupCb;
		memset(&groupCb,0,sizeof(groupCb));
		groupCb.isIPv6= patten->isIpv6;
		memcpy(groupCb.group ,patten->ignGroupIpStart,sizeof(groupCb.group));
		IGMP_CTRL("GROUP ADD CALL BACK2 HW");
		group2Psentry->cbKnownGrpToHw=1;
		hwCb.groupAddCbEvt(&groupCb);
	}
	rtk_igmp_flow_reflush_update(FAIL,NULL);


	return SUCCESS;
}

int rtk_igmp_groupToPsTblDel(rt_igmpHook_ignoreGroup_t* patten,uint32 index)
{
	if(patten==NULL)
	{
		if( IGMP_MAX_USER_REV2PS_GROUP_COUNT <= index)
			return FAIL;
		if(gorup2Ps[index].valid==0)
			return FAIL;

		if(hwCb.groupDelCbEvt  && gorup2Ps[index].cbKnownGrpToHw)
		{
			rtk_igmp_groupCbEvt_t groupCb;
			memset(&groupCb,0,sizeof(groupCb));
			groupCb.isIPv6= gorup2Ps[index].patten.isIpv6;
			groupCb.asunknownNotFlush=gorup2Ps[index].patten.isCopy2cpu;
			memcpy(groupCb.group ,gorup2Ps[index].patten.ignGroupIpStart,sizeof(groupCb.group));
			IGMP_CTRL("GROUP DEL CALL BACK2 HW");
			hwCb.groupDelCbEvt(&groupCb);
		}
		
		rtk_igmp_groupToPsTbl_free(&gorup2Ps[index]);
		rtk_igmp_flow_reflush_update(FAIL,NULL);
		return SUCCESS;
	}
	else
	{
		rtk_igmp_userResvGroup_t *groupToPs_entry =NULL;
		rtk_igmp_userResvGroup_t *group2Psentry_tmp=NULL;
		
		list_for_each_entry_safe(groupToPs_entry,group2Psentry_tmp,&gorup2PsListHead,entry)
		{
			if(memcmp(&groupToPs_entry->patten,patten,sizeof(rt_igmpHook_ignoreGroup_t))==0)
			{
				if(hwCb.groupDelCbEvt && groupToPs_entry->cbKnownGrpToHw)
				{
					rtk_igmp_groupCbEvt_t groupCb;
					memset(&groupCb,0,sizeof(groupCb));
					groupCb.isIPv6= groupToPs_entry->patten.isIpv6;
					groupCb.asunknownNotFlush=groupToPs_entry->patten.isCopy2cpu;
					memcpy(groupCb.group ,groupToPs_entry->patten.ignGroupIpStart,sizeof(groupCb.group));
					IGMP_CTRL("GROUP DEL CALL BACK2 HW");
					hwCb.groupDelCbEvt(&groupCb);
				}

				rtk_igmp_groupToPsTbl_free(groupToPs_entry);
				rtk_igmp_flow_reflush_update(FAIL,NULL);
				return SUCCESS;
			}
		}
		return FAIL;
	}

	return SUCCESS;
}



rtk_igmp_userResvGroup_t * _rtk_igmp_checkGroupToPsTbl(uint32 ipVersion,uint32 *_groupIp)
{
	
	rtk_igmp_userResvGroup_t *group2PsEt;
	int32 groupHit=0;
	
	list_for_each_entry(group2PsEt,&gorup2PsListHead,entry)
	{
		groupHit=0;
		if(ipVersion==IP_VERSION6 && group2PsEt->patten.isIpv6==1)
		{
			if(!((iprange_ipv6_lt(_groupIp, group2PsEt->patten.ignGroupIpStart) ||
				iprange_ipv6_lt(group2PsEt->patten.ignGroupIpEnd, _groupIp))))
				groupHit=1;
		}
		else if ((ipVersion==IP_VERSION4 && group2PsEt->patten.isIpv6==0))
		{
			if(group2PsEt->patten.ignGroupIpEnd[0]==0)
			{
				if((group2PsEt->patten.ignGroupIpStart[0] ) == (_groupIp[0]))
					groupHit=1;
			}
			else
			{
				if((group2PsEt->patten.ignGroupIpStart[0] <= _groupIp[0])	&&	(_groupIp[0] <=group2PsEt->patten.ignGroupIpEnd[0]))
					groupHit=1;
			}

		}
		if(groupHit)
		{
			IGMP_DATA("Hit ignore group table[%d]",group2PsEt->resvGrpListIdx);
			return group2PsEt;
		}
	}

	return NULL;
}

int rtk_igmp_groupToPsDump(struct seq_file *s, void *v)
{
	int i;
	PROC_PRINTF("GroupToPs Table:\n");
	for (i=0 ; i<IGMP_MAX_USER_REV2PS_GROUP_COUNT ;i++)
	{
		if(!gorup2Ps[i].valid)
			continue;

		if(gorup2Ps[i].patten.isIpv6)
		{
			PROC_PRINTF("[%d]ignGroupIpStart=%pI6   ignGroupIpEnd=%pI6 %s %s\n",i,gorup2Ps[i].patten.ignGroupIpStart,gorup2Ps[i].patten.ignGroupIpEnd,gorup2Ps[i].patten.isCopy2cpu?"COPY2CPU":"",gorup2Ps[i].cbKnownGrpToHw?"HW_KNOWN":"");
		}
		else
		{
			PROC_PRINTF("[%d]ignGroupIpStart=%pI4h  ignGroupIpEnd=%pI4h %s %s\n",i,gorup2Ps[i].patten.ignGroupIpStart,gorup2Ps[i].patten.ignGroupIpEnd,gorup2Ps[i].patten.isCopy2cpu?"COPY2CPU":"",gorup2Ps[i].cbKnownGrpToHw?"HW_KNOWN":"");
		}
	}
	return SUCCESS;
}

#define GROUP_WEIGHT

int rtk_igmp_groupWeight_flashInit(void)
{
	int i;

	INIT_LIST_HEAD(&groupWeightFreeListHead);
	INIT_LIST_HEAD(&groupWeightListHead);
	
	for(i=0 ; i<IGMP_MAX_USER_GROUP_WEIGHT;i++ )
	{
		memset(&groupWeight[i],0,sizeof(rtk_igmp_groupWeight_t));
		groupWeight[i].groupWeightListIdx = i;
		INIT_LIST_HEAD(&groupWeight[i].entry);
		list_add_tail(&groupWeight[i].entry, &groupWeightFreeListHead);
	}
	return (SUCCESS);
}


int rtk_igmp_groupWeightTbl_free(rtk_igmp_groupWeight_t* grpEntry)
{

	list_del_init(&grpEntry->entry);
	list_add(&grpEntry->entry, &groupWeightFreeListHead);
	grpEntry->valid=0;
	return (SUCCESS);
}


rtk_igmp_groupWeight_t * rtk_igmp_groupWeightTbl_malloc(void)
{
	rtk_igmp_groupWeight_t *groupWeightentry;
	rtk_igmp_groupWeight_t *groupWeightentry_tmp;

	if(list_empty(&groupWeightFreeListHead))
	{
		IGMP_WARNING("all free IGMP rtk_igmp_groupWeightTbl_malloc are allocated...");
		return (NULL);
	}
	list_for_each_entry_safe(groupWeightentry,groupWeightentry_tmp,&groupWeightFreeListHead,entry)		//just return the first entry right behind of head
	{
		list_del_init(&groupWeightentry->entry);
		break;
	}
	memset(&groupWeightentry->patten,0,sizeof(groupWeightentry->patten));
	groupWeightentry->valid=1;
	return groupWeightentry;
}


int rtk_igmp_groupWeightTblAdd(rt_igmpHook_devInfo_t devInfo,rt_igmpHook_groupWeight_t* patten,uint32 *index)
{
	rtk_igmp_groupWeight_t *groupWeightentry;
	int32 ifIdx=FAIL;
	if(index==NULL)
		return FAIL;

	ifIdx = rtk_igmp_devInfoToIfIdx(devInfo);
	if(ifIdx==FAIL) return FAIL;


	list_for_each_entry(groupWeightentry,&groupWeightListHead,entry)
	{
		if(    memcmp(groupWeightentry->patten.weightGroupIpStart,patten->weightGroupIpStart,sizeof(groupWeightentry->patten.weightGroupIpStart))==0 
			&& memcmp(groupWeightentry->patten.weightGroupIpEnd,patten->weightGroupIpEnd,sizeof(groupWeightentry->patten.weightGroupIpEnd))==0
			&& groupWeightentry->patten.isIpv6 ==patten->isIpv6
			&& ifIdx==groupWeightentry->devIdx)
		{
			*index = groupWeightentry->groupWeightListIdx;
			return SUCCESS;
		}
	}

	groupWeightentry = rtk_igmp_groupWeightTbl_malloc();
	if(groupWeightentry)
	{
		list_add(&groupWeightentry->entry,&groupWeightListHead);
		memcpy(&groupWeightentry->patten,patten,sizeof(groupWeightentry->patten));
		groupWeightentry->devIdx = ifIdx;
		*index = groupWeightentry->groupWeightListIdx;
	}
	else
		return FAIL;


	return SUCCESS;
}

int rtk_igmp_groupWeightTblDel(rt_igmpHook_devInfo_t devInfo,rt_igmpHook_groupWeight_t* patten,uint32 index)
{
	if(patten==NULL)
	{
		if(IGMP_MAX_USER_GROUP_WEIGHT <= index)
			return FAIL;
		if(groupWeight[index].valid==0)
			return FAIL;

		return rtk_igmp_groupWeightTbl_free(&groupWeight[index]);

	}
	else
	{
		rtk_igmp_groupWeight_t *groupWeight_entry =NULL;
		rtk_igmp_groupWeight_t *groupWeightentry_tmp=NULL;
		int32 ifIdx=FAIL;

		ifIdx = rtk_igmp_devInfoToIfIdx(devInfo);
		if(ifIdx==FAIL) return FAIL;

		
		list_for_each_entry_safe(groupWeight_entry,groupWeightentry_tmp,&groupWeightListHead,entry)
		{
			if(memcmp(&groupWeight_entry->patten,patten,sizeof(rt_igmpHook_groupWeight_t))==0 && ifIdx==groupWeight_entry->devIdx)
			{

				return rtk_igmp_groupWeightTbl_free(groupWeight_entry);
			}
		}
		return FAIL;
	}

	return SUCCESS;
}


int rtk_igmp_weightOverCheck(rtk_igmp_multicastDeviceInfo_t* devInfocfg,rtk_igmp_multicastBrDeviceInfo_t* brDevInfocfg ,int32 ipVersion,uint32 *groupAddr)
{
	rt_igmpHook_devInfo_t devInfo={{0}};
	rt_igmpHook_devInfo_t brdevInfo={{0}};
	int32 devGetIdx=0,brGetIdx=0;
	int32 isIpv6=0;
	uint32 devTotalWeight=0,brTotalWeigh=0;
	uint32 devGroupWeight=0,brGroupWeight=0;
	int32 devOverWeightDrop=0,brOverWeightDrop=0;
	uint32 devMaxWeight=0,brMaxWeight=0;
	rt_igmpHook_groupStatistic_t statisticInfo;
	int32 ret=SUCCESS;

	devInfo.devIfidx=devInfocfg->ifindex;
	brdevInfo.devIfidx =brDevInfocfg->ifindex;
	if(ipVersion==IP_VERSION6)
	{
		isIpv6=1;
		devMaxWeight=devInfocfg->devConf.igmp6.igmp6MaxWeight;
		brMaxWeight=brDevInfocfg->brDevConf.sysIp6MaxWeight;
		devOverWeightDrop = devInfocfg->devConf.igmp6.igmp6OverWeightDrop;
		brOverWeightDrop = brDevInfocfg->brDevConf.sysIp6OverWeightDrop;
	}
	else
	{
		isIpv6=0;
		devMaxWeight=devInfocfg->devConf.igmp.igmpMaxWeight;
		brMaxWeight=brDevInfocfg->brDevConf.sysIp4MaxWeight;		
		devOverWeightDrop = devInfocfg->devConf.igmp.igmpOverWeightDrop;
		brOverWeightDrop = brDevInfocfg->brDevConf.sysIp4OverWeightDrop;
	}

	devGroupWeight =_rtk_igmp_checkGroupWeightTbl(devInfo.devIfidx,ipVersion,groupAddr);
	brGroupWeight =_rtk_igmp_checkGroupWeightTbl(brdevInfo.devIfidx,ipVersion,groupAddr);

	while(1)
	{
		if(rtk_igmp_nextValidGroupStatistic_get(devInfo,&devGetIdx,isIpv6,&statisticInfo)==SUCCESS && devGetIdx>=0)
		{
			devTotalWeight+=_rtk_igmp_checkGroupWeightTbl(devInfo.devIfidx,ipVersion,statisticInfo.groupAddr);
			devGetIdx++;
		}
		else
			break;
	}


	IGMP_CTRL("devTotalWeight=%d WeightLimit=%d devGroupWeight=%d",devTotalWeight,devMaxWeight,devGroupWeight);
	if(devMaxWeight && ((devTotalWeight+devGroupWeight)>devMaxWeight))
	{
		if(isIpv6)
			devInfocfg->devStatistic.igmp6WeightExceedCnt++;
		else		
			devInfocfg->devStatistic.igmp4WeightExceedCnt++;

		if(devOverWeightDrop)
			ret=FAIL;
	}

	while(1)
	{
		if(rtk_igmp_nextValidGroupStatistic_get(brdevInfo,&brGetIdx,isIpv6,&statisticInfo)==SUCCESS && brGetIdx>=0)
		{
			brTotalWeigh+=_rtk_igmp_checkGroupWeightTbl(brdevInfo.devIfidx,ipVersion,statisticInfo.groupAddr);
			brGetIdx++;
		}
		else
			break;
	}

	IGMP_CTRL("brTotalWeigh=%d WeightLimit=%d brGroupWeight=%d",brTotalWeigh,brMaxWeight,brGroupWeight);
	if(brMaxWeight && ((brTotalWeigh+brGroupWeight)>brMaxWeight))
	{
		if(isIpv6)
			brDevInfocfg->brStatistic.igmp6WeightExceedCnt++;
		else		
			brDevInfocfg->brStatistic.igmp4WeightExceedCnt++;

		if(brOverWeightDrop)
			ret=FAIL;
	}

	return ret;

}

//return weight
unsigned int _rtk_igmp_checkGroupWeightTbl(uint32 ifIdx,uint32 ipVersion,uint32 *_groupIp)
{
	
	rtk_igmp_groupWeight_t *groupWeightEt;
	int32 groupHit=0;

	list_for_each_entry(groupWeightEt,&groupWeightListHead,entry)
	{
		if(groupWeightEt->devIdx!=ifIdx)
			continue;
		groupHit=0;
		if(ipVersion==IP_VERSION6 && groupWeightEt->patten.isIpv6==1)
		{

			if(!((iprange_ipv6_lt(_groupIp, groupWeightEt->patten.weightGroupIpStart) ||
				iprange_ipv6_lt(groupWeightEt->patten.weightGroupIpEnd, _groupIp))))
				groupHit=1;		

			if(groupHit)
				return groupWeightEt->patten.weight;
			
		}
		else if ((ipVersion==IP_VERSION4 && groupWeightEt->patten.isIpv6==0))
		{
			if(groupWeightEt->patten.weightGroupIpEnd[0]==0)
			{
				if((groupWeightEt->patten.weightGroupIpStart[0] ) == (_groupIp[0]))
					groupHit=1;
			}
			else
			{
				if((groupWeightEt->patten.weightGroupIpStart[0] <= _groupIp[0])	&&	(_groupIp[0] <=groupWeightEt->patten.weightGroupIpEnd[0]))
					groupHit=1;
			}

			if(groupHit)
				return groupWeightEt->patten.weight;

		}
	}

	IGMP_CTRL("return Default Weight =0 ");
	//default weight
	return 0;
}

int rtk_igmp_groupWeightDump(struct seq_file *s, void *v)
{
	int i;
	struct net_device * dev;

	PROC_PRINTF("groupWeight Table:\n");
	for (i=0 ; i<IGMP_MAX_USER_GROUP_WEIGHT ;i++)
	{
		if(!groupWeight[i].valid)
			continue;

		dev=rtk_igmp_devIfidx_to_dev(groupWeight[i].devIdx);

		if(dev)
		{
			if(groupWeight[i].patten.isIpv6)
			{
				PROC_PRINTF("[%d]%s weightGroupIpStart=%pI6   weightGroupIpEnd=%pI6\n",i,dev->name,groupWeight[i].patten.weightGroupIpStart,groupWeight[i].patten.weightGroupIpEnd);
			}
			else
			{
				PROC_PRINTF("[%d]%s weightGroupIpStart=%pI4h  weightGroupIpEnd=%pI4h\n",i,dev->name,groupWeight[i].patten.weightGroupIpStart,groupWeight[i].patten.weightGroupIpEnd);
			}
		}
	}
	return SUCCESS;
}

#define DEV


int rtk_igmp_BrDevConfig_set(int devifid ,rt_igmpHook_br_control_list_t type , uint32 value)
{
	struct rtk_igmp_multicastModule* BrDevInfo = _rtk_igmp_devIfidx_to_BrDevInfo(devifid);
	struct net_bridge *br =NULL;
	struct net_bridge_port *p;
	if(BrDevInfo== NULL) return FAIL;
	switch ( type )
	{

	    case RT_BR_MAX_GROUP_SIZE:
			BrDevInfo->brDevInfo.brDevConf.sysMaxGroupSize = value;
	        break;

	    case RT_BR_MAX_IP4_GROUP_SIZE:
			BrDevInfo->brDevInfo.brDevConf.sysIp4MaxGroupSize= value;
	        break;

	    case RT_BR_MAX_IP6_GROUP_SIZE:
			BrDevInfo->brDevInfo.brDevConf.sysIp6MaxGroupSize= value;
	        break;

	    case RT_BR_MAX_CLIENT_SIZE:
			BrDevInfo->brDevInfo.brDevConf.sysMaxClientSize= value;
	        break;

	    case RT_BR_MAX_IP4_CLIENT_SIZE:
			BrDevInfo->brDevInfo.brDevConf.sysIp4MaxClientSize= value;
	        break;

	    case RT_BR_MAX_IP6_CLIENT_SIZE:
			BrDevInfo->brDevInfo.brDevConf.sysIp6MaxClientSize= value;
	        break;
		
		case RT_BR_PER_IP4_CLIENT_MAX_JOIN_GROUP:
			BrDevInfo->brDevInfo.brDevConf.perIp4ClientMaxJoinGroupSize= value;
			break;
		
		case RT_BR_PER_IP6_CLIENT_MAX_JOIN_GROUP:
			BrDevInfo->brDevInfo.brDevConf.perIp6ClientMaxJoinGroupSize= value;
			break;
		
		case RT_BR_MAX_IP4_WEIGHT:
			BrDevInfo->brDevInfo.brDevConf.sysIp4MaxWeight= value;
			break;
		
		case RT_BR_MAX_IP6_WEIGHT:
			BrDevInfo->brDevInfo.brDevConf.sysIp6MaxWeight= value;
			break;
		
		case RT_BR_DROP_IP4_OVER_WEIGHT:
			BrDevInfo->brDevInfo.brDevConf.sysIp4OverWeightDrop= value;
			break;
		
		case RT_BR_DROP_IP6_OVER_WEIGHT:
			BrDevInfo->brDevInfo.brDevConf.sysIp6OverWeightDrop= value;		
			break;

		case RT_BR_COMBINE_ID:
			BrDevInfo->brDevInfo.brDevConf.combineId= value; 	
			break;

		case RT_BR_DROP_IGMPV1:
			br = netdev_priv(BrDevInfo->brDevInfo.dev);
			if(br != NULL)
			{
				list_for_each_entry(p, &br->port_list, list) {
					rtk_igmp_devConfig_set(p->dev->ifindex, 0,RT_DROP_IGMPV1 ,value);
				}
			}
			break;

		case RT_BR_DROP_IGMPV2:
			br = netdev_priv(BrDevInfo->brDevInfo.dev);
			if(br != NULL)
			{
				list_for_each_entry(p, &br->port_list, list) {
					rtk_igmp_devConfig_set(p->dev->ifindex, 0,RT_DROP_IGMPV2 ,value);
				}
			}

			break;
		case RT_BR_DROP_IGMPV3:
			br = netdev_priv(BrDevInfo->brDevInfo.dev);
			if(br != NULL)
			{
				list_for_each_entry(p, &br->port_list, list) {
					rtk_igmp_devConfig_set(p->dev->ifindex, 0,RT_DROP_IGMPV3 ,value);
				}
			}

			break;
		case RT_BR_DROP_MLDV1:
			br = netdev_priv(BrDevInfo->brDevInfo.dev);
			if(br != NULL)
			{
				list_for_each_entry(p, &br->port_list, list) {
					rtk_igmp_devConfig_set(p->dev->ifindex, 1,RT_DROP_MLDV1 ,value);
				}
			}
			break;
			
		case RT_BR_DROP_MLDV2:
			br = netdev_priv(BrDevInfo->brDevInfo.dev);
			if(br != NULL)
			{
				list_for_each_entry(p, &br->port_list, list) {
					rtk_igmp_devConfig_set(p->dev->ifindex, 1,RT_DROP_MLDV2 ,value);
				}
			}
			break;
			
		case RT_BR_SNOOPING_DISABLE:
			br = netdev_priv(BrDevInfo->brDevInfo.dev);
			if(br != NULL)
			{
				list_for_each_entry(p, &br->port_list, list) {
					rtk_igmp_devConfig_set(p->dev->ifindex, 0,RT_SNOOPING_DISABLE ,value);
					rtk_igmp_devConfig_set(p->dev->ifindex, 1,RT_SNOOPING_DISABLE ,value);
				}
			}
			break;
			
		case RT_BR_FLUSH_CONF:
			br = netdev_priv(BrDevInfo->brDevInfo.dev);
			if(br != NULL)
			{
				list_for_each_entry(p, &br->port_list, list) {
					rtk_igmp_reset_devConfig(p->dev->ifindex, 0);
					rtk_igmp_reset_devConfig(p->dev->ifindex, 1);
				}
			}
			break;
			
		case RT_BR_FLUSH_MDB:
			br = netdev_priv(BrDevInfo->brDevInfo.dev);
			if(br != NULL)
			{
				list_for_each_entry(p, &br->port_list, list) {
					rtk_igmp_flush_Br_mdb(BrDevInfo->brDevInfo.ifindex,0);
					rtk_igmp_flush_Br_mdb(BrDevInfo->brDevInfo.ifindex,1);
				}
			}
			break;
		case RT_BR_FLUSH_MDBv4:
			br = netdev_priv(BrDevInfo->brDevInfo.dev);
			if(br != NULL)
			{
				list_for_each_entry(p, &br->port_list, list) {
					rtk_igmp_flush_Br_mdb(BrDevInfo->brDevInfo.ifindex,0);
				}
			}
			break;
		case RT_BR_FLUSH_MDBv6:
			br = netdev_priv(BrDevInfo->brDevInfo.dev);
			if(br != NULL)
			{
				list_for_each_entry(p, &br->port_list, list) {
					rtk_igmp_flush_Br_mdb(BrDevInfo->brDevInfo.ifindex,1);
				}
			}
			break;

			
		case RT_BR_RATE_LIMIT:
			br = netdev_priv(BrDevInfo->brDevInfo.dev);
			if(br != NULL)
			{
				list_for_each_entry(p, &br->port_list, list) {
					rtk_igmp_devConfig_set(p->dev->ifindex, 0,RT_RATE_LIMIT, value);
					rtk_igmp_devConfig_set(p->dev->ifindex, 1,RT_RATE_LIMIT, value);
				}
			}
			break;

		case RT_BR_RATE_LIMIT_UMC:
			br = netdev_priv(BrDevInfo->brDevInfo.dev);
			if(br != NULL)
			{
				list_for_each_entry(p, &br->port_list, list) {
					rtk_igmp_devConfig_set(p->dev->ifindex, 0,RT_RATE_LIMIT_UMC, value);
					rtk_igmp_devConfig_set(p->dev->ifindex, 1,RT_RATE_LIMIT_UMC, value);
				}
			}
			break;

			
		case RT_BR_FAST_LEAVE:
			br = netdev_priv(BrDevInfo->brDevInfo.dev);
			if(br != NULL)
			{
				list_for_each_entry(p, &br->port_list, list) {
					rtk_igmp_devConfig_set(p->dev->ifindex, 0,RT_FAST_LEAVE, value);
					rtk_igmp_devConfig_set(p->dev->ifindex, 1,RT_FAST_LEAVE, value);
				}
			}
			break;

	    default:
			IGMP_WARNING("not support type");
	}
	return SUCCESS;
}


int rtk_igmp_BrDevConfig_get(int devifid ,rt_igmpHook_br_control_list_t type , uint32* value)
{
	struct rtk_igmp_multicastModule* BrDevInfo = _rtk_igmp_devIfidx_to_BrDevInfo(devifid);
	if(BrDevInfo== NULL) return FAIL;
	switch ( type )
	{

	    case RT_BR_MAX_GROUP_SIZE:
			*value = BrDevInfo->brDevInfo.brDevConf.sysMaxGroupSize;
	        break;

	    case RT_BR_MAX_IP4_GROUP_SIZE:
			*value = BrDevInfo->brDevInfo.brDevConf.sysIp4MaxGroupSize;

	        break;

	    case RT_BR_MAX_IP6_GROUP_SIZE:
			*value = BrDevInfo->brDevInfo.brDevConf.sysIp6MaxGroupSize;

	        break;

	    case RT_BR_MAX_CLIENT_SIZE:
			*value = BrDevInfo->brDevInfo.brDevConf.sysMaxClientSize;

	        break;

	    case RT_BR_MAX_IP4_CLIENT_SIZE:
			*value = BrDevInfo->brDevInfo.brDevConf.sysIp4MaxClientSize;

	        break;

	    case RT_BR_MAX_IP6_CLIENT_SIZE:
			*value = BrDevInfo->brDevInfo.brDevConf.sysIp6MaxClientSize;

	        break;
		case RT_BR_PER_IP4_CLIENT_MAX_JOIN_GROUP:
			*value =BrDevInfo->brDevInfo.brDevConf.perIp4ClientMaxJoinGroupSize;

			break;
		case RT_BR_PER_IP6_CLIENT_MAX_JOIN_GROUP:
			*value = BrDevInfo->brDevInfo.brDevConf.perIp6ClientMaxJoinGroupSize;
			break;
		
		case RT_BR_COMBINE_ID:
			*value = BrDevInfo->brDevInfo.brDevConf.combineId; 	
			break;



	    default:
			IGMP_WARNING("not support type:%d to get",type);
			return FAIL;
	}
	return SUCCESS;
}

int rtk_igmp_anyDevRateLimitUMC(uint32 isIpv6)
{

	int i;

	if(igmpSysdb.unknownFlood)
		return TRUE;
	
	for( i=0 ; i< IGMP_MAX_LAN_DEV_NUM; i++)
	{
		if(rtk_igmp_lanDevInfo[i].dev==NULL)
			continue;
		if(isIpv6)
		{
			if(rtk_igmp_lanDevInfo[i].devConf.igmp6.ratelimit_unkMc.rate)
				return TRUE;
		}
		else
		{
			if(rtk_igmp_lanDevInfo[i].devConf.igmp.ratelimit_unkMc.rate)
				return TRUE;
		}
	}

	for( i=0 ; i< IGMP_MAX_WAN_DEV_NUM; i++)
	{
		if(rtk_igmp_wanDevInfo[i].dev==NULL)
			continue;
		if(isIpv6)
		{
			if(rtk_igmp_wanDevInfo[i].devConf.igmp6.ratelimit_unkMc.rate)
				return TRUE;
		}
		else
		{
			if(rtk_igmp_wanDevInfo[i].devConf.igmp.ratelimit_unkMc.rate)
				return TRUE;
		}
	}


	return FALSE;
}


int rtk_igmp_anyDevDisSnooping(uint32 isIpv6)
{

	int i;

	if(igmpSysdb.unknownFlood)
		return TRUE;
	
	for( i=0 ; i< IGMP_MAX_LAN_DEV_NUM; i++)
	{
		if(rtk_igmp_lanDevInfo[i].dev==NULL)
			continue;
		if(isIpv6)
		{
			if(rtk_igmp_lanDevInfo[i].devConf.igmp6.igmp6SnoopingDisable)
				return TRUE;
		}
		else
		{
			if(rtk_igmp_lanDevInfo[i].devConf.igmp.igmpSnoopingDisable)
				return TRUE;
		}
	}

	for( i=0 ; i< IGMP_MAX_WAN_DEV_NUM; i++)
	{
		if(rtk_igmp_wanDevInfo[i].dev==NULL)
			continue;
		if(isIpv6)
		{
			if(rtk_igmp_wanDevInfo[i].devConf.igmp6.igmp6SnoopingDisable)
				return TRUE;
		}
		else
		{
			if(rtk_igmp_wanDevInfo[i].devConf.igmp.igmpSnoopingDisable)
				return TRUE;
		}
	}


	return FALSE;
}

int rtk_igmp_devConfig_set(int devifid , uint32 isIpv6 ,rt_igmpHook_control_list_t type , uint32 value)
{

	rtk_igmp_multicastDeviceInfo_t* devInfo = _rtk_igmp_devIfidx_to_devInfo(devifid);
	if(devInfo== NULL) return FAIL;


	switch ( type )
	{

	    case RT_SNOOPING_DISABLE:
			{
				uint32 statusChange=0;
				if(isIpv6)
				{
					if(devInfo->devConf.igmp6.igmp6SnoopingDisable!=value)
					{
						statusChange=1;
						devInfo->devConf.igmp6.igmp6SnoopingDisable=value;
					}
				}
				else
				{
					if(devInfo->devConf.igmp.igmpSnoopingDisable!=value)
					{
						statusChange=1;
						devInfo->devConf.igmp.igmpSnoopingDisable=value;
					}
				}
				if(statusChange)
				{
					rtk_igmp_flow_reflush_update(FAIL,NULL);
					if(rtk_igmp_anyDevDisSnooping(isIpv6))
					{
						if(hwCb.hwUnknownIpMcAction)
						{
							rtk_igmp_unknownMcCbEvt_t unknownCtrl;
							unknownCtrl.isIpv6=isIpv6;
							unknownCtrl.unknownAct=UNKNOWN_MC_TRAP;
							hwCb.hwUnknownIpMcAction(&unknownCtrl);
						}
					}
					else
					{
						if(hwCb.hwUnknownIpMcAction)
						{
							rtk_igmp_unknownMcCbEvt_t unknownCtrl;
							unknownCtrl.isIpv6=isIpv6;
							unknownCtrl.unknownAct=UNKNOWN_MC_DROP;
							hwCb.hwUnknownIpMcAction(&unknownCtrl);
						}
					}
				}
	 	   	}
	        break;

	    case RT_DROP_IGMPV1:
			if(!isIpv6)
				devInfo->devConf.igmp.igmpVerDrp_v1=value;
	        break;

	    case RT_DROP_IGMPV2:
			if(!isIpv6)
				devInfo->devConf.igmp.igmpVerDrp_v2=value;
	        break;

	    case RT_DROP_IGMPV3:
			if(!isIpv6)
				devInfo->devConf.igmp.igmpVerDrp_v3=value;
	        break;

	    case RT_IGNORE_IGMPV1:
			if(!isIpv6)
				devInfo->devConf.igmp.igmpVerIgr_v1=value;
	        break;

	    case RT_IGNORE_IGMPV2:
			if(!isIpv6)
				devInfo->devConf.igmp.igmpVerIgr_v2=value;
	        break;

		case RT_IGNORE_IGMPV3:
			if(!isIpv6)
				devInfo->devConf.igmp.igmpVerIgr_v3=value;
			break;

		case RT_DROP_MLDV1:
			if(isIpv6)
				devInfo->devConf.igmp6.igmp6VerDrp_v1=value;
			break;

		case RT_DROP_MLDV2:
			if(isIpv6)
				devInfo->devConf.igmp6.igmp6VerDrp_v2=value;
			break;

		case RT_IGNORE_MLDV1:
			if(isIpv6)
				devInfo->devConf.igmp6.igmp6VerIgr_v1=value;
			break;

		case RT_IGNORE_MLDV2:
			if(isIpv6)
				devInfo->devConf.igmp6.igmp6VerIgr_v2=value;
			break;

		case RT_REPORT_DROP:
			if(isIpv6)
				devInfo->devConf.igmp6.igmp6ReportDrop=value;
			else
				devInfo->devConf.igmp.igmpReportDrop=value;
			break;

		case RT_FAST_LEAVE:
			if(isIpv6)
				devInfo->devConf.igmp6.igmp6fastLeave=value;
			else
				devInfo->devConf.igmp.igmpfastLeave=value;
			break;

		case RT_CARE_SOURCE:
			if(isIpv6)
				devInfo->devConf.igmp6.mldv2CareSource=value;
			else
				devInfo->devConf.igmp.igmpv3CareSource=value;
			break;

		case RT_MAX_GROUP_SIZE:
			if(isIpv6)
				devInfo->devConf.igmp6.igmp6MaxGroupSize=value;
			else
				devInfo->devConf.igmp.igmpMaxGroupSize=value;
			break;

		case RT_MC_MEMBER_AGING_INTERVAL:
			if(isIpv6)
				devInfo->devConf.igmp6.igmp6McMemberAgingInterval=value;
			else
				devInfo->devConf.igmp.igmpMcMemberAgingInterval=value;
			break;
			
		case RT_MC_LAST_MEMBER_AGING_INTERVAL:
			if(isIpv6)
				devInfo->devConf.igmp6.igmp6McLastMemberAgingInterval=value;
			else
				devInfo->devConf.igmp.igmpMcLastMemberAgingInterval=value;
			break;

		case RT_RATE_LIMIT:
			if(isIpv6){
				devInfo->devConf.igmp6.ratelimit.rate=value;
				devInfo->devConf.igmp6.ratelimit.counter=0;
				devInfo->devConf.igmp6.ratelimit.cycle_start_time=jiffies;
			}else{
				devInfo->devConf.igmp.ratelimit.rate=value;
				devInfo->devConf.igmp.ratelimit.counter=0;
				devInfo->devConf.igmp.ratelimit.cycle_start_time=jiffies;
			}
			break;
		case RT_RATE_LIMIT_UMC:
			if(isIpv6){
				devInfo->devConf.igmp6.ratelimit_unkMc.rate=value;
				devInfo->devConf.igmp6.ratelimit_unkMc.counter=0;
				devInfo->devConf.igmp6.ratelimit_unkMc.cycle_start_time=jiffies;
			}else{
				devInfo->devConf.igmp.ratelimit_unkMc.rate=value;
				devInfo->devConf.igmp.ratelimit_unkMc.counter=0;
				devInfo->devConf.igmp.ratelimit_unkMc.cycle_start_time=jiffies;
			}
			break;

		case RT_FLUSH_CONF:
			if(isIpv6)
				rtk_igmp_reset_devConfig(devifid, 1);
			else
				rtk_igmp_reset_devConfig(devifid, 0);
			break;

		case RT_FLUSH_MDB:
			//clear data base
			rtk_igmp_clearDataBaseByDev(devifid);
			rtk_igmp_flow_reflush_update(FAIL,NULL);

			break;

		case RT_MAX_WEIGHT:
			if(isIpv6)
				devInfo->devConf.igmp6.igmp6MaxWeight=value;
			else
				devInfo->devConf.igmp.igmpMaxWeight=value;
			break;		

		case RT_DROP_OVER_WEIGHT:
			if(isIpv6)
				devInfo->devConf.igmp6.igmp6OverWeightDrop=value;
			else
				devInfo->devConf.igmp.igmpOverWeightDrop=value;
			break;		

		case RT_FORWARD_DISABLE:
			if(!isIpv6)
				devInfo->devConf.igmp.igmpForwardDisable=value;
			else
				devInfo->devConf.igmp6.igmp6ForwardDisable=value;
			break;

		case RT_DMAC_MC_TO_UC:
			if(!isIpv6)
				devInfo->devConf.igmp.igmpDmacMcToUc=value;
			else
				devInfo->devConf.igmp6.igmp6DmacMcToUc=value;
			rtk_igmp_flow_reflush_update_with_hw(FAIL,NULL);
			break;
			
		case RT_DIS_SRC_DEV_FILTER:
			if(!isIpv6)
				devInfo->devConf.igmp.igmpSrcDevFilterDis=value;
			else
				devInfo->devConf.igmp6.igmp6SrcDevFilterDis=value;
			rtk_igmp_flow_reflush_update_with_hw(FAIL,NULL);
			break;


	    default:
			IGMP_WARNING("not support type");
	}


	return SUCCESS;

}


int rtk_igmp_devConfig_get(int devifid , uint32 isIpv6 ,rt_igmpHook_control_list_t type , uint32* value)
{

	rtk_igmp_multicastDeviceInfo_t* devInfo = _rtk_igmp_devIfidx_to_devInfo(devifid);
	if(devInfo== NULL) return FAIL;

	switch ( type )
	{

	    case RT_SNOOPING_DISABLE:
			{
				if(isIpv6)
				{
					*value=devInfo->devConf.igmp6.igmp6SnoopingDisable;
				}
				else
				{
					*value=devInfo->devConf.igmp.igmpSnoopingDisable;
				}
	 	   	}
	        break;

	    case RT_DROP_IGMPV1:
			if(!isIpv6)
				*value=devInfo->devConf.igmp.igmpVerDrp_v1;
			else
				return FAIL;
	        break;

	    case RT_DROP_IGMPV2:
			if(!isIpv6)
				*value=devInfo->devConf.igmp.igmpVerDrp_v2;
			else
				return FAIL;
	        break;

	    case RT_DROP_IGMPV3:
			if(!isIpv6)
				*value=devInfo->devConf.igmp.igmpVerDrp_v3;
			else
				return FAIL;
	        break;

	    case RT_IGNORE_IGMPV1:
			if(!isIpv6)
				*value=devInfo->devConf.igmp.igmpVerIgr_v1;
			else
				return FAIL;
	        break;

	    case RT_IGNORE_IGMPV2:
			if(!isIpv6)
				*value=devInfo->devConf.igmp.igmpVerIgr_v2;
			else
				return FAIL;			
	        break;

		case RT_IGNORE_IGMPV3:
			if(!isIpv6)
				*value=devInfo->devConf.igmp.igmpVerIgr_v3;
			else
				return FAIL;			
			break;

		case RT_DROP_MLDV1:
			if(isIpv6)
				*value=devInfo->devConf.igmp6.igmp6VerDrp_v1;
			else
				return FAIL;			
			break;

		case RT_DROP_MLDV2:
			if(isIpv6)
				*value=devInfo->devConf.igmp6.igmp6VerDrp_v2;
			break;

		case RT_IGNORE_MLDV1:
			if(isIpv6)
				*value=devInfo->devConf.igmp6.igmp6VerIgr_v1;
			else
				return FAIL;			
			break;

		case RT_IGNORE_MLDV2:
			if(isIpv6)
				*value=devInfo->devConf.igmp6.igmp6VerIgr_v2;
			else
				return FAIL;			
			break;

		case RT_REPORT_DROP:
			if(isIpv6)
				*value=devInfo->devConf.igmp6.igmp6ReportDrop;
			else
				*value=devInfo->devConf.igmp.igmpReportDrop;
			break;

		case RT_FAST_LEAVE:
			if(isIpv6)
				*value=devInfo->devConf.igmp6.igmp6fastLeave;
			else
				*value=devInfo->devConf.igmp.igmpfastLeave;
			break;

		case RT_CARE_SOURCE:
			if(isIpv6)
				*value=devInfo->devConf.igmp6.mldv2CareSource;
			else
				*value=devInfo->devConf.igmp.igmpv3CareSource;
			break;

		case RT_MAX_GROUP_SIZE:
			if(isIpv6)
				*value=devInfo->devConf.igmp6.igmp6MaxGroupSize;
			else
				*value=devInfo->devConf.igmp.igmpMaxGroupSize;
			break;

		case RT_MC_MEMBER_AGING_INTERVAL:
			if(isIpv6)
				*value=devInfo->devConf.igmp6.igmp6McMemberAgingInterval;
			else
				*value=devInfo->devConf.igmp.igmpMcMemberAgingInterval;
			break;
			
		case RT_MC_LAST_MEMBER_AGING_INTERVAL:
			if(isIpv6)
				*value=devInfo->devConf.igmp6.igmp6McLastMemberAgingInterval;
			else
				*value=devInfo->devConf.igmp.igmpMcLastMemberAgingInterval;
			break;

		case RT_RATE_LIMIT:
			if(isIpv6){
				*value=devInfo->devConf.igmp6.ratelimit.rate;
				devInfo->devConf.igmp6.ratelimit.counter=0;
				devInfo->devConf.igmp6.ratelimit.cycle_start_time=jiffies;
			}else{
				*value=devInfo->devConf.igmp.ratelimit.rate;
				devInfo->devConf.igmp.ratelimit.counter=0;
				devInfo->devConf.igmp.ratelimit.cycle_start_time=jiffies;
			}
			break;
		case RT_RATE_LIMIT_UMC:
			if(isIpv6){
				*value=devInfo->devConf.igmp6.ratelimit_unkMc.rate;
				devInfo->devConf.igmp6.ratelimit_unkMc.counter=0;
				devInfo->devConf.igmp6.ratelimit_unkMc.cycle_start_time=jiffies;
			}else{
				*value=devInfo->devConf.igmp.ratelimit_unkMc.rate;
				devInfo->devConf.igmp.ratelimit_unkMc.counter=0;
				devInfo->devConf.igmp.ratelimit_unkMc.cycle_start_time=jiffies;
			}
			break;

		case RT_FORWARD_DISABLE:
			if(!isIpv6)
				*value=devInfo->devConf.igmp.igmpForwardDisable;
			else
				*value=devInfo->devConf.igmp6.igmp6ForwardDisable;
			break;

		case RT_DMAC_MC_TO_UC:
			if(!isIpv6)
				*value=devInfo->devConf.igmp.igmpDmacMcToUc;
			else
				*value=devInfo->devConf.igmp6.igmp6DmacMcToUc;
			break;


	    default:
			IGMP_WARNING("not support type:%d to get",type);
			return FAIL;
	}


	return SUCCESS;

}


int show_igmp_blackList_all(struct seq_file *s, void *v)
{
	rtk_igmp_blackListEntry_t *black_entry;

	PROC_PRINTF("\n");
	list_for_each_entry(black_entry,&blackListHead,entry)
	{
		
		PROC_PRINTF("\t[%d](ref.%d)",black_entry->blackListIdx,black_entry->blackRefCnt);

		PROC_PRINTF(" %s ",black_entry->patten.isIpv6?"IPv6":"IPv4");

		if(black_entry->patten.smacChk)
		{
			PROC_PRINTF("smac:%pM smacM:%pM ",black_entry->patten.smac,black_entry->patten.smacMask);
		}
		PROC_PRINTF("\n");
	
	}

	return SUCCESS;
}



int show_igmp_blackList(struct seq_file *s, void *v,rtk_igmp_DevConfdb_t *devconf)
{
	rtk_igmp_blackListEntry_t *black_entry;
	rtk_igmp_idxEntry_t *blackIdx;
	
	black_entry= &blackListHeadEntry;

	
	PROC_PRINTF("\tBlackListv4: \n");
	list_for_each_entry(blackIdx,&devconf->igmp.black,entry)
	{
		list_for_each_entry_continue(black_entry,&blackListHead,entry)
		{
			//not this entry continue
			if(black_entry->blackListIdx !=blackIdx->entryIdx)
				continue;
			PROC_PRINTF("\t[%d](ref.%d)",black_entry->blackListIdx,black_entry->blackRefCnt);

			if(black_entry->patten.smacChk)
			{
				PROC_PRINTF("smac:%pM smacM:%pM ",black_entry->patten.smac,black_entry->patten.smacMask);
			}
			PROC_PRINTF("\n");

			break;
		}
	}

	PROC_PRINTF("\tBlackListv6: \n");
	list_for_each_entry(blackIdx,&devconf->igmp6.black,entry)
	{
		list_for_each_entry_continue(black_entry,&blackListHead,entry)
		{
			//not this entry continue
			if(black_entry->blackListIdx !=blackIdx->entryIdx)
				continue;
			PROC_PRINTF("\t[%d]",black_entry->blackListIdx);

			if(black_entry->patten.smacChk)
			{
				PROC_PRINTF("smac:%pM smacM:%pM ",black_entry->patten.smac,black_entry->patten.smacMask);
			}
			PROC_PRINTF("\n");

			break;
		}
	}

	return SUCCESS;

}


int show_igmp_whiteList_all(struct seq_file *s, void *v)
{
	rtk_igmp_whiteListEntry_t *white_entry;

	PROC_PRINTF("\n");
	list_for_each_entry(white_entry,&whiteListHead,entry)
	{
		
		PROC_PRINTF("\t[%d](ref.%d)",white_entry->whiteListIdx,white_entry->whiteRefCnt);
		if(white_entry->patten.gipChk)
		{
			if(white_entry->patten.isIpv6)
				PROC_PRINTF("gip:%pI6 gipE:%pI6 ",white_entry->patten.wlGroupIpStart,white_entry->patten.wlGroupIpEnd);
			else
				PROC_PRINTF("gip:%pI4h gipE:%pI4h ",white_entry->patten.wlGroupIpStart,white_entry->patten.wlGroupIpEnd);
		}
		if(white_entry->patten.sipChk)
		{
			if(white_entry->patten.isIpv6)
				PROC_PRINTF("sip:%pI6 sipEnd:%pI6 ",white_entry->patten.wlSipStart,white_entry->patten.wlSipEnd);
			else
				PROC_PRINTF("sip:%pI4h sipEnd:%pI4h ",white_entry->patten.wlSipStart,white_entry->patten.wlSipEnd);
		}
		if(white_entry->patten.mcSipChk)
		{
			if(white_entry->patten.isIpv6)
				PROC_PRINTF("mcSip:%pI6 mcSipEnd:%pI6  %s",white_entry->patten.wlMcSip,white_entry->patten.wlMcSipEnd,white_entry->patten.mcSipChkIgnoreNonIGMPv3MLDv2==1?"[ignore non mldv2]":"");
			else
				PROC_PRINTF("mcSip:%pI4h mcSipEnd:%pI4h %s",white_entry->patten.wlMcSip,white_entry->patten.wlMcSipEnd,white_entry->patten.mcSipChkIgnoreNonIGMPv3MLDv2==1?"[ignore non igmpv3]":"");
		}
		if(white_entry->patten.smacChk)
		{
	
			PROC_PRINTF("smac:%pM smacM:%pM ",white_entry->patten.smac,white_entry->patten.smacMask);
		}
		PROC_PRINTF("\n");
	
	}

	return SUCCESS;
}



int show_igmp_whiteList(struct seq_file *s, void *v,rtk_igmp_DevConfdb_t *devconf)
{


	rtk_igmp_whiteListEntry_t *white_entry;
	rtk_igmp_idxEntry_t *whiteIdx;
	
	white_entry= &whiteListHeadEntry;
	PROC_PRINTF("\tWhiteListv4: \n");

	list_for_each_entry(whiteIdx,&devconf->igmp.white,entry)
	{
		list_for_each_entry_continue(white_entry,&whiteListHead,entry)
		{
			//not this entry continue
			if(white_entry->whiteListIdx !=whiteIdx->entryIdx)
				continue;

			PROC_PRINTF("\t[%d]",white_entry->whiteListIdx);
			if(white_entry->patten.gipChk)
			{
				PROC_PRINTF("gip:%pI4h gipE:%pI4h ",white_entry->patten.wlGroupIpStart,white_entry->patten.wlGroupIpEnd);
			}
			if(white_entry->patten.sipChk)
			{
				PROC_PRINTF("sip:%pI4h sipEnd:%pI4h ",white_entry->patten.wlSipStart,white_entry->patten.wlSipEnd);
			}
			if(white_entry->patten.mcSipChk)
			{
				PROC_PRINTF("mcSip:%pI4h mcSipEnd:%pI4h %s",white_entry->patten.wlMcSip,white_entry->patten.wlMcSipEnd,white_entry->patten.mcSipChkIgnoreNonIGMPv3MLDv2==1?"[ignore non igmpv3]":"");
			}
			if(white_entry->patten.smacChk)
			{

				PROC_PRINTF("smac:%pM smacM:%pM ",white_entry->patten.smac,white_entry->patten.smacMask);
			}
			PROC_PRINTF("\n");

			break;
		}
	}
	

	white_entry= &whiteListHeadEntry;
	PROC_PRINTF("\tWhiteListv6: \n");

	list_for_each_entry(whiteIdx,&devconf->igmp6.white,entry)
	{

		list_for_each_entry_continue(white_entry,&whiteListHead,entry)
		{
			//not this entry continue
			if(white_entry->whiteListIdx !=whiteIdx->entryIdx)
				continue;

			PROC_PRINTF("\t[%d]",white_entry->whiteListIdx);
			if(white_entry->patten.gipChk)
			{
				if(white_entry->patten.isIpv6)
					PROC_PRINTF("gip:%pI6 gipE:%pI6 ",white_entry->patten.wlGroupIpStart,white_entry->patten.wlGroupIpEnd);
			}
			if(white_entry->patten.sipChk)
			{
				if(white_entry->patten.isIpv6)
					PROC_PRINTF("sip:%pI6 sipEnd:%pI6 ",white_entry->patten.wlSipStart,white_entry->patten.wlSipEnd);
			}
			if(white_entry->patten.mcSipChk)
			{
				if(white_entry->patten.isIpv6)
					PROC_PRINTF("mcSip:%pI6 mcSipEnd:%pI6  %s",white_entry->patten.wlMcSip,white_entry->patten.wlMcSipEnd,white_entry->patten.mcSipChkIgnoreNonIGMPv3MLDv2==1?"[ignore non mldv2]":"");
			}
			if(white_entry->patten.smacChk)
			{

				PROC_PRINTF("smac:%pM smacM:%pM ",white_entry->patten.smac,white_entry->patten.smacMask);
			}
			PROC_PRINTF("\n");

			break;
		}
	}
	


	return SUCCESS;


}

int rtk_igmp_dump_devConfig(int ifidx,struct seq_file *s, void *v)
{
	rtk_igmp_DevConfdb_t *devconf;
	int i;
	
	PROC_PRINTF("=================================================\n");
	PROC_PRINTF("BR INFO:\n");
	for( i=0 ; i< IGMP_MAX_BR_MODULE_NUM; i++)
	{
		if(!rtk_igmp_mCastModuleArray[i].validBit)
			continue;
		if(ifidx !=FAIL && rtk_igmp_mCastModuleArray[i].brDevInfo.ifindex!=ifidx)
			continue;
		PROC_PRINTF("BrDev:[%s] ifidx=%d \n\tGROUP_LIMIT:[total/ipv4/ipv6][%d/%d/%d] CLIENT_LIMIT[total/ipv4/ipv6][%d/%d/%d] PER_CLIENT_JOIN_GROUP_LIMIT[ipv4/ipv6][%d/%d]\n",rtk_igmp_mCastModuleArray[i].brDevInfo.devName,rtk_igmp_mCastModuleArray[i].brDevInfo.ifindex
			,rtk_igmp_mCastModuleArray[i].brDevInfo.brDevConf.sysMaxGroupSize,rtk_igmp_mCastModuleArray[i].brDevInfo.brDevConf.sysIp4MaxGroupSize,rtk_igmp_mCastModuleArray[i].brDevInfo.brDevConf.sysIp6MaxGroupSize
			,rtk_igmp_mCastModuleArray[i].brDevInfo.brDevConf.sysMaxClientSize,rtk_igmp_mCastModuleArray[i].brDevInfo.brDevConf.sysIp4MaxClientSize,rtk_igmp_mCastModuleArray[i].brDevInfo.brDevConf.sysIp6MaxClientSize
			,rtk_igmp_mCastModuleArray[i].brDevInfo.brDevConf.perIp4ClientMaxJoinGroupSize,rtk_igmp_mCastModuleArray[i].brDevInfo.brDevConf.perIp6ClientMaxJoinGroupSize);
		PROC_PRINTF("MAX_WEIGHT:[ipv4/ipv6][%d/%d] DROP_OVER_WEIGHT[ipv4/ipv6][%d/%d]",
			rtk_igmp_mCastModuleArray[i].brDevInfo.brDevConf.sysIp4MaxWeight,rtk_igmp_mCastModuleArray[i].brDevInfo.brDevConf.sysIp6MaxWeight,
			rtk_igmp_mCastModuleArray[i].brDevInfo.brDevConf.sysIp4OverWeightDrop,rtk_igmp_mCastModuleArray[i].brDevInfo.brDevConf.sysIp6OverWeightDrop);
		PROC_PRINTF("\n");
	}


	PROC_PRINTF("=================================================\n");
	PROC_PRINTF("WAN INFO:\n");
	for( i=0 ; i< IGMP_MAX_WAN_DEV_NUM; i++)
	{
		if(rtk_igmp_wanDevInfo[i].dev==NULL)
			continue;
		if(ifidx !=FAIL && rtk_igmp_wanDevInfo[i].ifindex!=ifidx)
			continue;

		PROC_PRINTF("WAN-Dev:[%s]  ifidx=%d ",rtk_igmp_wanDevInfo[i].devName,rtk_igmp_wanDevInfo[i].ifindex);
		if(rtk_igmp_wanDevInfo[i].bind2BrModuleIdx!=FAIL)
			PROC_PRINTF("Mapping-to [%s] \n",rtk_igmp_mCastModuleArray[rtk_igmp_wanDevInfo[i].bind2BrModuleIdx].brDevInfo.devName);
		else
			PROC_PRINTF("\n");

		devconf = &rtk_igmp_wanDevInfo[i].devConf;
		PROC_PRINTF("\tIGMP:\n");
		PROC_PRINTF("\t");

		if(devconf->igmp.igmpVerIgr_v1)
			PROC_PRINTF("/IGNORE_IGMPV1");
		if(devconf->igmp.igmpVerIgr_v2)
			PROC_PRINTF("/IGNORE_IGMPV2");
		if(devconf->igmp.igmpVerIgr_v3)
			PROC_PRINTF("/IGNORE_IGMPV3");
		if(devconf->igmp.igmpVerDrp_v1)
			PROC_PRINTF("/DROP_IGMPV1");
		if(devconf->igmp.igmpVerDrp_v2)
			PROC_PRINTF("/DROP_IGMPV2");
		if(devconf->igmp.igmpVerDrp_v3)
			PROC_PRINTF("/DROP_IGMPV3");
		if(devconf->igmp.igmpReportDrop)
			PROC_PRINTF("/REPORT_DROP");
		if(devconf->igmp.igmpSnoopingDisable)
			PROC_PRINTF("/SNOOPING_DISABLE");
		if(devconf->igmp.igmpForwardDisable)
			PROC_PRINTF("/FORWARD_DISABLE");		
		if(devconf->igmp.igmpfastLeave)
			PROC_PRINTF("/FAST_LEAVE");
		if(devconf->igmp.igmpv3CareSource)
			PROC_PRINTF("/CARE_SOURCE");
		if(devconf->igmp.igmpMaxGroupSize)
			PROC_PRINTF("/MAX_GROUP_SIZE:%d",devconf->igmp.igmpMaxGroupSize);
		if(devconf->igmp.igmpMcMemberAgingInterval)
			PROC_PRINTF("/MC_MEMBER_AGING_INTERVAL:%d",devconf->igmp.igmpMcMemberAgingInterval);
		if(devconf->igmp.igmpMcLastMemberAgingInterval)
			PROC_PRINTF("/MC_LAST_MEMBER_AGING_INTERVAL:%d",devconf->igmp.igmpMcLastMemberAgingInterval);		
		if(devconf->igmp.ratelimit.rate)
			PROC_PRINTF("/RATE_LIMIT:%d",devconf->igmp.ratelimit.rate);
		if(devconf->igmp.ratelimit_unkMc.rate)
			PROC_PRINTF("/RATE_LIMIT_UMC:%d",devconf->igmp.ratelimit_unkMc.rate);
		if(devconf->igmp.igmpMaxWeight)
			PROC_PRINTF("/MAX_WEIGHT:%d",devconf->igmp.igmpMaxWeight);		
		if(devconf->igmp.igmpOverWeightDrop==0)
			PROC_PRINTF("/DROP_OVER_WEIGHT:%d",devconf->igmp.igmpOverWeightDrop);
		if(devconf->igmp.igmpDmacMcToUc)
			PROC_PRINTF("/DMAC_MC_TO_UC:%d",devconf->igmp.igmpDmacMcToUc);
		if(devconf->igmp.igmpSrcDevFilterDis)
			PROC_PRINTF("/DIS_SRC_DEV_FILTER:%d",devconf->igmp.igmpSrcDevFilterDis);

		PROC_PRINTF("\n");
		PROC_PRINTF("\tIGMP6:\n");
		PROC_PRINTF("\t");
		if(devconf->igmp6.igmp6VerIgr_v1)
			PROC_PRINTF("/IGNORE_MLDV1");
		if(devconf->igmp6.igmp6VerIgr_v2)
			PROC_PRINTF("/IGNORE_MLDV2");
		if(devconf->igmp6.igmp6VerDrp_v1)
			PROC_PRINTF("/DROP_MLDV1");
		if(devconf->igmp6.igmp6VerDrp_v2)
			PROC_PRINTF("/DROP_MLDV2");
		if(devconf->igmp6.igmp6ReportDrop)
			PROC_PRINTF("/REPORT_DROP");
		if(devconf->igmp6.igmp6SnoopingDisable)
			PROC_PRINTF("/SNOOPING_DISABLE");
		if(devconf->igmp6.igmp6ForwardDisable)
			PROC_PRINTF("/FORWARD_DISABLE");				
		if(devconf->igmp6.igmp6fastLeave)
			PROC_PRINTF("/FAST_LEAVE");
		if(devconf->igmp6.mldv2CareSource)
			PROC_PRINTF("/CARE_SOURCE");
		if(devconf->igmp6.igmp6MaxGroupSize)
			PROC_PRINTF("/MAX_GROUP_SIZE:%d",devconf->igmp6.igmp6MaxGroupSize);
		if(devconf->igmp6.igmp6McMemberAgingInterval)
			PROC_PRINTF("/MC_MEMBER_AGING_INTERVAL:%d",devconf->igmp6.igmp6McMemberAgingInterval);
		if(devconf->igmp6.igmp6McLastMemberAgingInterval)
			PROC_PRINTF("/MC_LAST_MEMBER_AGING_INTERVAL:%d",devconf->igmp6.igmp6McLastMemberAgingInterval);		
		if(devconf->igmp6.ratelimit.rate)
			PROC_PRINTF("/RATE_LIMIT:%d",devconf->igmp6.ratelimit.rate);
		if(devconf->igmp6.ratelimit_unkMc.rate)
			PROC_PRINTF("/RATE_LIMIT_UMC:%d",devconf->igmp6.ratelimit_unkMc.rate);		
		if(devconf->igmp6.igmp6MaxWeight)
			PROC_PRINTF("/MAX_WEIGHT:%d",devconf->igmp6.igmp6MaxWeight);		
		if(devconf->igmp6.igmp6OverWeightDrop==0)
			PROC_PRINTF("/DROP_OVER_WEIGHT:%d",devconf->igmp6.igmp6OverWeightDrop);
		if(devconf->igmp6.igmp6DmacMcToUc)
			PROC_PRINTF("/DMAC_MC_TO_UC:%d",devconf->igmp6.igmp6DmacMcToUc);
		if(devconf->igmp6.igmp6SrcDevFilterDis)
			PROC_PRINTF("/DIS_SRC_DEV_FILTER:%d",devconf->igmp6.igmp6SrcDevFilterDis);

		PROC_PRINTF("\n");

		show_igmp_whiteList(s,v,devconf);
		show_igmp_blackList(s,v,devconf);
	}

	PROC_PRINTF("=================================================\n");
	PROC_PRINTF("LAN INFO:\n");
	for( i=0 ; i< IGMP_MAX_LAN_DEV_NUM; i++)
	{
		if(rtk_igmp_lanDevInfo[i].dev==NULL)
			continue;
		if(ifidx !=FAIL && rtk_igmp_lanDevInfo[i].ifindex!=ifidx)
			continue;

		PROC_PRINTF("LAN-Dev:[%s]  ifidx=%d ",rtk_igmp_lanDevInfo[i].devName,rtk_igmp_lanDevInfo[i].ifindex);
		PROC_PRINTF("\n");

		devconf = &rtk_igmp_lanDevInfo[i].devConf;
		PROC_PRINTF("\tIGMP:\n");
		PROC_PRINTF("\t");

		if(devconf->igmp.igmpVerIgr_v1)
			PROC_PRINTF("/IGNORE_IGMPV1");
		if(devconf->igmp.igmpVerIgr_v2)
			PROC_PRINTF("/IGNORE_IGMPV2");
		if(devconf->igmp.igmpVerIgr_v3)
			PROC_PRINTF("/IGNORE_IGMPV3");
		if(devconf->igmp.igmpVerDrp_v1)
			PROC_PRINTF("/DROP_IGMPV1");
		if(devconf->igmp.igmpVerDrp_v2)
			PROC_PRINTF("/DROP_IGMPV2");
		if(devconf->igmp.igmpVerDrp_v3)
			PROC_PRINTF("/DROP_IGMPV3");
		if(devconf->igmp.igmpReportDrop)
			PROC_PRINTF("/REPORT_DROP");
		if(devconf->igmp.igmpSnoopingDisable)
			PROC_PRINTF("/SNOOPING_DISABLE");
		if(devconf->igmp.igmpForwardDisable)
			PROC_PRINTF("/FORWARD_DISABLE");			
		if(devconf->igmp.igmpfastLeave)
			PROC_PRINTF("/FAST_LEAVE");
		if(devconf->igmp.igmpv3CareSource)
			PROC_PRINTF("/CARE_SOURCE");
		if(devconf->igmp.igmpMaxGroupSize)
			PROC_PRINTF("/MAX_GROUP_SIZE:%d",devconf->igmp.igmpMaxGroupSize);
		if(devconf->igmp.igmpMcMemberAgingInterval)
			PROC_PRINTF("/MC_MEMBER_AGING_INTERVAL:%d",devconf->igmp.igmpMcMemberAgingInterval);
		if(devconf->igmp.igmpMcLastMemberAgingInterval)
			PROC_PRINTF("/MC_LAST_MEMBER_AGING_INTERVAL:%d",devconf->igmp.igmpMcLastMemberAgingInterval);		
		if(devconf->igmp.ratelimit.rate)
			PROC_PRINTF("/RATE_LIMIT:%d",devconf->igmp.ratelimit.rate);
		if(devconf->igmp.ratelimit_unkMc.rate)
			PROC_PRINTF("/RATE_LIMIT_UMC:%d",devconf->igmp.ratelimit_unkMc.rate);		
		if(devconf->igmp.igmpMaxWeight)
			PROC_PRINTF("/MAX_WEIGHT:%d",devconf->igmp.igmpMaxWeight);		
		if(devconf->igmp.igmpOverWeightDrop==0)
			PROC_PRINTF("/DROP_OVER_WEIGHT:%d",devconf->igmp.igmpOverWeightDrop);
		if(devconf->igmp.igmpDmacMcToUc)
			PROC_PRINTF("/DMAC_MC_TO_UC:%d",devconf->igmp.igmpDmacMcToUc);
		if(devconf->igmp.igmpSrcDevFilterDis)
			PROC_PRINTF("/DIS_SRC_DEV_FILTER:%d",devconf->igmp.igmpSrcDevFilterDis);

		PROC_PRINTF("\n");
		PROC_PRINTF("\tIGMP6:\n");
		PROC_PRINTF("\t");
		if(devconf->igmp6.igmp6VerIgr_v1)
			PROC_PRINTF("/IGNORE_MLDV1");
		if(devconf->igmp6.igmp6VerIgr_v2)
			PROC_PRINTF("/IGNORE_MLDV2");
		if(devconf->igmp6.igmp6VerDrp_v1)
			PROC_PRINTF("/DROP_MLDV1");
		if(devconf->igmp6.igmp6VerDrp_v2)
			PROC_PRINTF("/DROP_MLDV2");
		if(devconf->igmp6.igmp6ReportDrop)
			PROC_PRINTF("/REPORT_DROP");
		if(devconf->igmp6.igmp6SnoopingDisable)
			PROC_PRINTF("/SNOOPING_DISABLE");
		if(devconf->igmp6.igmp6ForwardDisable)
			PROC_PRINTF("/FORWARD_DISABLE");			
		if(devconf->igmp6.igmp6fastLeave)
			PROC_PRINTF("/FAST_LEAVE");
		if(devconf->igmp6.mldv2CareSource)
			PROC_PRINTF("/CARE_SOURCE");
		if(devconf->igmp6.igmp6MaxGroupSize)
			PROC_PRINTF("/MAX_GROUP_SIZE:%d",devconf->igmp6.igmp6MaxGroupSize);
		if(devconf->igmp6.igmp6McMemberAgingInterval)
			PROC_PRINTF("/MC_MEMBER_AGING_INTERVAL:%d",devconf->igmp6.igmp6McMemberAgingInterval);
		if(devconf->igmp6.igmp6McLastMemberAgingInterval)
			PROC_PRINTF("/MC_LAST_MEMBER_AGING_INTERVAL:%d",devconf->igmp6.igmp6McLastMemberAgingInterval);		
		if(devconf->igmp6.ratelimit.rate)
			PROC_PRINTF("/RATE_LIMIT:%d",devconf->igmp6.ratelimit.rate);
		if(devconf->igmp6.ratelimit_unkMc.rate)
			PROC_PRINTF("/RATE_LIMIT_UMC:%d",devconf->igmp6.ratelimit_unkMc.rate);		
		if(devconf->igmp6.igmp6MaxWeight)
			PROC_PRINTF("/MAX_WEIGHT:%d",devconf->igmp6.igmp6MaxWeight);		
		if(devconf->igmp6.igmp6OverWeightDrop==0)
			PROC_PRINTF("/DROP_OVER_WEIGHT:%d",devconf->igmp6.igmp6OverWeightDrop);			
		if(devconf->igmp6.igmp6DmacMcToUc)
			PROC_PRINTF("/DMAC_MC_TO_UC:%d",devconf->igmp6.igmp6DmacMcToUc);
		if(devconf->igmp6.igmp6SrcDevFilterDis)
			PROC_PRINTF("/DIS_SRC_DEV_FILTER:%d",devconf->igmp6.igmp6SrcDevFilterDis);

		PROC_PRINTF("\n");

		show_igmp_whiteList(s,v,devconf);
		show_igmp_blackList(s,v,devconf);

	}

	return SUCCESS;
}


//dump ALL
int rtk_igmp_dump_allDevConfig(struct seq_file *s, void *v)
{
	return rtk_igmp_dump_devConfig(FAIL,s,v);
}

int rtk_igmp_dump_memoryUsed(struct seq_file *s, void *v)
{
	PROC_PRINTF("MallocMemory              = %u byte\n",igmpTotalAlloc);
	PROC_PRINTF("rtk_igmp_mCastModuleArray = %lu byte\n",sizeof(rtk_igmp_mCastModuleArray));
	PROC_PRINTF("rtk_igmp_wanDevInfo       = %lu byte\n",sizeof(rtk_igmp_wanDevInfo));
	PROC_PRINTF("rtk_igmp_lanDevInfo       = %lu byte\n",sizeof(rtk_igmp_lanDevInfo));
	PROC_PRINTF("clientEntryPool           = %lu byte\n",sizeof(clientEntryPool));
	PROC_PRINTF("groupEntryPool            = %lu byte\n",sizeof(groupEntryPool));
	PROC_PRINTF("sourceEntryPool           = %lu byte\n",sizeof(sourceEntryPool));
	PROC_PRINTF("mcastFlowEntryPool        = %lu byte\n",sizeof(mcastFlowEntryPool));
	PROC_PRINTF("gorup2Ps                  = %lu byte\n",sizeof(gorup2Ps));
	PROC_PRINTF("groupWeight               = %lu byte\n",sizeof(groupWeight));

	return SUCCESS;
}




void _rtk_igmp_showValidToDevInfo(struct seq_file *s, void *v,rtk_igmp_flowToDevInfo_t *flowToDevInfo)
{
	int i;
	rtk_igmp_multicastDeviceInfo_t *pDevInfo;

	for (i=0 ;i< IGMP_MAX_DEV_NUM;i++)
	{
		pDevInfo = (_rtk_igmp_devIfidx_to_devInfo(flowToDevInfo[i].devIfIdx));
		if(pDevInfo == NULL)
			continue;

		if(flowToDevInfo[i].valid)
			PROC_PRINTF("  %s[%d]%s%s/",pDevInfo->devName,flowToDevInfo[i].devIfIdx,pDevInfo->devConf.igmp.igmpSnoopingDisable?"[disIGMP]":"",pDevInfo->devConf.igmp6.igmp6SnoopingDisable?"[disMLD]":"");
		else
			break;
	}
	return;
}

int rtk_igmp_allDump(struct seq_file *s, void *v)
{
	return rtk_igmp_devDump(FAIL,s,v);
}

int rtk_igmp_devDump(int ifidx,struct seq_file *s, void *v)
{
	int32 moduleIndex;
	int32 hashIndex,clientCnt;
	struct rtk_igmp_groupEntry *groupEntryPtr;
	struct rtk_igmp_clientEntry* clientEntry=NULL;
	struct rtk_igmp_sourceEntry *sourceEntryPtr;
	int32 flowCnt;
	struct rtk_igmp_mcastFlowEntry *mcastFlowEntry=NULL;

	PROC_PRINTF("IGMP Snooping:\n");
	PROC_PRINTF("=================================================\n");

	for( moduleIndex=0 ; moduleIndex< IGMP_MAX_BR_MODULE_NUM; moduleIndex++)
	{
		if(rtk_igmp_mCastModuleArray[moduleIndex].validBit==0)
			continue;

		PROC_PRINTF("BR-Dev:[%s]  ifidx=%d  Mac[%pM] combineID[%d]\n",rtk_igmp_mCastModuleArray[moduleIndex].brDevInfo.devName,rtk_igmp_mCastModuleArray[moduleIndex].brDevInfo.ifindex,rtk_igmp_mCastModuleArray[moduleIndex].brDevInfo.rtk_igmp_gatewayMac,rtk_igmp_mCastModuleArray[moduleIndex].brDevInfo.brDevConf.combineId);


		/* IPv4 */
		for (hashIndex=0;hashIndex<IGMP_HASH_TBL_SIZE;hashIndex++)
		{
			groupEntryPtr=rtk_igmp_mCastModuleArray[moduleIndex].rtk_igmp_ipv4HashTable[hashIndex];
			while (groupEntryPtr!=NULL)
			{

				PROC_PRINTF( "	 [%d] Group address:%pI4h vlanID[%d] svlanID[%d]\n",groupEntryPtr->groupIdx,groupEntryPtr->groupAddr,groupEntryPtr->vlanId,groupEntryPtr->svlanId);

				clientEntry=groupEntryPtr->clientList;

				clientCnt=0;
				while (clientEntry!=NULL)
				{

					clientCnt++;
					{
						if(_rtk_igmp_devIfidx_to_devInfo(clientEntry->inIfidx))
							PROC_PRINTF( "		  <%d>%pI4h\\Mac:%pM\\ %s[%d]\\IGMPv%d\\",clientCnt, (clientEntry->clientAddr),clientEntry->clientMacAddr,_rtk_igmp_devIfidx_to_devInfo(clientEntry->inIfidx)->devName,clientEntry->inIfidx, (clientEntry->igmpVersion-IGMP_V1 + 1));
						else
							PROC_PRINTF( "		  <%d>%pI4h\\Mac:%pM\\ none[%d]\\IGMPv%d\\",clientCnt, (clientEntry->clientAddr),clientEntry->clientMacAddr,clientEntry->inIfidx, (clientEntry->igmpVersion-IGMP_V1 + 1));
					}

					PROC_PRINTF( "%s",(clientEntry->groupFilterTimer>rtk_igmp_sysUpSeconds)?"EXCLUDE":"INCLUDE");
					if(clientEntry->groupFilterTimer>rtk_igmp_sysUpSeconds)
					{
						PROC_PRINTF( ":%ds",clientEntry->groupFilterTimer-rtk_igmp_sysUpSeconds);
					}
					else
					{
						PROC_PRINTF( ":0s");
					}
					//PROC_PRINTF( ":%s ",clientEntry->pppoePassthroughEntry?"PPPoE Passthrough Entry":"");
					if(clientEntry->reportStagif)
						PROC_PRINTF( " STag:%d",clientEntry->reportSVlanId);
					else
						PROC_PRINTF( " unSTag");

					if(clientEntry->reportCtagif)
						PROC_PRINTF( " CTag:%d",clientEntry->reportVlanId);
					else
						PROC_PRINTF( " unCTag");

					if(clientEntry->extraPortInfo.valid)
						PROC_PRINTF( " ExtPortId:%d",clientEntry->extraPortInfo.devExtraPortId);
					
					sourceEntryPtr=clientEntry->sourceList;
					if(sourceEntryPtr!=NULL)
					{
						PROC_PRINTF( "\\source list:");
					}

					while(sourceEntryPtr!=NULL)
					{
						PROC_PRINTF( "%pI4h:",sourceEntryPtr->sourceAddr);

						if(sourceEntryPtr->portTimer>rtk_igmp_sysUpSeconds)
						{
							PROC_PRINTF( "%ds",sourceEntryPtr->portTimer-rtk_igmp_sysUpSeconds);
						}
						else
						{
							PROC_PRINTF( "0s");
						}

						if(sourceEntryPtr->next!=NULL)
						{
							PROC_PRINTF( ", ");
						}

						sourceEntryPtr=sourceEntryPtr->next;
					}


					PROC_PRINTF( "\n");
					clientEntry = clientEntry->next;
				}

				PROC_PRINTF( "\n");
				groupEntryPtr=groupEntryPtr->next;
			}

		}

		/* IPv6 */
		for (hashIndex=0;hashIndex<IGMP_HASH_TBL_SIZE;hashIndex++)
		{
			groupEntryPtr=rtk_igmp_mCastModuleArray[moduleIndex].rtk_igmp_ipv6HashTable[hashIndex];
			while (groupEntryPtr!=NULL)
			{
				PROC_PRINTF( "	 [%d] Group address:%pI6 vlanID[%d] svlanID[%d]\n",groupEntryPtr->groupIdx,groupEntryPtr->groupAddr,groupEntryPtr->vlanId,groupEntryPtr->svlanId);

				clientEntry=groupEntryPtr->clientList;

				clientCnt=0;
				while (clientEntry!=NULL)
				{

					clientCnt++;
					{
						if(_rtk_igmp_devIfidx_to_devInfo(clientEntry->inIfidx))
							PROC_PRINTF( "		  <%d>%pI6\\Mac:%pM\\ %s[%d]\\MLDv%d\\",clientCnt, (clientEntry->clientAddr), (clientEntry->clientMacAddr),_rtk_igmp_devIfidx_to_devInfo(clientEntry->inIfidx)->devName,clientEntry->inIfidx, (clientEntry->igmpVersion-MLD_V1 + 1));
						else
							PROC_PRINTF( "		  <%d>%pI6\\Mac:%pM\\ none[%d]\\MLDv%d\\",clientCnt, (clientEntry->clientAddr), (clientEntry->clientMacAddr),clientEntry->inIfidx, (clientEntry->igmpVersion-MLD_V1 + 1));
					}

					PROC_PRINTF( "%s",(clientEntry->groupFilterTimer>rtk_igmp_sysUpSeconds)?"EXCLUDE":"INCLUDE");
					if(clientEntry->groupFilterTimer>rtk_igmp_sysUpSeconds)
					{
						PROC_PRINTF( ":%ds",clientEntry->groupFilterTimer-rtk_igmp_sysUpSeconds);
					}
					else
					{
						PROC_PRINTF( ":0s");
					}
					//PROC_PRINTF( ":%s ",clientEntry->pppoePassthroughEntry?"PPPoE Passthrough Entry":"");
					if(clientEntry->reportStagif)
						PROC_PRINTF( " STag:%d",clientEntry->reportSVlanId);
					else
						PROC_PRINTF( " unSTag");
					
					if(clientEntry->reportCtagif)
						PROC_PRINTF( " CTag:%d",clientEntry->reportVlanId);
					else
						PROC_PRINTF( " unCTag");

					if(clientEntry->extraPortInfo.valid)
						PROC_PRINTF( " ExtPortId:%d",clientEntry->extraPortInfo.devExtraPortId);

					sourceEntryPtr=clientEntry->sourceList;
					if(sourceEntryPtr!=NULL)
					{
						PROC_PRINTF( "\\source list:");
					}

					while(sourceEntryPtr!=NULL)
					{
						PROC_PRINTF( "%pI6:",sourceEntryPtr->sourceAddr);

						if(sourceEntryPtr->portTimer>rtk_igmp_sysUpSeconds)
						{
							PROC_PRINTF( "%ds",sourceEntryPtr->portTimer-rtk_igmp_sysUpSeconds);
						}
						else
						{
							PROC_PRINTF( "0s");
						}

						if(sourceEntryPtr->next!=NULL)
						{
							PROC_PRINTF( ", ");
						}

						sourceEntryPtr=sourceEntryPtr->next;
					}


					PROC_PRINTF( "\n");
					clientEntry = clientEntry->next;
				}

				PROC_PRINTF( "\n");
				groupEntryPtr=groupEntryPtr->next;
			}

		}




		PROC_PRINTF("=================================================\n");
		PROC_PRINTF("IPv4 Flow List:\n");
		flowCnt=1;
		for(hashIndex=0;hashIndex<IGMP_HASH_TBL_SIZE;hashIndex++)
		{
			/*to dump multicast flow information*/
			mcastFlowEntry=rtk_igmp_mCastModuleArray[moduleIndex].flowHashTable[hashIndex];
			while(mcastFlowEntry!=NULL)
			{
				if(mcastFlowEntry->ipVersion==IP_VERSION4)
				{
					PROC_PRINTF( "	  [%d] %pI4h:(%d)-->%pI4h:(%d) \n \t\t l4proto:%d InCvid:%d InSvid:%d swFwdCnt=%u defCnt=%u (idle=%d) hwcb=%d fwdcpu=%d",
						flowCnt,&mcastFlowEntry->serverAddr[0],mcastFlowEntry->sport,&mcastFlowEntry->groupAddr[0],mcastFlowEntry->dport,mcastFlowEntry->l4proto,mcastFlowEntry->ingressCvlan,mcastFlowEntry->ingressSvlan,mcastFlowEntry->fwdPktCnt,mcastFlowEntry->defineFwdPktCnt,rtk_igmp_sysUpSeconds-mcastFlowEntry->refreshTime,mcastFlowEntry->multicastFwdInfo.hwCbAcc,mcastFlowEntry->multicastFwdInfo.copy2cpu);
					_rtk_igmp_showValidToDevInfo(s,v,mcastFlowEntry->multicastFwdInfo.egressDevInfo);
					PROC_PRINTF("\n");
				}
				flowCnt++;
				mcastFlowEntry=mcastFlowEntry->next;
			}
		}

		PROC_PRINTF( "IPv6 Flow List:\n");
		flowCnt=1;
		for(hashIndex=0;hashIndex<IGMP_HASH_TBL_SIZE;hashIndex++)
		 {
			/*to dump multicast flow information*/
			mcastFlowEntry=rtk_igmp_mCastModuleArray[moduleIndex].flowHashTable[hashIndex];
			while(mcastFlowEntry!=NULL)
			{
				if(mcastFlowEntry->ipVersion==IP_VERSION6)
				{
					PROC_PRINTF( "	  [%d] %pI6:(%d)-->",flowCnt,&mcastFlowEntry->serverAddr[0],mcastFlowEntry->sport);
					PROC_PRINTF( "%pI6:(%d) \n \t\t l4proto:%d InCvid:%d InSvid:%d swFwdCnt=%u defCnt=%u (idle=%d) hwcb=%d fwdcpu=%d",&mcastFlowEntry->groupAddr[0],mcastFlowEntry->dport,mcastFlowEntry->l4proto,mcastFlowEntry->ingressCvlan,mcastFlowEntry->ingressSvlan,mcastFlowEntry->fwdPktCnt,mcastFlowEntry->defineFwdPktCnt,rtk_igmp_sysUpSeconds-mcastFlowEntry->refreshTime,mcastFlowEntry->multicastFwdInfo.hwCbAcc,mcastFlowEntry->multicastFwdInfo.copy2cpu);
					_rtk_igmp_showValidToDevInfo(s,v,mcastFlowEntry->multicastFwdInfo.egressDevInfo);
					PROC_PRINTF("\n");
				}
				flowCnt++;
				mcastFlowEntry=mcastFlowEntry->next;
			}
		}

	}
	PROC_PRINTF("************************************************\n\n");


	return SUCCESS;
}



static struct notifier_block rtk_igmp_netdev_notifier = {
	.notifier_call = rtk_igmp_netdev_event,
};


static int32 rtk_igmp_initHashTable(int32 moduleIndex, int32 hashTableSize)
{
	uint32 i=0;

	/* Allocate memory */
	rtk_igmp_mCastModuleArray[moduleIndex].rtk_igmp_ipv4HashTable = (struct rtk_igmp_groupEntry **)rtk_igmp_malloc(sizeof(struct rtk_igmp_groupEntry *) * hashTableSize);
	if (rtk_igmp_mCastModuleArray[moduleIndex].rtk_igmp_ipv4HashTable!= NULL)
	{
		for (i = 0 ; i < hashTableSize ; i++)
		{
			rtk_igmp_mCastModuleArray[moduleIndex].rtk_igmp_ipv4HashTable[i]=NULL;
		}
	}
	else
	{
		return FAIL;
	}

	rtk_igmp_mCastModuleArray[moduleIndex].rtk_igmp_ipv6HashTable=  (struct rtk_igmp_groupEntry **)rtk_igmp_malloc(sizeof(struct rtk_igmp_groupEntry *) * hashTableSize);
	if (rtk_igmp_mCastModuleArray[moduleIndex].rtk_igmp_ipv6HashTable!=NULL)
	{
		for (i = 0 ; i < hashTableSize ; i++)
		{
			rtk_igmp_mCastModuleArray[moduleIndex].rtk_igmp_ipv6HashTable[i]=NULL;
		}
	}
	else
	{
		if(rtk_igmp_mCastModuleArray[moduleIndex].rtk_igmp_ipv4HashTable!=NULL)
		{
			rtk_igmp_free(rtk_igmp_mCastModuleArray[moduleIndex].rtk_igmp_ipv4HashTable);
		}
		return FAIL;
	}

	rtk_igmp_mCastModuleArray[moduleIndex].flowHashTable= (struct rtk_igmp_mcastFlowEntry **)rtk_igmp_malloc(sizeof(struct rtk_igmp_mcastFlowEntry *) * hashTableSize);
	if (rtk_igmp_mCastModuleArray[moduleIndex].flowHashTable!=NULL)
	{
		for (i = 0 ; i < hashTableSize ; i++)
		{
			rtk_igmp_mCastModuleArray[moduleIndex].flowHashTable[i]=NULL;
		}
	}
	else
	{
		if(rtk_igmp_mCastModuleArray[moduleIndex].rtk_igmp_ipv4HashTable!=NULL)
		{
			rtk_igmp_free(rtk_igmp_mCastModuleArray[moduleIndex].rtk_igmp_ipv4HashTable);
		}

		if(rtk_igmp_mCastModuleArray[moduleIndex].rtk_igmp_ipv6HashTable!=NULL)
		{
			rtk_igmp_free(rtk_igmp_mCastModuleArray[moduleIndex].rtk_igmp_ipv6HashTable);
		}

		return FAIL;
	}

	return SUCCESS;
}



//size of name follow IFNAMSIZ
int rtk_igmp_devNameToBrDevIfidx(char *devName)
{
	int i;
	int ifidx=FAIL;

	for(i=0 ; i< IGMP_MAX_BR_MODULE_NUM ; i++)
	{
		if(!rtk_igmp_mCastModuleArray[i].validBit)
			continue;
		if(strcmp(rtk_igmp_mCastModuleArray[i].brDevInfo.devName,devName)==0)
		{
			ifidx =rtk_igmp_mCastModuleArray[i].brDevInfo.ifindex;
			break;
		}
	}

	return ifidx;
}


//size of name follow IFNAMSIZ
int rtk_igmp_devNameToDevIfidx(char *devName)
{
	int i;
	int ifidx=FAIL;


	for(i=0 ; i< IGMP_MAX_WAN_DEV_NUM ; i++)
	{
		if(!rtk_igmp_wanDevInfo[i].dev)
			continue;
		if(strcmp(rtk_igmp_wanDevInfo[i].devName,devName)==0)
		{
			ifidx =rtk_igmp_wanDevInfo[i].ifindex;
			break;
		}
	}

	if(ifidx==FAIL)
	{
		for(i=0 ; i<IGMP_MAX_LAN_DEV_NUM ;i++ )
		{
			if(!rtk_igmp_lanDevInfo[i].dev)
				continue;
			if(strcmp(rtk_igmp_lanDevInfo[i].devName,devName) ==0)
			{
				ifidx = rtk_igmp_lanDevInfo[i].ifindex;
				break;
			}
		}
	}

	return ifidx;
}


//return FAIL if not found
int rtk_igmp_devInfoToIfIdx(rt_igmpHook_devInfo_t devInfo)
{
	
	int32 devIdx=FAIL;
	rtk_igmp_multicastDeviceInfo_t *devcfg=NULL;
	rtk_igmp_multicastModule_t *brcfg=NULL;
	
	if(devIdx==FAIL)
	{
		devIdx = rtk_igmp_devNameToDevIfidx(devInfo.devIfname);
	}
	if(devIdx==FAIL)
	{
		devIdx = rtk_igmp_devNameToBrDevIfidx(devInfo.devIfname);
	}
	if(devIdx ==FAIL)
	{
		devIdx=devInfo.devIfidx;
	}

	devcfg =_rtk_igmp_devIfidx_to_devInfo(devIdx);
	brcfg = _rtk_igmp_devIfidx_to_BrDevInfo(devIdx);

	if(devIdx==FAIL || (devcfg==NULL && brcfg==NULL))
	{
		IGMP_CTRL("can't get devIdx=%d  devInfo.devIfname=[%s] devInfo.devIfidx=%d",devIdx,devInfo.devIfname,devInfo.devIfidx);
		if(!devcfg) IGMP_CTRL("devcfg NULL");
		if(!brcfg) IGMP_CTRL("brcfg NULL");
	}

	if(devcfg==NULL && brcfg==NULL)
		return FAIL;
	else
		return devIdx;
	
}

//size of name follow IFNAMSIZ
int rtk_igmp_wanIfNameBindToBrName(char *wanDevName , char *brDevName)
{
	int i;
	int wanEntIdx=FAIL,brEntIdx=FAIL;


	for(i=0 ; i< IGMP_MAX_WAN_DEV_NUM ; i++)
	{
		if(strcmp(rtk_igmp_wanDevInfo[i].devName,wanDevName)==0)
		{
			wanEntIdx = i;
			break;
		}
	}

	for(i=0 ; i<IGMP_MAX_BR_MODULE_NUM ;i++ )
	{
		if(rtk_igmp_mCastModuleArray[i].validBit &&strcmp(rtk_igmp_mCastModuleArray[i].brDevInfo.devName,brDevName) ==0)
		{
			brEntIdx = i;
			break;
		}
	}

	if(wanEntIdx ==FAIL || brEntIdx==FAIL ){IGMP_WARNING("wanEntIdx:%d ofr brEntIdx:%d illegall",wanEntIdx,brEntIdx); return FAIL;}

	rtk_igmp_wanDevInfo[wanEntIdx].bind2BrModuleIdx = brEntIdx;

	return SUCCESS;


}

int rtk_igmp_wanIdxBindToBrIdx(int32 wanifidx,int32 brIfidx)
{
	int i;
	int wanEntIdx=FAIL,brEntIdx=FAIL;

	if(wanifidx<0 || brIfidx<0){IGMP_WARNING("wanifidx:%d  or brIfidx:%d illegall",wanifidx,brIfidx); return FAIL;}

	for(i=0 ; i< IGMP_MAX_WAN_DEV_NUM ; i++)
	{
		if(rtk_igmp_wanDevInfo[i].ifindex==wanifidx)
		{
			wanEntIdx = i;
			break;
		}
	}

	for(i=0 ; i<IGMP_MAX_BR_MODULE_NUM ;i++ )
	{
		if(rtk_igmp_mCastModuleArray[i].validBit && rtk_igmp_mCastModuleArray[i].brDevInfo.ifindex ==brIfidx)
		{
			brEntIdx = i;
			break;
		}
	}

	if(wanEntIdx ==FAIL || brEntIdx==FAIL ){IGMP_WARNING("wanEntIdx:%d ofr brEntIdx:%d illegall",wanEntIdx,brEntIdx); return FAIL;}

	rtk_igmp_wanDevInfo[wanEntIdx].bind2BrModuleIdx = brEntIdx;

	return SUCCESS;

}


int _rtk_igmp_registerWanDev(struct net_device *wan_dev)
{

	int i;
	int entIdx=FAIL;
	
	if(_rtk_igmp_devIfidx_to_devInfo(wan_dev->ifindex))
		return RTK_IGMP_ERR_OK;
	
	for(i=0 ; i< IGMP_MAX_WAN_DEV_NUM ; i++)
	{
		if(rtk_igmp_wanDevInfo[i].dev==NULL)
		{
			entIdx = i;
			break;
		}

	}
	if(entIdx == FAIL){
		IGMP_WARNING("TABLE FULL");
		return RTK_IGMP_TABLE_FULL;
	}

	memset(&rtk_igmp_wanDevInfo[entIdx],0,sizeof(rtk_igmp_multicastDeviceInfo_t));
	rtk_igmp_wanDevInfo[entIdx].ifindex = wan_dev->ifindex;
	memcpy(rtk_igmp_wanDevInfo[entIdx].devName,wan_dev->name,sizeof(wan_dev->name));
	rtk_igmp_wanDevInfo[entIdx].dev = wan_dev;
	rtk_igmp_wanDevInfo[entIdx].bind2BrModuleIdx=FAIL;

	//init devconfig default valud
	rtk_igmp_wanDevInfo[entIdx].devConf.igmp.igmpMcMemberAgingInterval =IGMP_MEMBER_AGING;
	rtk_igmp_wanDevInfo[entIdx].devConf.igmp.igmpMcLastMemberAgingInterval =IGMP_LAST_MEMBER_AGING;	
	rtk_igmp_wanDevInfo[entIdx].devConf.igmp.igmpReportDrop=1;

	rtk_igmp_wanDevInfo[entIdx].devConf.igmp6.igmp6McMemberAgingInterval =IGMP6_MEMBER_AGING;
	rtk_igmp_wanDevInfo[entIdx].devConf.igmp6.igmp6McLastMemberAgingInterval =IGMP6_LAST_MEMBER_AGING;	
	rtk_igmp_wanDevInfo[entIdx].devConf.igmp6.igmp6ReportDrop=1;

	rtk_igmp_wanDevInfo[entIdx].devConf.igmp6.igmp6OverWeightDrop=1;
	rtk_igmp_wanDevInfo[entIdx].devConf.igmp.igmpOverWeightDrop=1;
#if defined(CONFIG_RTL_PER_PORT_CLIENT_NUM)
	if(igmpSysdb.WanLearnIgmp) 
		rtk_igmp_set_wan_learn(igmpSysdb.WanLearnIgmp);
#endif
	INIT_LIST_HEAD(&rtk_igmp_wanDevInfo[entIdx].devConf.igmp.white);
	INIT_LIST_HEAD(&rtk_igmp_wanDevInfo[entIdx].devConf.igmp.black);
	INIT_LIST_HEAD(&rtk_igmp_wanDevInfo[entIdx].devConf.igmp6.white);
	INIT_LIST_HEAD(&rtk_igmp_wanDevInfo[entIdx].devConf.igmp6.black);


	IGMP_CTRL("Register rtk_igmp_wanDevInfo[%d] %s on igmp",entIdx,rtk_igmp_wanDevInfo[entIdx].devName);


	return RTK_IGMP_ERR_OK;
}

int _rtk_igmp_unregisterWanDev(struct net_device *wan_dev, int del)
{
	int entIdx=FAIL;
	int i;
	for(i=0 ; i< IGMP_MAX_WAN_DEV_NUM ; i++)
	{
		if(!rtk_igmp_wanDevInfo[i].dev)
			continue;
		if(rtk_igmp_wanDevInfo[i].dev==wan_dev && rtk_igmp_wanDevInfo[i].ifindex==wan_dev->ifindex)
		{
			entIdx = i;
			break;
		}

	}
	if(entIdx == FAIL){
		IGMP_WARNING("TABLE NOT FOUND");
		return RTK_IGMP_ENTRY_NOT_FOUND;
	}

	if(del) memset(&rtk_igmp_wanDevInfo[entIdx],0,sizeof(rtk_igmp_multicastDeviceInfo_t));
	IGMP_CTRL("%s rtk_igmp_wanDevInfo[%d] %s on igmp",(del)?"UnRegister":"Clear",entIdx,wan_dev->name);
	
	return RTK_IGMP_ERR_OK;

}

int _rtk_dev_isWlan(struct net_device *dev)
{
#ifdef RTK_NETDEV_PRIV_FLAGS
	if (rtk_netdev_get_flags(dev)&(RTK_IFF_DOMAIN_WLAN))
		return TRUE;
#else
	if ((dev->priv_flags & IFF_DOMAIN_WLAN))
		return TRUE;
#endif

	return FALSE;
}

int _rtk_igmp_registerLanDev(struct net_device *lan_dev)
{

	int i;
	int entIdx=FAIL;
	
	if(_rtk_igmp_devIfidx_to_devInfo(lan_dev->ifindex))
		return RTK_IGMP_ERR_OK;
	
	for(i=0 ; i< IGMP_MAX_LAN_DEV_NUM ; i++)
	{
		if(rtk_igmp_lanDevInfo[i].dev==NULL)
		{
			entIdx = i;
			break;
		}

	}
	if(entIdx == FAIL){
		IGMP_WARNING("TABLE FULL");
		return RTK_IGMP_TABLE_FULL;
	}

	memset(&rtk_igmp_lanDevInfo[entIdx],0,sizeof(rtk_igmp_multicastDeviceInfo_t));
	rtk_igmp_lanDevInfo[entIdx].ifindex = lan_dev->ifindex;
	memcpy(rtk_igmp_lanDevInfo[entIdx].devName,lan_dev->name,sizeof(lan_dev->name));
	rtk_igmp_lanDevInfo[entIdx].dev = lan_dev;
	rtk_igmp_lanDevInfo[entIdx].bind2BrModuleIdx=FAIL;

	//init devconfig default valud
	rtk_igmp_lanDevInfo[entIdx].devConf.igmp.igmpMcMemberAgingInterval =IGMP_MEMBER_AGING;
	rtk_igmp_lanDevInfo[entIdx].devConf.igmp6.igmp6McMemberAgingInterval =IGMP6_MEMBER_AGING;
	rtk_igmp_lanDevInfo[entIdx].devConf.igmp.igmpMcLastMemberAgingInterval =IGMP_LAST_MEMBER_AGING;
	rtk_igmp_lanDevInfo[entIdx].devConf.igmp6.igmp6McLastMemberAgingInterval =IGMP6_LAST_MEMBER_AGING;

	rtk_igmp_lanDevInfo[entIdx].devConf.igmp6.igmp6OverWeightDrop=1;
	rtk_igmp_lanDevInfo[entIdx].devConf.igmp.igmpOverWeightDrop=1;

#if defined(CONFIG_RTK_FC_WIFI_AMSDU_OFFLOAD_BY_PE) && defined(IGMP_SUPPORT_MCTOUC)
	if(_rtk_dev_isWlan(lan_dev))
	{
		rtk_igmp_lanDevInfo[entIdx].devConf.igmp.igmpDmacMcToUc=1;
		rtk_igmp_lanDevInfo[entIdx].devConf.igmp6.igmp6DmacMcToUc=1;

		rtk_igmp_lanDevInfo[entIdx].devConf.igmp.igmpSrcDevFilterDis=1;
		rtk_igmp_lanDevInfo[entIdx].devConf.igmp6.igmp6SrcDevFilterDis=1;
	}
#endif

	INIT_LIST_HEAD(&rtk_igmp_lanDevInfo[entIdx].devConf.igmp.white);
	INIT_LIST_HEAD(&rtk_igmp_lanDevInfo[entIdx].devConf.igmp.black);
	INIT_LIST_HEAD(&rtk_igmp_lanDevInfo[entIdx].devConf.igmp6.white);
	INIT_LIST_HEAD(&rtk_igmp_lanDevInfo[entIdx].devConf.igmp6.black);



	IGMP_CTRL("Register rtk_igmp_lanDevInfo[%d] %s on igmp",entIdx,rtk_igmp_lanDevInfo[entIdx].devName);


	return RTK_IGMP_ERR_OK;
}

int32 rtk_igmp_clearDataBaseByDev(int32 devIdx)
{
	struct rtk_igmp_groupEntry* groupEntryPtr=NULL;
	struct rtk_igmp_groupEntry* nextEntry=NULL;
	struct rtk_igmp_clientEntry *clientEntry=NULL;
	struct rtk_igmp_clientEntry *nextClientEntry=NULL;	
	int32 moduleIndex;
	int i;
	
	for(moduleIndex=0; moduleIndex<IGMP_MAX_BR_MODULE_NUM; moduleIndex++)
	{
		if(rtk_igmp_mCastModuleArray[moduleIndex].validBit==0)
			continue;

		/*maintain ipv4 group entry  timer */
		for(i=0; i<IGMP_HASH_TBL_SIZE; i++)
		{
			/*scan the hash table*/
			if(rtk_igmp_mCastModuleArray[moduleIndex].rtk_igmp_ipv4HashTable!=NULL)
			{
				groupEntryPtr=rtk_igmp_mCastModuleArray[moduleIndex].rtk_igmp_ipv4HashTable[i];
				while(groupEntryPtr)              /*traverse each group list*/
				{
					nextEntry=groupEntryPtr->next;
					clientEntry = groupEntryPtr->clientList;
					while(clientEntry!=NULL)
					{
						nextClientEntry=clientEntry->next;
						if(devIdx == clientEntry->inIfidx)
							rtk_igmp_deleteClientEntry(groupEntryPtr,clientEntry);
						clientEntry=nextClientEntry;
					}
					groupEntryPtr=nextEntry;/*because expired group entry  will be cleared*/
				}
			}
		}

		/*maintain ipv6 group entry  timer */
		for(i=0; i<IGMP_HASH_TBL_SIZE; i++)
		{
			  /*scan the hash table*/
			if(rtk_igmp_mCastModuleArray[moduleIndex].rtk_igmp_ipv6HashTable!=NULL)
			{
				groupEntryPtr=rtk_igmp_mCastModuleArray[moduleIndex].rtk_igmp_ipv6HashTable[i];
				while(groupEntryPtr)              /*traverse each group list*/
				{
					nextEntry=groupEntryPtr->next;
					clientEntry = groupEntryPtr->clientList;
					while(clientEntry!=NULL)
					{
						nextClientEntry=clientEntry->next;
						if(devIdx == clientEntry->inIfidx)
							rtk_igmp_deleteClientEntry(groupEntryPtr,clientEntry);
						clientEntry=nextClientEntry;
					}
					groupEntryPtr=nextEntry;/*because expired group entry  will be cleared*/
				}
			}
		}

	}

	return RTK_IGMP_ERR_OK;

}


int _rtk_igmp_unregisterLanDev(struct net_device *lan_dev, int del)
{
	int entIdx=FAIL;
	int i;
	for(i=0 ; i< IGMP_MAX_LAN_DEV_NUM ; i++)
	{
		if(!rtk_igmp_lanDevInfo[i].dev)
			continue;
		if(rtk_igmp_lanDevInfo[i].dev==lan_dev && rtk_igmp_lanDevInfo[i].ifindex==lan_dev->ifindex)
		{
			entIdx = i;
			break;
		}

	}
	if(entIdx == FAIL){
		IGMP_CTRL("TABLE NOT FOUND");
		return RTK_IGMP_ENTRY_NOT_FOUND;
	}

	//clear data base
	rtk_igmp_clearDataBaseByDev(rtk_igmp_lanDevInfo[i].ifindex);
	rtk_igmp_flow_reflush_update(FAIL,NULL);
	
	if(del) memset(&rtk_igmp_lanDevInfo[entIdx],0,sizeof(rtk_igmp_multicastDeviceInfo_t));
	IGMP_CTRL("%s rtk_igmp_lanDevInfo[%d] %s on igmp",(del)?"UnRegister":"Clear",entIdx,lan_dev->name);
	
	return RTK_IGMP_ERR_OK;

}




int _rtk_igmp_registerBrDev(struct net_device *br_dev)
{
	int i;
	int entIdx=FAIL;
	
	if(_rtk_igmp_devIfidx_to_BrDevInfo(br_dev->ifindex))
		return RTK_IGMP_ERR_OK;

	for(i=0 ; i<IGMP_MAX_BR_MODULE_NUM ;i++ )
	{
		if(rtk_igmp_mCastModuleArray[i].validBit==0)
		{
			entIdx = i;
			break;
		}
	}
	if(entIdx == FAIL){
		IGMP_WARNING("TABLE FULL");
		return RTK_IGMP_TABLE_FULL;
	}

	//regist moudle
	memset(&rtk_igmp_mCastModuleArray[entIdx],0,sizeof(struct rtk_igmp_multicastModule));
	/*initialize hash table*/
	ASSERT_EQ(rtk_igmp_initHashTable(entIdx, IGMP_HASH_TBL_SIZE),SUCCESS);

	rtk_igmp_mCastModuleArray[entIdx].validBit=1;
	rtk_igmp_mCastModuleArray[entIdx].brDevInfo.dev = br_dev;
	memcpy(rtk_igmp_mCastModuleArray[entIdx].brDevInfo.devName,br_dev->name,sizeof(br_dev->name));
	rtk_igmp_mCastModuleArray[entIdx].brDevInfo.ifindex = br_dev->ifindex;

	if(br_dev->dev_addr[0]|br_dev->dev_addr[1]|br_dev->dev_addr[2]|br_dev->dev_addr[3]|br_dev->dev_addr[4]|br_dev->dev_addr[5])
	{
		memcpy(rtk_igmp_mCastModuleArray[entIdx].brDevInfo.rtk_igmp_gatewayMac,br_dev->dev_addr,ETH_ALEN);
	}
	//default drop overWeight
	rtk_igmp_mCastModuleArray[entIdx].brDevInfo.brDevConf.sysIp6OverWeightDrop=1;
	rtk_igmp_mCastModuleArray[entIdx].brDevInfo.brDevConf.sysIp4OverWeightDrop=1;


	IGMP_CTRL("Register mCastModule[%d] %s on igmp",entIdx,rtk_igmp_mCastModuleArray[entIdx].brDevInfo.devName);

	return RTK_IGMP_ERR_OK;

}

int _rtk_igmp_unregisterBrDev(struct net_device *br_dev, int del)
{
	int moduleIndex;
	int i;
	struct rtk_igmp_groupEntry *groupEntryPtr;
	struct rtk_igmp_mcastFlowEntry *mcastFlowEntryPtr;
	for(moduleIndex=0 ; moduleIndex<IGMP_MAX_BR_MODULE_NUM ;moduleIndex++ )
	{
		if(rtk_igmp_mCastModuleArray[moduleIndex].validBit && rtk_igmp_mCastModuleArray[moduleIndex].brDevInfo.ifindex==br_dev->ifindex)
		{
			if(del) rtk_igmp_mCastModuleArray[moduleIndex].validBit =0 ;
			/*delete ipv4 multicast entry*/
			for (i=0;i<IGMP_HASH_TBL_SIZE;i++)
			{
				groupEntryPtr=rtk_igmp_mCastModuleArray[moduleIndex].rtk_igmp_ipv4HashTable[i];

				while(groupEntryPtr!=NULL)
				{
					rtk_igmp_deleteGroupEntry(moduleIndex,groupEntryPtr, rtk_igmp_mCastModuleArray[moduleIndex].rtk_igmp_ipv4HashTable);
					groupEntryPtr=rtk_igmp_mCastModuleArray[moduleIndex].rtk_igmp_ipv4HashTable[i];
				}
			}
			if(del)
			{
				rtk_igmp_free(rtk_igmp_mCastModuleArray[moduleIndex].rtk_igmp_ipv4HashTable);
				rtk_igmp_mCastModuleArray[moduleIndex].rtk_igmp_ipv4HashTable=NULL;
			}

			/*delete ipv6 multicast entry*/
			for(i=0; i<IGMP_HASH_TBL_SIZE; i++)
			{

				groupEntryPtr=rtk_igmp_mCastModuleArray[moduleIndex].rtk_igmp_ipv6HashTable[i];
				while(groupEntryPtr!=NULL)
				{
					rtk_igmp_deleteGroupEntry(moduleIndex,groupEntryPtr, rtk_igmp_mCastModuleArray[moduleIndex].rtk_igmp_ipv6HashTable);
					groupEntryPtr=rtk_igmp_mCastModuleArray[moduleIndex].rtk_igmp_ipv6HashTable[i];
				}
			}
			if(del)
			{
				rtk_igmp_free(rtk_igmp_mCastModuleArray[moduleIndex].rtk_igmp_ipv6HashTable);
				rtk_igmp_mCastModuleArray[moduleIndex].rtk_igmp_ipv6HashTable=NULL;
			}

			/*delete multicast flow entry*/
			for (i=0;i<IGMP_HASH_TBL_SIZE;i++)
			{
				mcastFlowEntryPtr=rtk_igmp_mCastModuleArray[moduleIndex].flowHashTable[i];

				while(mcastFlowEntryPtr!=NULL)
				{
					rtk_igmp_deleteMcastFlowEntry(mcastFlowEntryPtr, rtk_igmp_mCastModuleArray[moduleIndex].flowHashTable);
					mcastFlowEntryPtr=rtk_igmp_mCastModuleArray[moduleIndex].flowHashTable[i];
				}
			}
			if(del)
			{
				rtk_igmp_free(rtk_igmp_mCastModuleArray[moduleIndex].flowHashTable);
				rtk_igmp_mCastModuleArray[moduleIndex].flowHashTable=NULL;
			}

			//invalid wan mapping info
			for(i=0 ; i< IGMP_MAX_WAN_DEV_NUM ; i++)
			{
				if(rtk_igmp_wanDevInfo[i].dev && rtk_igmp_wanDevInfo[i].bind2BrModuleIdx==moduleIndex)
				{
					rtk_igmp_wanDevInfo[i].bind2BrModuleIdx=FAIL;
				}
			}


			IGMP_CTRL("%s mCastModule[%d] %s on igmp",(del)?"UnRegister":"Clear",i,br_dev->name);
			return RTK_IGMP_ERR_OK;
		}
	}
	return RTK_IGMP_ENTRY_NOT_FOUND;
}


static int rtk_igmp_netdev_event(struct notifier_block *this, unsigned long event, void *ptr)
{
	struct net_device *netDev = netdev_notifier_info_to_dev(ptr);

	//ARPHRD_ETHER
	IGMP_CTRL("[NETDEVICE EVENT] netdevice:%s  type: %d event:0x%lx  priv_flags:0x%x\n", netDev->name, netDev->type, event, netDev->priv_flags);
	IGMP_CTRL("[NETDEVICE EVENT] ifinedx=%d	if_port=%d",netDev->ifindex,netDev->if_port);

	igmp_spin_lock_bh(igmpSysdb.lock_igmp);

	if(netDev->priv_flags & IFF_EBRIDGE )
	{
		//register BrDev
		if(event==NETDEV_UP)
			_rtk_igmp_registerBrDev(netDev);
		else if(event==NETDEV_DOWN)
			_rtk_igmp_unregisterBrDev(netDev, 0);
		else if(event==NETDEV_UNREGISTER)
			_rtk_igmp_unregisterBrDev(netDev, 1);
	}
#ifdef RTK_NETDEV_PRIV_FLAGS
	else if (rtk_netdev_get_flags(netDev)&RTK_IFF_DOMAIN_WAN)
#else
	else if (netDev->priv_flags & IFF_DOMAIN_WAN )
#endif
	{
		//register WanDev
		if(event==NETDEV_UP) {
			if(netDev->flags & IFF_MULTICAST)
				_rtk_igmp_registerWanDev(netDev);
		} else if (event==NETDEV_DOWN) {
			_rtk_igmp_unregisterWanDev(netDev, 0);
		} else if (event==NETDEV_UNREGISTER) {
			_rtk_igmp_unregisterWanDev(netDev, 1);
		} else if (event==NETDEV_CHANGE) {
			if(netDev->flags & IFF_MULTICAST)
				_rtk_igmp_registerWanDev(netDev);
			else
				_rtk_igmp_unregisterWanDev(netDev, 1);
		}

	}
#ifdef RTK_NETDEV_PRIV_FLAGS
	else if (rtk_netdev_get_flags(netDev)&(RTK_IFF_DOMAIN_WLAN|RTK_IFF_DOMAIN_ELAN))
#else
	else if ((netDev->priv_flags & IFF_DOMAIN_ELAN)||(netDev->priv_flags & IFF_DOMAIN_WLAN))
#endif
	{
		//register LanDev
		if(event==NETDEV_UP){
			if(netDev->flags & IFF_MULTICAST)
				_rtk_igmp_registerLanDev(netDev); 
		} else if (event==NETDEV_DOWN) {
			_rtk_igmp_unregisterLanDev(netDev, 0);
		} else if (event==NETDEV_UNREGISTER) {
			_rtk_igmp_unregisterLanDev(netDev, 1);
		} else if (event==NETDEV_CHANGE) {
			if(netDev->flags & IFF_MULTICAST)
				_rtk_igmp_registerLanDev(netDev);
			else
				_rtk_igmp_unregisterLanDev(netDev, 1);
		}

	}
	igmp_spin_unlock_bh(igmpSysdb.lock_igmp);


	return RTK_IGMP_ERR_OK;

}

struct rtk_igmp_multicastModule* _rtk_igmp_devIfidx_to_BrDevInfo(int32 brIfindex)
{
	int i ;
	for(i=0 ; i <IGMP_MAX_BR_MODULE_NUM ;i++)
	{
		if(!rtk_igmp_mCastModuleArray[i].validBit)
			continue;
		if(rtk_igmp_mCastModuleArray[i].brDevInfo.ifindex==brIfindex)
			return &rtk_igmp_mCastModuleArray[i];
	}
	return NULL;
}


rtk_igmp_multicastDeviceInfo_t* _rtk_igmp_devIfidx_to_devInfo(int32 devIfindex)
{
	int i ;
	for(i=0 ; i <IGMP_MAX_LAN_DEV_NUM ;i++)
	{
		
		if(rtk_igmp_lanDevInfo[i].dev==NULL)
			continue;		
		if(rtk_igmp_lanDevInfo[i].ifindex == devIfindex)
			return &rtk_igmp_lanDevInfo[i];
	}
	if(i == IGMP_MAX_LAN_DEV_NUM)
	{
		for(i=0 ; i <IGMP_MAX_WAN_DEV_NUM ;i++)
		{
			if(rtk_igmp_wanDevInfo[i].dev==NULL)
				continue; 	
			if(rtk_igmp_wanDevInfo[i].ifindex == devIfindex)
				return &rtk_igmp_wanDevInfo[i];
		}

		if( i == IGMP_MAX_WAN_DEV_NUM)
			return NULL;
	}
	return NULL;
}



struct net_device * rtk_igmp_devIfidx_to_dev(int32 devIfindex)
{
	int i ;
	for(i=0 ; i <IGMP_MAX_LAN_DEV_NUM ;i++)
	{
		if(!rtk_igmp_lanDevInfo[i].dev)
			continue;
		if(rtk_igmp_lanDevInfo[i].ifindex == devIfindex)
			return rtk_igmp_lanDevInfo[i].dev;
	}

	for(i=0 ; i <IGMP_MAX_WAN_DEV_NUM ;i++)
	{
		if(!rtk_igmp_wanDevInfo[i].dev)
			continue;		
		if(rtk_igmp_wanDevInfo[i].ifindex == devIfindex)
			return rtk_igmp_wanDevInfo[i].dev;
	}
	
	for(i=0 ; i<IGMP_MAX_BR_MODULE_NUM ;i++ )
	{
		if((rtk_igmp_mCastModuleArray[i].validBit) && (devIfindex == rtk_igmp_mCastModuleArray[i].brDevInfo.ifindex))
		{
			return rtk_igmp_mCastModuleArray[i].brDevInfo.dev;
		}
	}

	
	return NULL;
}


//for br device mapping to ModuleIdx
int rtk_igmp_dstNetDevToModuleIdx(const struct net_device  *dstDev)
{
	int32 i;
	int32 moduleIndex=FAIL;

	for(i=0 ; i<IGMP_MAX_BR_MODULE_NUM ;i++ )
	{
		if((rtk_igmp_mCastModuleArray[i].validBit) && (dstDev->ifindex == rtk_igmp_mCastModuleArray[i].brDevInfo.ifindex))
		{
			IGMP_CTRL("Dst %s mapping to  moduleIndex:%d",dstDev->name,i);
			moduleIndex = i;
			break;
		}
	}

	return moduleIndex;
}


/*
	record to relative br device
*/
int rtk_igmp_srcNetDevToModuleIdx(const struct net_device  *srcDev,rtk_igmp_multicastDeviceInfo_t *devInfo)
{
	int32 i;
	struct net_bridge_port *p=NULL;
	struct net_bridge		*br=NULL;
	int32 moduleIndex=FAIL;

	if(srcDev==NULL)
		return moduleIndex;

	rcu_read_lock();
	p = rcu_dereference(srcDev->rx_handler_data);
	if(p)
	 	br = p->br;


	if(br)
	{
		for(i=0 ; i<IGMP_MAX_BR_MODULE_NUM ;i++ )
		{
			if((rtk_igmp_mCastModuleArray[i].validBit) && (br->dev->ifindex == rtk_igmp_mCastModuleArray[i].brDevInfo.ifindex))
			{
				IGMP_CTRL(" %s in %s get br search moduleIndex:%d",srcDev->name,br->dev->name,i);
				moduleIndex = i;
				break;
			}
		}
	}
	else
	{

		if(devInfo)
		{
			moduleIndex = devInfo->bind2BrModuleIdx;
		}
		else
		{
			devInfo = _rtk_igmp_devIfidx_to_devInfo(srcDev->ifindex);
			if(devInfo)
				moduleIndex = devInfo->bind2BrModuleIdx;
		}
		IGMP_CTRL("not in any br find in config table to do multicast routing %s ==> mouduleIdx:%d",srcDev->name,moduleIndex);

	}
	rcu_read_unlock();
	IGMP_CTRL("mapping moduleIndex=%d ",moduleIndex);

	return moduleIndex;

}


int32 rtk_igmp_hwCbReg(void)
{
#ifdef CONFIG_RTK_L34_FLEETCONNTRACK_ENABLE
#if 0
	if(igmpSysdb.regMdbCb)
	{
		//using wq to avoid  deadlock issue. cpu0(control igmplock->fclock) cpu1(data fclock->igmplock)
		//==> In Fc we move igmplock before fclock comment it
		hwCb.flowDelCbEvt	= rtk_igmp_flowDelCbEvt;
		hwCb.flowSetCbEvt	= rtk_igmp_flowSetCbEvt;
		hwCb.flowUpdateCbEvt	= rtk_igmp_flowUpdateCbEvt;	
		hwCb.groupAddCbEvt	= rtk_igmp_groupAddCbEvt;
		hwCb.groupDelCbEvt	= rtk_igmp_groupDelCbEvt;
		hwCb.hwRegInit		= rtk_igmp_hwRegInit;
		hwCb.hwUnRegClear	= rtk_igmp_hwUnRegClear;
		hwCb.hwUnknownIpMcAction= rtk_igmp_unknownIpMc_Action;
		if(hwCb.hwRegInit)
			hwCb.hwRegInit();
	}
	else
#endif		
	{
		//try to disable work queue fucntion because fc send dummy packet to netif_rx() function
		hwCb.flowDelCbEvt 	= _rtk_igmp_flowDelCbEvt;
		hwCb.flowSetCbEvt 	= _rtk_igmp_flowSetCbEvt;
		hwCb.flowUpdateCbEvt 	= _rtk_igmp_flowUpdateCbEvt;	
		hwCb.groupAddCbEvt	= _rtk_igmp_groupAddCbEvt;
		hwCb.groupDelCbEvt	= _rtk_igmp_groupDelCbEvt;
		hwCb.hwRegInit		= _rtk_igmp_hwRegInit;
		hwCb.hwUnRegClear	= _rtk_igmp_hwUnRegClear;
		hwCb.hwUnknownIpMcAction= _rtk_igmp_unknownIpMc_Action;
		if(hwCb.hwRegInit)
		{
			if(hwCb.hwRegInit())
				IGMP_WARNING("hwRegInit fail");
		}
	}
#endif

	return SUCCESS;
}

int32 rtk_igmp_hwCbUnReg(void)
{

	if(hwCb.hwUnRegClear)
		hwCb.hwUnRegClear();
	hwCb.flowDelCbEvt 	= NULL;
	hwCb.flowSetCbEvt 	= NULL;
	hwCb.flowUpdateCbEvt 	= NULL;	
	hwCb.groupAddCbEvt	= NULL;
	hwCb.groupDelCbEvt	= NULL;
	hwCb.hwRegInit		= NULL;
	hwCb.hwUnRegClear	= NULL;
	hwCb.hwUnknownIpMcAction= NULL;
	return SUCCESS;
}


extern int in6_pton(const char *src, int srclen, u8 *dst, int delim, const char **end);
int rtk_igmp_init(void)
{

	memset(rtk_igmp_mCastModuleArray,0,sizeof(rtk_igmp_mCastModuleArray));
	rtk_igmp_groupEntryPool=rtk_igmp_initGroupEntryPool(IGMP_MAX_GROUP_COUNT);
	rtk_igmp_clientEntryPool=rtk_igmp_initClientEntryPool(IGMP_MAX_CLIENT_COUNT);
	rtk_igmp_mcastFlowEntryPool=rtk_igmp_initMcastFlowEntryPool(IGMP_MAX_FLOW_COUNT);
	rtk_igmp_sourceEntryPool=rtk_igmp_initSourceEntryPool(IGMP_MAX_SOURCE_COUNT);

	memset(&gloConf,0,sizeof(gloConf));
	spin_lock_init(&igmpSysdb.lock_igmp);
	spin_lock_init(&igmpSysdb.lock_callBack);


	gloConf.igmp_sys_timer_sec = IGMP_EXPIRE_TIMER;

	//init timer
	rtk_igmp_multicastSysTimerInit();

	//init whilte list
	rtk_igmp_whiteEntry_flashInit();

	//init black list
	rtk_igmp_blackEntry_flashInit();

	//init user define reserved group to protocol-stack table
	rtk_igmp_groupToPsTbl_flashInit();
	{
		//setting table default value IN_MULTICAST_RESV_SPECIAL_IPV4
		rt_igmpHook_ignoreGroup_t patten;
		int32 index;
		memset(&patten,0,sizeof(patten));
		patten.isIpv6=0;
		patten.ignGroupIpStart[0] = RESERVE_MULTICAST_224_0_0_1;
		patten.ignGroupIpEnd[0] = RESERVE_MULTICAST_224_0_0_2;
		rtk_igmp_groupToPsTblAdd(&patten,&index);
		patten.ignGroupIpStart[0] = RESERVE_MULTICAST_224_0_0_22;
		patten.ignGroupIpEnd[0] = RESERVE_MULTICAST_224_0_0_22;
		rtk_igmp_groupToPsTblAdd(&patten,&index);
		patten.ignGroupIpStart[0] = RESERVE_MULTICAST_239_255_255_250;
		patten.ignGroupIpEnd[0] = RESERVE_MULTICAST_239_255_255_250;
		rtk_igmp_groupToPsTblAdd(&patten,&index);
		patten.isIpv6=1;
		in6_pton(RESERVE_MULTICAST_FF02_00_00,-1,(uint8*)&patten.ignGroupIpStart[0],-1,NULL);
		in6_pton(RESERVE_MULTICAST_FF02_02_FF,-1,(uint8*)&patten.ignGroupIpEnd[0],-1,NULL);
		rtk_igmp_groupToPsTblAdd(&patten,&index);
	}


	//init group Weight tbl
	rtk_igmp_groupWeight_flashInit();

	rtk_igmp_hwCbReg();
#if defined(CONFIG_RTL_LAN_COMPATIBILITY) || defined(CONFIG_RTL_NEW_IGMP_REPORT_BEHAVIOR)
        rtk_igmp_SnoopingconfigInit();
#endif
	//last register netdevice
	register_netdevice_notifier(&rtk_igmp_netdev_notifier);

	return 0;
}

int rtk_igmp_exit(void)
{
	unregister_netdevice_notifier(&rtk_igmp_netdev_notifier);
	igmp_spin_lock_bh(igmpSysdb.lock_igmp);
	rtk_igmp_multicastSysTimerDestroy();
	rtk_igmp_hwCbUnReg();
	igmp_spin_unlock_bh(igmpSysdb.lock_igmp);
	return SUCCESS;
}



static inline uint32 rtk_igmp_hashAlgorithm(uint32 ipVersion,uint32 *groupAddr)
{
	uint32 hashIndex=0;

	if (ipVersion==IP_VERSION4)
	{
		/*to do:change hash algorithm*/
		hashIndex= (IGMP_HASH_TBL_SIZE-1)&groupAddr[0];
	}
	else
	{
		hashIndex=(IGMP_HASH_TBL_SIZE-1)&(groupAddr[3]^groupAddr[2]^groupAddr[1]^groupAddr[0]);
	}

	return hashIndex;
}

#define TIMER

uint32 rtk_igmp_getTimeSec(void)
{
	int32 jiffiesDiff;
	if(jiffies > lastjiffies)
		jiffiesDiff =jiffies-lastjiffies;
	else
		jiffiesDiff =(0xffffffff-(lastjiffies-jiffies));
	lastjiffies=jiffies;
	lastTimer +=(jiffiesDiff/CONFIG_HZ);
	return lastTimer;
}

static int32 rtk_igmp_checkSourceTimer(struct rtk_igmp_clientEntry * clientEntry , struct rtk_igmp_sourceEntry * sourceEntry)
{
	uint8 deleteFlag=FALSE; //delte group? client? source?
	uint8 oldFwdState,newFwdState;

	//IGMP("Check GIP.Clt.Src Timer");
	oldFwdState=sourceEntry->fwdState;

	if(sourceEntry->portTimer<=rtk_igmp_sysUpSeconds) /*means time out*/
	{
		if (clientEntry->groupFilterTimer<=rtk_igmp_sysUpSeconds) /* include mode*/
		{
			deleteFlag=TRUE;
		}
		sourceEntry->fwdState=FALSE;
	}
	else
	{
		//deleteFlag=FALSE; // redundant code
		sourceEntry->fwdState=TRUE;
	}

	newFwdState=sourceEntry->fwdState;

	//IGMP("GIP.Clt.SrcIP fwdState by {%s}"
	//, (deleteFlag==TRUE)?"<del SrcIP>":((newFwdState!=oldFwdState)&&(newFwdState==TRUE))?"<SrcIP stop fwd>":"SrcIP go on fwd");

	if (deleteFlag==TRUE) /*means INCLUDE mode and expired*/
	{
		rtk_igmp_deleteSourceEntry(clientEntry,sourceEntry);
	}

	if ((deleteFlag==TRUE) || (newFwdState!=oldFwdState))
	{
		IGMP_CTRL("sourceTimer change");
		return TRUE;
	}

	return FALSE;
}


static void rtk_igmp_checkClientEntryTimer(int32 moduleIndex,struct rtk_igmp_groupEntry * groupEntry, struct rtk_igmp_clientEntry * clientEntry)
{

	uint32 oldFwdIfidx=0;
	uint32 newFwdIfidx=0;
	struct rtk_igmp_sourceEntry *sourceEntry=clientEntry->sourceList;
	struct rtk_igmp_sourceEntry *nextSourceEntry=NULL;
	uint32 clientStatusChange=0,sourceStatusChange=0;

	oldFwdIfidx=rtk_igmp_getClientFwdIfIdx(clientEntry, rtk_igmp_sysUpSeconds);
	while (sourceEntry!=NULL)
	{
		nextSourceEntry=sourceEntry->next;
		if(rtk_igmp_checkSourceTimer(clientEntry, sourceEntry))
			sourceStatusChange=1; //if any source change ,we will change to hardware
		sourceEntry=nextSourceEntry;
	}

	newFwdIfidx=rtk_igmp_getClientFwdIfIdx(clientEntry, rtk_igmp_sysUpSeconds);
	//IGMP_CTRL("oldFwdIfidx=%d newFwdIfidx=%d",oldFwdIfidx,newFwdIfidx);
	if (newFwdIfidx==FAIL) /*none active port*/
	{
		rtk_igmp_deleteClientEntry(groupEntry,clientEntry);
	}

	//Client status exclude mode -> include mode
	if( (clientEntry->groupFilterTimer<=rtk_igmp_sysUpSeconds) &&  ((rtk_igmp_sysUpSeconds - clientEntry->groupFilterTimer) <=  gloConf.igmp_sys_timer_sec))
		clientStatusChange=1;

	if ((oldFwdIfidx!=newFwdIfidx) || (newFwdIfidx==FAIL) || clientStatusChange || sourceStatusChange)
	{
		IGMP_CTRL("clientStatusChange=%d sourceStatusChange=%d oldFwdIfidx=%d newFwdIfidx=%d ",clientStatusChange,sourceStatusChange,oldFwdIfidx,newFwdIfidx);
		rtk_igmp_flow_reflush_update(FAIL,groupEntry->groupAddr);
	}

}


static void rtk_igmp_checkGroupEntryTimer(int32 moduleIndex ,struct rtk_igmp_groupEntry * groupEntry, struct rtk_igmp_groupEntry ** hashTable)
{
	struct rtk_igmp_clientEntry *clientEntry=groupEntry->clientList;
	struct rtk_igmp_clientEntry *nextClientEntry=NULL;
	//IGMP_CTRL("In rtk_igmp_checkGroupEntryTimer");
	while(clientEntry!=NULL)
	{
		nextClientEntry=clientEntry->next;
#if defined(CONFIG_RTL_PER_PORT_CLIENT_NUM)
                if((clientEntry->flag&FLAG_WAIT_STATUS)==0)
#endif
		rtk_igmp_checkClientEntryTimer(moduleIndex,groupEntry, clientEntry);
		clientEntry=nextClientEntry;
	}

	if(groupEntry->clientList==NULL) /*none active client*/
	{
		rtk_igmp_deleteGroupEntry(moduleIndex,groupEntry,hashTable);
		//IGMP_CTRL("Check GIP.Clientt Timer event");
	}

}

int32 rtk_igmp_maintainMulticastSnoopingTimerList(uint32 currentSystemTime)
{
	/* maintain current time */
	uint32 i=0;
	uint32 maxTime=0xffffffff;

	struct rtk_igmp_groupEntry* groupEntryPtr=NULL;
	struct rtk_igmp_groupEntry* nextEntry=NULL;
	int32 moduleIndex;

	//IGMP_CTRL("Timer Expire");

	/*handle timer conter overflow*/
	if(currentSystemTime>rtk_igmp_startTime)
	{
		rtk_igmp_sysUpSeconds=currentSystemTime-rtk_igmp_startTime;
	}
	else
	{
		rtk_igmp_sysUpSeconds=(maxTime-rtk_igmp_startTime)+currentSystemTime+1;
	}

	for(moduleIndex=0; moduleIndex<IGMP_MAX_BR_MODULE_NUM; moduleIndex++)
	{
		if(!rtk_igmp_mCastModuleArray[moduleIndex].validBit)
			continue;

		/*maintain ipv4 group entry  timer */
		for(i=0; i<IGMP_HASH_TBL_SIZE; i++)
		{

			/*scan the hash table*/
			if(rtk_igmp_mCastModuleArray[moduleIndex].rtk_igmp_ipv4HashTable!=NULL)
			{
				groupEntryPtr=rtk_igmp_mCastModuleArray[moduleIndex].rtk_igmp_ipv4HashTable[i];
				while(groupEntryPtr)              /*traverse each group list*/
				{
					nextEntry=groupEntryPtr->next;
					rtk_igmp_checkGroupEntryTimer(moduleIndex,groupEntryPtr, rtk_igmp_mCastModuleArray[moduleIndex].rtk_igmp_ipv4HashTable);
					groupEntryPtr=nextEntry;/*because expired group entry  will be cleared*/

				}
			}
		}

		/*maintain ipv6 group entry  timer */
		for(i=0; i<IGMP_HASH_TBL_SIZE; i++)
		{
			  /*scan the hash table*/
			if(rtk_igmp_mCastModuleArray[moduleIndex].rtk_igmp_ipv6HashTable!=NULL)
			{
				groupEntryPtr=rtk_igmp_mCastModuleArray[moduleIndex].rtk_igmp_ipv6HashTable[i];
				while(groupEntryPtr)              /*traverse each group list*/
				{
					nextEntry=groupEntryPtr->next;
					rtk_igmp_checkGroupEntryTimer(moduleIndex,groupEntryPtr, rtk_igmp_mCastModuleArray[moduleIndex].rtk_igmp_ipv6HashTable);
					groupEntryPtr=nextEntry;/*because expired group entry  will be cleared*/

				}
			}
		}

		//aging un-tracking flow (include snooping disable flow or flooding flow)
		rtk_igmp_agingMcastFlow(moduleIndex);
		if(igmpSysdb.unknownFlood)
			rtk_igmp_deleteUnknownFloodMacstFlow(moduleIndex);

	}
#if defined(CONFIG_RTL_LAN_COMPATIBILITY)
        if(mCastSnoopingGlobalConfig.igmpCompatibilityEnable)
        {
                switch(igmpcompatibilityStatus)
                {
                        case COMPATIBILITY_IGMPV1:
                                if(igmpV1Timer<rtk_igmp_sysUpSeconds)
                                {
                                        if(igmpV2Timer<rtk_igmp_sysUpSeconds)
                                                igmpcompatibilityStatus=COMPATIBILITY_IGMPV3;
                                        else
                                                igmpcompatibilityStatus=COMPATIBILITY_IGMPV2;
                                }
                                break;
                        case COMPATIBILITY_IGMPV2:
                                if(igmpV2Timer<rtk_igmp_sysUpSeconds)
                                        igmpcompatibilityStatus=COMPATIBILITY_IGMPV3;
                                break;
                        case COMPATIBILITY_IGMPV3:
                                break;
                        default:
                                break;
                }
        }
#endif
	return SUCCESS;
}



#if LINUX_VERSION_CODE >= KERNEL_VERSION(5,10,0)
void rtk_igmp_multicastSysTimerExpired(struct timer_list *t)
#else
void rtk_igmp_multicastSysTimerExpired(uint32 expireDada)
#endif
{
	igmp_spin_lock_bh(igmpSysdb.lock_igmp);
	rtk_igmp_maintainMulticastSnoopingTimerList((uint32)rtk_igmp_getTimeSec());
	mod_timer(&igmpSysTimer,jiffies+gloConf.igmp_sys_timer_sec*CONFIG_HZ);
	igmp_spin_unlock_bh(igmpSysdb.lock_igmp);

}

void rtk_igmp_multicastSysTimerInit(void)
{
	rtk_igmp_startTime=0;
	rtk_igmp_sysUpSeconds=0;
	lastjiffies=jiffies;
#if LINUX_VERSION_CODE >= KERNEL_VERSION(5,10,0)
	timer_setup(&igmpSysTimer, rtk_igmp_multicastSysTimerExpired, 0);
#else
	init_timer(&igmpSysTimer);
	igmpSysTimer.data=SYS_EXPIRED_NORMAL;
	igmpSysTimer.function=(void*)rtk_igmp_multicastSysTimerExpired;
#endif	
	igmpSysTimer.expires=jiffies+gloConf.igmp_sys_timer_sec*CONFIG_HZ;
	mod_timer(&igmpSysTimer,jiffies+gloConf.igmp_sys_timer_sec*CONFIG_HZ);
}

static void rtk_igmp_multicastSysTimerDestroy(void)
{
	if(timer_pending(&igmpSysTimer))
		timer_delete(&igmpSysTimer);
}






#define GROUP_OPERATION
// allocate a group entry from the group entry pool

/*********************************************
			Group list operation
 *********************************************/

/*       find a group address in a group list    */

struct rtk_igmp_groupEntry* rtk_igmp_searchGroupEntry(int32 moduleIndex,uint32 ipVersion,uint32 *multicastAddr, uint16 vlanId, uint16 svlanId)
{
	struct rtk_igmp_groupEntry* groupPtr = NULL;
	int32 hashIndex=0;

	hashIndex=rtk_igmp_hashAlgorithm(ipVersion, multicastAddr);

	if (ipVersion==IP_VERSION4)
	{
		groupPtr=rtk_igmp_mCastModuleArray[moduleIndex].rtk_igmp_ipv4HashTable[hashIndex];
	}
	else
	{
		groupPtr=rtk_igmp_mCastModuleArray[moduleIndex].rtk_igmp_ipv6HashTable[hashIndex];
	}

	while (groupPtr!=NULL)
	{
		if (ipVersion==IP_VERSION4)
		{

			if (multicastAddr[0]==groupPtr->groupAddr[0])
			{

				return groupPtr;

			}
		}
		else
		{
			if(	(multicastAddr[0]==groupPtr->groupAddr[0])&&
				(multicastAddr[1]==groupPtr->groupAddr[1])&&
				(multicastAddr[2]==groupPtr->groupAddr[2])&&
				(multicastAddr[3]==groupPtr->groupAddr[3])
			)
			{

				return groupPtr;
			}
		}

		groupPtr = groupPtr->next;

	}

	return NULL;
}

/*group entry memory management*/
static  struct rtk_igmp_groupEntry* rtk_igmp_initGroupEntryPool(uint32 poolSize)
{
	uint32 idx=0;
	struct rtk_igmp_groupEntry *poolHead=NULL;
	struct rtk_igmp_groupEntry *entryPtr=NULL;

	if (poolSize != IGMP_MAX_GROUP_COUNT)
	{
		goto out;
	}

	/* Allocate memory */
	poolHead = &groupEntryPool[0];

	if (poolHead != NULL)
	{
		memset(poolHead, 0,  (IGMP_MAX_GROUP_COUNT  * sizeof(struct rtk_igmp_groupEntry)));
		entryPtr = poolHead;

		/* link the whole group entry pool */
		for (idx = 0 ; idx < IGMP_MAX_GROUP_COUNT ; idx++, entryPtr++)
		{
			if(idx==0)
				entryPtr->previous=NULL;
			else
				entryPtr->previous=entryPtr-1;
			
			if(idx == (IGMP_MAX_GROUP_COUNT - 1)) //poolSize is 1
				entryPtr->next=NULL;
			else
				entryPtr->next = entryPtr + 1;

		}
	}

out:

	return poolHead;
}


// free a group entry and link it back to the group entry pool, default is link to the pool head
static  void rtk_igmp_freeGroupEntry(struct rtk_igmp_groupEntry* groupEntryPtr)
{
	if (!groupEntryPtr)
	{
		return;
	}

		groupEntryPtr->next = rtk_igmp_groupEntryPool;
		if(rtk_igmp_groupEntryPool!=NULL)
		{
			rtk_igmp_groupEntryPool->previous=groupEntryPtr;
		}
		rtk_igmp_groupEntryPool=groupEntryPtr;
}

/* clear the content of group entry */
static void rtk_igmp_clearGroupEntry(struct rtk_igmp_groupEntry* groupEntry)
{
	if (NULL!=groupEntry)
	{
		memset(groupEntry, 0, sizeof(struct rtk_igmp_groupEntry));
	}
}

/* unlink a group entry from group list */
static void rtk_igmp_unlinkGroupEntry(struct rtk_igmp_groupEntry* groupEntry,  struct rtk_igmp_groupEntry ** hashTable)
{
	uint32 hashIndex=0;

	if(NULL==groupEntry)
	{
		return;
	}

	if(hwCb.groupDelCbEvt)
	{
		rtk_igmp_groupCbEvt_t groupCb;
		memset(&groupCb,0,sizeof(groupCb));
		groupCb.isIPv6= groupEntry->ipVersion==IP_VERSION6?1:0;
		groupCb.vlan= groupEntry->vlanId ;
		groupCb.svlan = groupEntry->svlanId ;
		memcpy(groupCb.group ,groupEntry->groupAddr,sizeof(groupCb.group));
		IGMP_CTRL("GROUP DEL CALL BACK2 HW");
		hwCb.groupDelCbEvt(&groupCb);
	}

	hashIndex=rtk_igmp_hashAlgorithm(groupEntry->ipVersion, groupEntry->groupAddr);
	/* unlink entry node*/
	if(groupEntry==hashTable[hashIndex]) /*unlink group list head*/
	{
		hashTable[hashIndex]=groupEntry->next;
		if(hashTable[hashIndex]!=NULL)
		{
			hashTable[hashIndex]->previous=NULL;
		}
	}
	else
	{
		if(groupEntry->previous!=NULL)
		{
			groupEntry->previous->next=groupEntry->next;
		}

		if(groupEntry->next!=NULL)
		{
			groupEntry->next->previous=groupEntry->previous;
		}
	}

	groupEntry->previous=NULL;
	groupEntry->next=NULL;


}



static void rtk_igmp_deleteGroupEntry( int32 moduleIndex, struct rtk_igmp_groupEntry* groupEntry,struct rtk_igmp_groupEntry ** hashTable)
{
	if (groupEntry!=NULL)
	{
		rtk_igmp_deleteClientList(groupEntry);
		rtk_igmp_invalidateMCastGroupFlow(moduleIndex, groupEntry->ipVersion, groupEntry->groupAddr);
		rtk_igmp_unlinkGroupEntry(groupEntry, hashTable);
		rtk_igmp_clearGroupEntry(groupEntry);
		rtk_igmp_freeGroupEntry(groupEntry);
	}

}


static  struct rtk_igmp_groupEntry* rtk_igmp_allocateGroupEntry(void)
{
	struct rtk_igmp_groupEntry *ret = NULL;

	if (rtk_igmp_groupEntryPool!=NULL)
	{
		ret = rtk_igmp_groupEntryPool;
		if(rtk_igmp_groupEntryPool->next!=NULL)
		{
			rtk_igmp_groupEntryPool->next->previous=NULL;
		}
		rtk_igmp_groupEntryPool = rtk_igmp_groupEntryPool->next;
		memset(ret, 0, sizeof(struct rtk_igmp_groupEntry));
	}

	return ret;
}


/* link group Entry in the front of a group list */
static void  rtk_igmp_linkGroupEntry(struct rtk_igmp_groupEntry* groupEntry ,  struct rtk_igmp_groupEntry ** hashTable)
{
	uint32 hashIndex=0;

	if(NULL==groupEntry)
	{
		return;
	}
	atomic_inc(&groupIncIdx);
	groupEntry->groupIdx=atomic_read(&groupIncIdx);

	hashIndex=rtk_igmp_hashAlgorithm(groupEntry->ipVersion, groupEntry->groupAddr);
	if(hashTable[hashIndex]!=NULL)
	{
		hashTable[hashIndex]->previous=groupEntry;
	}
	groupEntry->next = hashTable[hashIndex];
	hashTable[hashIndex]=groupEntry;
	hashTable[hashIndex]->previous=NULL;

	if(hwCb.groupAddCbEvt)
	{
		rtk_igmp_groupCbEvt_t groupCb;
		memset(&groupCb,0,sizeof(groupCb));
		groupCb.isIPv6= groupEntry->ipVersion==IP_VERSION6?1:0;
		groupCb.vlan= groupEntry->vlanId ;
		groupCb.svlan= groupEntry->svlanId ;
		memcpy(groupCb.group ,groupEntry->groupAddr,sizeof(groupCb.group));
		IGMP_CTRL("GROUP ADD CALL BACK2 HW");
		hwCb.groupAddCbEvt(&groupCb);
	}

}


#define CLIENT_OPERATION
/*********************************************
			Client list operation
 *********************************************/
static struct rtk_igmp_clientEntry* rtk_igmp_searchClientEntry(uint32 ipVersion, struct rtk_igmp_groupEntry* groupEntry, uint32 inIfidx, uint32 *clientAddr,rtk_igmp_pktHdr_t *pPkthdr)
{
	struct rtk_igmp_clientEntry* clientPtr = groupEntry->clientList;

	if(clientAddr==NULL)
	{
		return NULL;
	}
	while (clientPtr!=NULL)
	{
		if(ipVersion==IP_VERSION4)
		{
			if((clientPtr->clientAddr[0]==clientAddr[0]) && memcmp(clientPtr->clientMacAddr,pPkthdr->smac,6)==0)
			{
				if( (clientPtr->inIfidx!= inIfidx))
				{
					/*update port number,in case of client change port*/
					IGMP_CTRL("Update Client Port form ifidx %d to %d",clientPtr->inIfidx ,inIfidx);
					clientPtr->inIfidx=inIfidx;
				}
				if(clientPtr->extraPortInfo.valid!=pPkthdr->DevExtPortEn || clientPtr->extraPortInfo.devExtraPortId!=pPkthdr->DevExtPortId)
				{
					IGMP_CTRL("Update Client EXTPort En:%d->%d ExtPortID:%d->%d",clientPtr->extraPortInfo.valid,pPkthdr->DevExtPortEn,clientPtr->extraPortInfo.devExtraPortId,pPkthdr->DevExtPortId);
					clientPtr->extraPortInfo.valid = pPkthdr->DevExtPortEn;
					clientPtr->extraPortInfo.devExtraPortId = pPkthdr->DevExtPortId;
				}
				return clientPtr;
			}

		}
		else
		{
			if(	((clientPtr->clientAddr[0]==clientAddr[0])
				&&(clientPtr->clientAddr[1]==clientAddr[1])
				&&(clientPtr->clientAddr[2]==clientAddr[2])
				&&(clientPtr->clientAddr[3]==clientAddr[3])) &&
				memcmp(clientPtr->clientMacAddr,pPkthdr->smac,6)==0)
			{

				if((clientPtr->inIfidx != inIfidx))
				{
					/*update port number,in case of client change port*/
					IGMP_CTRL("Update Client Port form ifidx %d to %d",clientPtr->inIfidx ,inIfidx);
					clientPtr->inIfidx=inIfidx;
				}
				if(clientPtr->extraPortInfo.valid!=pPkthdr->DevExtPortEn || clientPtr->extraPortInfo.devExtraPortId!=pPkthdr->DevExtPortId)
				{
					IGMP_CTRL("Update Client EXTPort En:%d->%d ExtPortID:%d->%d",clientPtr->extraPortInfo.valid,pPkthdr->DevExtPortEn,clientPtr->extraPortInfo.devExtraPortId,pPkthdr->DevExtPortId);
					clientPtr->extraPortInfo.valid = pPkthdr->DevExtPortEn;
					clientPtr->extraPortInfo.devExtraPortId = pPkthdr->DevExtPortId;
				}				
				return clientPtr;
			}
		}

		clientPtr = clientPtr->next;

	}

	return NULL;
}


static int32 rtk_igmp_getClientFwdIfIdx(struct rtk_igmp_clientEntry * clientEntry, uint32 sysTime)
{
	int32 fwdIfidx=FAIL;
	struct rtk_igmp_sourceEntry * sourcePtr=NULL;;

	 /*exclude mode or dummyClient never expired*/
	if (clientEntry->groupFilterTimer>sysTime )
	{
		fwdIfidx = clientEntry->inIfidx;
	}
	else/*include mode*/
	{
		sourcePtr=clientEntry->sourceList;
		while (sourcePtr!=NULL)
		{
			if (sourcePtr->portTimer>sysTime)
			{
				fwdIfidx = clientEntry->inIfidx;
				break;
			}
			sourcePtr=sourcePtr->next;
		}
	}
	return fwdIfidx;
}


/*client entry memory management*/
static  struct rtk_igmp_clientEntry* rtk_igmp_initClientEntryPool(uint32 poolSize)
{

	uint32 idx=0;
	struct rtk_igmp_clientEntry *poolHead=NULL;
	struct rtk_igmp_clientEntry *entryPtr=NULL;

	if (poolSize != IGMP_MAX_CLIENT_COUNT)
	{
		goto out;
	}

	/* Allocate memory */
	poolHead = &clientEntryPool[0];

	if (poolHead != NULL)
	{
		memset(poolHead, 0,  (IGMP_MAX_CLIENT_COUNT  * sizeof(struct rtk_igmp_clientEntry)));
		entryPtr = poolHead;

		/* link the whole client entry pool */
		for (idx = 0 ; idx < IGMP_MAX_CLIENT_COUNT ; idx++, entryPtr++)
		{
			if(idx==0)
				entryPtr->previous=NULL;
			else
				entryPtr->previous=entryPtr-1;

			
			if(idx == (IGMP_MAX_CLIENT_COUNT - 1))
				entryPtr->next=NULL;
			else
				entryPtr->next = entryPtr + 1;
		}
	}

out:


	return poolHead;

}


/* unlink a client entry from group client list */
static void rtk_igmp_unlinkClientEntry(struct rtk_igmp_groupEntry *groupEntry, struct rtk_igmp_clientEntry* clientEntry)
{
	//IGMP_CTRL("unlink %pI4",&clientEntry->clientAddr);
	if(NULL==clientEntry)
	{
		return;
	}

	if(NULL==groupEntry)
	{
		return;
	}


	/* unlink entry node*/
	if(clientEntry==groupEntry->clientList) /*unlink group list head*/
	{
		groupEntry->clientList=groupEntry->clientList->next;
		if(groupEntry->clientList!=NULL)
		{
			groupEntry->clientList->previous=NULL;
		}

	}
	else
	{
		if(clientEntry->previous!=NULL)
		{
			clientEntry->previous->next=clientEntry->next;
		}

		if(clientEntry->next!=NULL)
		{
			clientEntry->next->previous=clientEntry->previous;
		}
	}

	clientEntry->previous=NULL;
	clientEntry->next=NULL;

#if 0
	{
		switch(clientEntry->igmpVersion)
		{
			case IGMP_V1:	////++
				igmpSnoopingCounterVer_1--;
				break;
			case IGMP_V2:
				igmpSnoopingCounterVer_2--;
				break;
			case IGMP_V3:
				igmpSnoopingCounterVer_3--;
				break;
			case MLD_V1:
				MLDCounterVer_1--;
				break;
			case MLD_V2:
				MLDCounterVer_2--;
				break;
			default:
				break;
		}

		{
			IGMP_WARNING("FIXME");
			//igmpSnoopingCounterVer_Port[clientEntry->portNum][clientEntry->igmpVersion - IGMP_V1]--;
		}

	}
#endif


}

// free a client entry and link it back to the client entry pool, default is link to the pool head
static  void rtk_igmp_freeClientEntry(struct rtk_igmp_clientEntry* clientEntryPtr)
{
	if (!clientEntryPtr)
	{
		return;
	}
	clientEntryPtr->next = rtk_igmp_clientEntryPool;
	if(rtk_igmp_clientEntryPool!=NULL)
	{
		rtk_igmp_clientEntryPool->previous=clientEntryPtr;
	}
	rtk_igmp_clientEntryPool=clientEntryPtr;
}


/* clear the content of client entry */
static void rtk_igmp_clearClientEntry(struct rtk_igmp_clientEntry* clientEntry)
{

	if (NULL!=clientEntry)
	{
		memset(clientEntry, 0, sizeof(struct rtk_igmp_clientEntry));
	}
}

static void rtk_igmp_deleteClientEntry(struct rtk_igmp_groupEntry* groupEntry,struct rtk_igmp_clientEntry *clientEntry)
{

	if (NULL==clientEntry)
	{
		return;
	}

	if (NULL==groupEntry)
	{
		return;
	}
#if defined(CONFIG_RTL_PER_PORT_CLIENT_NUM)
        if((clientEntry->flag & FLAG_WAIT_STATUS)==0)
                rtk_igmp_recoverClientSpace(clientEntry->moduleIndex,clientEntry->ipVersion,clientEntry->inIfidx);
#endif
	rtk_igmp_deleteSourceList(clientEntry);
	rtk_igmp_unlinkClientEntry(groupEntry,clientEntry);
	rtk_igmp_clearClientEntry(clientEntry);
	rtk_igmp_freeClientEntry(clientEntry);
	return;

}

static void rtk_igmp_deleteClientList(struct rtk_igmp_groupEntry* groupEntry)
{

	struct rtk_igmp_clientEntry *clientEntry=NULL;
	struct rtk_igmp_clientEntry *nextClientEntry=NULL;

	if (NULL==groupEntry)
	{
		return;
	}

	clientEntry=groupEntry->clientList;
	while (clientEntry!=NULL)
	{
		nextClientEntry=clientEntry->next;
		rtk_igmp_deleteClientEntry(groupEntry,clientEntry);
		clientEntry=nextClientEntry;
	}
}


// allocate a client entry from the client entry pool
static  struct rtk_igmp_clientEntry* rtk_igmp_allocateClientEntry(uint32 *clientAddr,uint8 *clientMac,int32 ifidx,rtk_igmp_pktHdr_t *pPkthdr)
{
	struct rtk_igmp_clientEntry *client = NULL;

	if (rtk_igmp_clientEntryPool!=NULL)
	{
		client = rtk_igmp_clientEntryPool;

		if(rtk_igmp_clientEntryPool->next!=NULL)
		{

			rtk_igmp_clientEntryPool->next->previous=NULL;
		}
		rtk_igmp_clientEntryPool = rtk_igmp_clientEntryPool->next;
		memset(client, 0, sizeof(struct rtk_igmp_clientEntry));
		
		client->joinSystemSecTime = rtk_igmp_sysUpSeconds;
		client->sourceList=NULL;
		memcpy(client->clientAddr,clientAddr,sizeof(client->clientAddr));
		memcpy(client->clientMacAddr,clientMac,sizeof(client->clientMacAddr));
		client->inIfidx = ifidx;
		client->extraPortInfo.valid=pPkthdr->DevExtPortEn;
		client->extraPortInfo.devExtraPortId=pPkthdr->DevExtPortId;
	}

	return client;
}


/* link client Entry in the front of group client list */
static void  rtk_igmp_linkClientEntry(struct rtk_igmp_groupEntry *groupEntry, struct rtk_igmp_clientEntry* clientEntry )
{
	if(NULL==clientEntry)
	{
		return;
	}

	if(NULL==groupEntry)
	{
		return;
	}

	switch(clientEntry->igmpVersion)
	{
		case IGMP_V1:	////++
			igmpSnoopingCounterVer_1++;

			break;
		case IGMP_V2:
			igmpSnoopingCounterVer_2++;
			break;
		case IGMP_V3:
			igmpSnoopingCounterVer_3++;
			break;
		case MLD_V1:
			MLDCounterVer_1++;
			break;
		case MLD_V2:
			MLDCounterVer_2++;
			break;
		default:
			break;
	}


//ADD_CLIENT_LINK:

	if(groupEntry->clientList!=NULL)
	{
		groupEntry->clientList->previous=clientEntry;
	}
	clientEntry->next = groupEntry->clientList;

	groupEntry->clientList=clientEntry;
	groupEntry->clientList->previous=NULL;


}


#define SOURCE_OPERATION

/*********************************************
			source list operation
 *********************************************/

static struct rtk_igmp_sourceEntry* rtk_igmp_searchSourceEntry(uint32 ipVersion, uint32 *sourceAddr, struct rtk_igmp_clientEntry *clientEntry)
{
	struct rtk_igmp_sourceEntry *sourcePtr=clientEntry->sourceList;
	while(sourcePtr!=NULL)
	{
		if(ipVersion==IP_VERSION4)
		{
			if(sourceAddr[0]==sourcePtr->sourceAddr[0])
			{
				return sourcePtr;
			}
		}
		else
		{
			if(	(sourceAddr[0]==sourcePtr->sourceAddr[0])&&
				(sourceAddr[1]==sourcePtr->sourceAddr[1])&&
				(sourceAddr[2]==sourcePtr->sourceAddr[2])&&
				(sourceAddr[3]==sourcePtr->sourceAddr[3])
			)
			{
				return sourcePtr;
			}
		}
		sourcePtr=sourcePtr->next;
	}

	return NULL;
}



static void rtk_igmp_linkSourceEntry(struct rtk_igmp_clientEntry *clientEntry,  struct rtk_igmp_sourceEntry* entryNode)
{
	if(NULL==entryNode)
	{
		return;
	}

	if(NULL==clientEntry)
	{
		return;
	}


	if(clientEntry->sourceList!=NULL)
	{
		clientEntry->sourceList->previous=entryNode;
	}
	entryNode->next=clientEntry->sourceList;
	clientEntry->sourceList=entryNode;
	clientEntry->sourceList->previous=NULL;

}


static void rtk_igmp_unlinkSourceEntry(struct rtk_igmp_clientEntry *clientEntry, struct rtk_igmp_sourceEntry* sourceEntry)
{
	if(NULL==sourceEntry)
	{
		return;
	}

	if(NULL==clientEntry)
	{
		return;
	}

	/* unlink entry node*/
	if(sourceEntry==clientEntry->sourceList) /*unlink group list head*/
	{
		clientEntry->sourceList=sourceEntry->next;
		if(clientEntry->sourceList!=NULL)
		{
			clientEntry->sourceList ->previous=NULL;
		}
	}
	else
	{
		if(sourceEntry->previous!=NULL)
		{
			sourceEntry->previous->next=sourceEntry->next;
		}

		if(sourceEntry->next!=NULL)
		{
			sourceEntry->next->previous=sourceEntry->previous;
		}
	}

	sourceEntry->previous=NULL;
	sourceEntry->next=NULL;

}


// allocate a source entry from the source entry pool
static  struct rtk_igmp_sourceEntry* rtk_igmp_allocateSourceEntry(void)
{
	struct rtk_igmp_sourceEntry *ret = NULL;


		if (rtk_igmp_sourceEntryPool!=NULL)
		{
			ret = rtk_igmp_sourceEntryPool;
			if(rtk_igmp_sourceEntryPool->next!=NULL)
			{
				rtk_igmp_sourceEntryPool->next->previous=NULL;
			}
			rtk_igmp_sourceEntryPool = rtk_igmp_sourceEntryPool->next;
			memset(ret, 0, sizeof(struct rtk_igmp_sourceEntry));
		}

	return ret;
}


/*source entry memory management*/
static  struct rtk_igmp_sourceEntry* rtk_igmp_initSourceEntryPool(uint32 poolSize)
{
	uint32 idx=0;
	struct rtk_igmp_sourceEntry *poolHead=NULL;
	struct rtk_igmp_sourceEntry *entryPtr=NULL;
	if (poolSize != IGMP_MAX_SOURCE_COUNT)
	{
		goto out;
	}

	/* Allocate memory */
	poolHead = &sourceEntryPool[0];

	if (poolHead != NULL)
	{
		memset(poolHead, 0,  (IGMP_MAX_SOURCE_COUNT  * sizeof(struct rtk_igmp_sourceEntry)));
		entryPtr = poolHead;

		/* link the whole source entry pool */
		for (idx = 0 ; idx < IGMP_MAX_SOURCE_COUNT ; idx++, entryPtr++)
		{
			if(idx==0)
				entryPtr->previous=NULL;
			else
				entryPtr->previous=entryPtr-1;
			
			if(idx == (IGMP_MAX_SOURCE_COUNT - 1))
				entryPtr->next=NULL;
			else
				entryPtr->next = entryPtr + 1;


		}
	}

out:

	return poolHead;
}


static void rtk_igmp_clearSourceEntry(struct rtk_igmp_sourceEntry* sourceEntryPtr)
{
	if (NULL!=sourceEntryPtr)
	{
		memset(sourceEntryPtr, 0, sizeof(struct rtk_igmp_sourceEntry));
	}
}

// free a source entry and link it back to the source entry pool, default is link to the pool head
static  void rtk_igmp_freeSourceEntry(struct rtk_igmp_sourceEntry* sourceEntryPtr)
{
	if (!sourceEntryPtr)
	{
		return;
	}

		sourceEntryPtr->next = rtk_igmp_sourceEntryPool;
		if(rtk_igmp_sourceEntryPool!=NULL)
		{
			rtk_igmp_sourceEntryPool->previous=sourceEntryPtr;
		}

		rtk_igmp_sourceEntryPool=sourceEntryPtr;

}

/*****source entry/client entry/group entry/flow entry operation*****/
static void rtk_igmp_deleteSourceEntry(struct rtk_igmp_clientEntry *clientEntry, struct rtk_igmp_sourceEntry* sourceEntry)
{
	if (clientEntry==NULL)
	{
		return;
	}

	if (sourceEntry!=NULL)
	{
		rtk_igmp_unlinkSourceEntry(clientEntry,sourceEntry);
		rtk_igmp_clearSourceEntry(sourceEntry);
		rtk_igmp_freeSourceEntry(sourceEntry);
	}
}


static void rtk_igmp_deleteSourceList(struct rtk_igmp_clientEntry* clientEntry)
{
	struct rtk_igmp_sourceEntry *sourceEntry=NULL;
	struct rtk_igmp_sourceEntry *nextSourceEntry=NULL;

	sourceEntry=clientEntry->sourceList;
	while (sourceEntry!=NULL)
	{
		nextSourceEntry=sourceEntry->next;
		rtk_igmp_deleteSourceEntry(clientEntry,sourceEntry);
		sourceEntry=nextSourceEntry;
	}
}

#define FLOW_OPERATION
/*********************************************
			multicast flow list operation
 *********************************************/
// allocate a multicast flow entry	from the multicast flow pool
static	struct rtk_igmp_mcastFlowEntry* rtk_igmp_allocateMcastFlowEntry(void)
{
	struct rtk_igmp_mcastFlowEntry *ret = NULL;

		if (rtk_igmp_mcastFlowEntryPool!=NULL)
		{
			ret = rtk_igmp_mcastFlowEntryPool;
			if(rtk_igmp_mcastFlowEntryPool->next!=NULL)
			{
				rtk_igmp_mcastFlowEntryPool->next->previous=NULL;
			}
			rtk_igmp_mcastFlowEntryPool = rtk_igmp_mcastFlowEntryPool->next;
			memset(ret, 0, sizeof(struct rtk_igmp_mcastFlowEntry));
		}


	return ret;
}


static struct rtk_igmp_mcastFlowEntry* rtk_igmp_searchMcastFlowEntry(int32 moduleIndex, uint32 ipVersion, uint32 *serverAddr,uint32 *groupAddr,int32 sport,int32 dport,int16 ingressCvid,int16 ingressSvid,int16 l4proto)
{
	struct rtk_igmp_mcastFlowEntry* mcastFlowPtr = NULL;
	uint32 hashIndex=0;

	if(NULL==serverAddr)
	{
		return NULL;
	}

	if(NULL==groupAddr)
	{
		return NULL;
	}

	hashIndex=rtk_igmp_hashAlgorithm(ipVersion, groupAddr);

	mcastFlowPtr=rtk_igmp_mCastModuleArray[moduleIndex].flowHashTable[hashIndex];
	while (mcastFlowPtr!=NULL)
	{

		if(mcastFlowPtr->ipVersion!=ipVersion)
		{
			goto nextFlow;
		}


		if(ipVersion==IP_VERSION4)
		{
			//IGMP_CTRL("serverAddr =%pI4	mcastFlowPtr->serverAddr=%pI4",&serverAddr[0],&mcastFlowPtr->serverAddr[0]);
			//IGMP_CTRL("groupAddr=%pI4	groupAddr->groupAddr=%pI4",&groupAddr[0],&mcastFlowPtr->groupAddr[0]);
			//IGMP_CTRL("sport=%d dport=%d  mcastFlowPtr->sport=%d  mcastFlowPtr->dport=%d",sport,dport,mcastFlowPtr->sport,mcastFlowPtr->dport);

			if( (serverAddr[0]==mcastFlowPtr->serverAddr[0]) && (groupAddr[0]==mcastFlowPtr->groupAddr[0])&& (sport ==mcastFlowPtr->sport) && (dport ==mcastFlowPtr->dport) &&(ingressCvid ==mcastFlowPtr->ingressCvlan)&&(ingressSvid ==mcastFlowPtr->ingressSvlan)&&(l4proto ==mcastFlowPtr->l4proto) )
			{
				mcastFlowPtr->refreshTime=rtk_igmp_sysUpSeconds;
				return mcastFlowPtr;
			}
		}
		else
		{
			//IGMP_CTRL("serverAddr =%pI6	mcastFlowPtr->serverAddr=%pI6",&serverAddr[0],&mcastFlowPtr->serverAddr[0]);
			//IGMP_CTRL("groupAddr=%pI6	groupAddr->groupAddr=%pI6",&groupAddr[0],&mcastFlowPtr->groupAddr[0]);
			//IGMP_CTRL("sport=%d dport=%d  mcastFlowPtr->sport=%d  mcastFlowPtr->dport=%d",sport,dport,mcastFlowPtr->sport,mcastFlowPtr->dport);
			if(	(serverAddr[0]==mcastFlowPtr->serverAddr[0])
				&&(serverAddr[1]==mcastFlowPtr->serverAddr[1])
				&&(serverAddr[2]==mcastFlowPtr->serverAddr[2])
				&&(serverAddr[3]==mcastFlowPtr->serverAddr[3])
				&&(groupAddr[0]==mcastFlowPtr->groupAddr[0])
				&&(groupAddr[1]==mcastFlowPtr->groupAddr[1])
				&&(groupAddr[2]==mcastFlowPtr->groupAddr[2])
				&&(groupAddr[3]==mcastFlowPtr->groupAddr[3])
				&&(sport ==mcastFlowPtr->sport)
				&&(dport ==mcastFlowPtr->dport)
				&&(ingressCvid ==mcastFlowPtr->ingressCvlan)
				&&(ingressSvid ==mcastFlowPtr->ingressSvlan)
				&&(l4proto ==mcastFlowPtr->l4proto))
			{
				mcastFlowPtr->refreshTime=rtk_igmp_sysUpSeconds;
				return mcastFlowPtr;
			}
		}

nextFlow:

		mcastFlowPtr = mcastFlowPtr->next;

	}

	return NULL;
}

#define UNCTAG -1
#define IF_SOURCECARE 1
#define IF_VLANCARE 1

int32 _rtk_igmp_flowcheck(rtk_igmp_flowCbEvt_t *flowCb)
{

	int ret=0;
#if defined(CONFIG_COMMON_RT_API) && defined(CONFIG_RTK_L34_FLEETCONNTRACK_ENABLE)
	// int action0Active=0,action1Active=0;
	rt_igmp_multicast_group_cfg_t mcConfig;
	
	memset(&mcConfig,0,sizeof(mcConfig));
	mcConfig.groupBehavior=RT_MC_CHECK_UNKNOWN_FLOOD_FLOW;
	mcConfig.is_ipv6=flowCb->isIPv6;

	mcConfig.careIngressCvid=IF_VLANCARE;
	mcConfig.careIngressSvid=IF_VLANCARE;
	mcConfig.careSourceAddress=IF_SOURCECARE;

	if(flowCb->ingressCvlan==FAIL)
		mcConfig.ingress_ctagif=0;
	else
	{
		mcConfig.ingress_ctagif=1;
		mcConfig.ingress_cvid = flowCb->ingressCvlan;
	}
	
	if(flowCb->ingressSvlan==FAIL)
		mcConfig.ingress_stagif=0;
	else
	{
		mcConfig.ingress_stagif=1;
		mcConfig.ingress_svid = flowCb->ingressSvlan;
	}


	if(flowCb->isIPv6)
		memcpy(&mcConfig.source_addr.ipv6[0],&flowCb->sourceAddr[0],sizeof(mcConfig.source_addr));
	else
		mcConfig.source_addr.ipv4=flowCb->sourceAddr[0];


	if(flowCb->isIPv6)
		memcpy(&mcConfig.group_addr.ipv6[0],&flowCb->group[0],sizeof(mcConfig.group_addr));
	else
		mcConfig.group_addr.ipv4=flowCb->group[0];

	
	ret = rt_igmp_multicastFlow_check (&mcConfig);
	//printk("----%s;%d mcConfig->floodFlowDelete =%d\n",__FUNCTION__,__LINE__,mcConfig.floodFlowDelete);
	flowCb->floodflowdelete = mcConfig.floodFlowDelete;


	if(ret)
		IGMP_WARNING("ret=%d Error",ret);

#endif
	return ret;
}

//we only delete timeout multicast flow entry
static int rtk_igmp_deleteUnknownFloodMacstFlow(int32 moduleIndex)
{
	uint32 i;
	struct rtk_igmp_mcastFlowEntry* mcastFlowEntry = NULL;
	struct rtk_igmp_mcastFlowEntry* nextMcastFlowEntry = NULL;
	rtk_igmp_flowCbEvt_t *pFlowCb=&igmpSysdb.flowCb;

	if(rtk_igmp_mCastModuleArray[moduleIndex].flowHashTable==NULL)
		return SUCCESS;

	for (i = 0 ; i < IGMP_HASH_TBL_SIZE ; i++)
	{
		mcastFlowEntry=rtk_igmp_mCastModuleArray[moduleIndex].flowHashTable[i];

		while (mcastFlowEntry!=NULL)
		{
			nextMcastFlowEntry=mcastFlowEntry->next;

			memset(pFlowCb,0,sizeof(*pFlowCb));
            
			if(mcastFlowEntry->ipVersion == IP_VERSION6)
				pFlowCb->isIPv6=1;
			memcpy(pFlowCb->group,mcastFlowEntry->groupAddr,sizeof(pFlowCb->group));
			memcpy(pFlowCb->sourceAddr,mcastFlowEntry->serverAddr,sizeof(pFlowCb->sourceAddr));
			memcpy(pFlowCb->egressDevInfo,mcastFlowEntry->multicastFwdInfo.egressDevInfo,sizeof(pFlowCb->egressDevInfo));
			pFlowCb->ingressCvlan = mcastFlowEntry->ingressCvlan;
			pFlowCb->ingressSvlan = mcastFlowEntry->ingressSvlan;
			//hwCb.flowCheckCbEvt(&flowCb);
			_rtk_igmp_flowcheck(pFlowCb);

			//printk("%s:%d, will check group ip is %x ,flowCb.floodflowdelete =%d\n",__FUNCTION__,__LINE__,mcastFlowEntry->groupAddr[0],flowCb.floodflowdelete);
				
			if(pFlowCb->floodflowdelete)
				rtk_igmp_deleteMcastFlowEntry(mcastFlowEntry, rtk_igmp_mCastModuleArray[moduleIndex].flowHashTable);
			else
				mcastFlowEntry->refreshTime = rtk_igmp_sysUpSeconds;
			
			mcastFlowEntry=nextMcastFlowEntry;
		}
	}


	return SUCCESS;
}

//we only want to aging out the dev which disable snooping .
//delete flow if ((idel >IGMP_MCAST_FLOW_EXPIRE_TIME) && (snooping disable) && (hw no traffic))
int rtk_igmp_agingMcastFlow(int32 moduleIndex)
{
	uint32 i,j;
	uint32 delFlow=1;
	struct rtk_igmp_mcastFlowEntry* mcastFlowEntry = NULL;
	struct rtk_igmp_mcastFlowEntry* nextMcastFlowEntry = NULL;
	rtk_igmp_multicastDeviceInfo_t* devconf=NULL;
	rtk_igmp_flowCbEvt_t *pFlowCb=&igmpSysdb.flowCb;

	if(rtk_igmp_mCastModuleArray[moduleIndex].flowHashTable==NULL)
		return SUCCESS;

	for (i = 0 ; i < IGMP_HASH_TBL_SIZE ; i++)
	{
		mcastFlowEntry=rtk_igmp_mCastModuleArray[moduleIndex].flowHashTable[i];

		while (mcastFlowEntry!=NULL)
		{
			nextMcastFlowEntry=mcastFlowEntry->next;
			/*keep the most recently used entry*/
			if ((mcastFlowEntry->refreshTime+IGMP_MCAST_FLOW_EXPIRE_TIME) < rtk_igmp_sysUpSeconds)
			{
				delFlow=1;
				for( j=0 ; j < IGMP_MAX_DEV_NUM ; j++)
				{
					if(mcastFlowEntry->multicastFwdInfo.egressDevInfo[j].valid)
					{
						devconf = _rtk_igmp_devIfidx_to_devInfo(mcastFlowEntry->multicastFwdInfo.egressDevInfo[j].devIfIdx);
						if( (mcastFlowEntry->ipVersion==IP_VERSION4 && devconf && devconf->devConf.igmp.igmpSnoopingDisable==0)   || 
							(mcastFlowEntry->ipVersion==IP_VERSION6 && devconf && devconf->devConf.igmp6.igmp6SnoopingDisable==0))
						{
							delFlow=0;
						}
					}
					else
						break;
				}
				if(delFlow)
				{
					memset(pFlowCb,0,sizeof(*pFlowCb));
					if(mcastFlowEntry->ipVersion == IP_VERSION6)
						pFlowCb->isIPv6=1;
					memcpy(pFlowCb->group,mcastFlowEntry->groupAddr,sizeof(pFlowCb->group));
					memcpy(pFlowCb->sourceAddr,mcastFlowEntry->serverAddr,sizeof(pFlowCb->sourceAddr));
					memcpy(pFlowCb->egressDevInfo,mcastFlowEntry->multicastFwdInfo.egressDevInfo,sizeof(pFlowCb->egressDevInfo));
					pFlowCb->ingressCvlan = mcastFlowEntry->ingressCvlan;
					pFlowCb->ingressSvlan = mcastFlowEntry->ingressSvlan;
					
					_rtk_igmp_flowcheck(pFlowCb);
					if(pFlowCb->floodflowdelete)
						rtk_igmp_deleteMcastFlowEntry(mcastFlowEntry, rtk_igmp_mCastModuleArray[moduleIndex].flowHashTable);
					else
						mcastFlowEntry->refreshTime = rtk_igmp_sysUpSeconds;

				}
			}
			mcastFlowEntry=nextMcastFlowEntry;
		}
	}


	return SUCCESS;
}


static void rtk_igmp_doMcastFlowRecycle(int32 moduleIndex, uint32 ipVersion)
{
	uint32 i;
	uint32 freeCnt=0;
	struct rtk_igmp_mcastFlowEntry* mcastFlowEntry = NULL;
	struct rtk_igmp_mcastFlowEntry* nextMcastFlowEntry = NULL;
	struct rtk_igmp_mcastFlowEntry* oldestMcastFlowEntry = NULL;

	if(rtk_igmp_mCastModuleArray[moduleIndex].flowHashTable==NULL)
		return ;

	for (i = 0 ; i < IGMP_HASH_TBL_SIZE ; i++)
	{
		mcastFlowEntry=rtk_igmp_mCastModuleArray[moduleIndex].flowHashTable[i];

		if (oldestMcastFlowEntry==NULL)
		{
			oldestMcastFlowEntry=mcastFlowEntry;
		}

		while (mcastFlowEntry!=NULL)
		{
			nextMcastFlowEntry=mcastFlowEntry->next;
			/*keep the most recently used entry*/
			if ((mcastFlowEntry->refreshTime+IGMP_MCAST_FLOW_EXPIRE_TIME) < rtk_igmp_sysUpSeconds)
			{
				rtk_igmp_deleteMcastFlowEntry(mcastFlowEntry,  rtk_igmp_mCastModuleArray[moduleIndex].flowHashTable);
				freeCnt++;
			}
			mcastFlowEntry=nextMcastFlowEntry;
		}
	}

	if(freeCnt>0)
	{
		return;
	}

	/*if too many concurrent flow,we have to do LRU*/
	for (i = 0 ; i < IGMP_HASH_TBL_SIZE ; i++)
	{
		mcastFlowEntry=rtk_igmp_mCastModuleArray[moduleIndex].flowHashTable[i];

		if (oldestMcastFlowEntry==NULL)
		{
			oldestMcastFlowEntry=mcastFlowEntry;
		}

		while (mcastFlowEntry!=NULL)
		{
			nextMcastFlowEntry=mcastFlowEntry->next;
			if (mcastFlowEntry->refreshTime < oldestMcastFlowEntry->refreshTime)
			{
				oldestMcastFlowEntry=mcastFlowEntry;
			}

			mcastFlowEntry=nextMcastFlowEntry;
		}
	}

	if (oldestMcastFlowEntry!=NULL)
	{
		rtk_igmp_deleteMcastFlowEntry(oldestMcastFlowEntry,  rtk_igmp_mCastModuleArray[moduleIndex].flowHashTable);
	}

	return ;
}

static int32 _rtk_igmp_IsflowToDevInfoIsNonValidPort(rtk_igmp_flowToDevInfo_t * egressDevInfo)
{
	if(egressDevInfo[0].valid)
		return FALSE;
	else
		return TRUE;
}


//_rtk_igmp_setEgressDevToFwdInfo
int _rtk_igmp_setEgressDevToflowCb(rtk_igmp_flowCbEvt_t *flowCb ,uint32 egressIfidx,uint8 extraPortValid,uint8 extraPortId,uint8 dmacMcToUcEn,uint8 *mac)
{

	int i,j;
	int32 firstInvalid=IGMP_MAX_DEV_NUM;
	int32 firstsubPortInvalid=IGMP_MAX_EXTDEV_PORT_NUM;
	int32 firstDmacUcToMcInvalid = IGMP_MAX_EXTDEV_MAC_NUM;

	i=0;j=0;
	for(i=0 ;i<IGMP_MAX_DEV_NUM;i++)
	{
		if(flowCb->egressDevInfo[i].valid)
		{
			//forward to same egress dev reuse it
			if(flowCb->egressDevInfo[i].devIfIdx==egressIfidx)
			{
				firstsubPortInvalid=IGMP_MAX_EXTDEV_PORT_NUM;
#if defined(CONFIG_RTL8367_SUPPORT) && defined(CONFIG_RTL9607C_SERIES) && defined(CONFIG_RTK_SKB_MARK2)
				if(extraPortValid)
				{
					for(j=0 ; j<IGMP_MAX_EXTDEV_PORT_NUM ;j++)
					{
						if(flowCb->egressDevInfo[i].subPorts[j]  .valid)
						{
							//already here ignore
							if(flowCb->egressDevInfo[i].subPorts[j].devExtraPortId==extraPortId)
								break;
						}
						else
						{
							if(firstsubPortInvalid == IGMP_MAX_EXTDEV_PORT_NUM)
							{
								firstsubPortInvalid=j;
								break;
							}
						}
					}
					//append new extport
					if(firstsubPortInvalid!=IGMP_MAX_EXTDEV_PORT_NUM)
					{
						flowCb->egressDevInfo[i].subPorts[firstsubPortInvalid].valid=1;
						flowCb->egressDevInfo[i].subPorts[firstsubPortInvalid].devExtraPortId=extraPortId;
					}
				}
#endif
				firstDmacUcToMcInvalid = IGMP_MAX_EXTDEV_MAC_NUM;
#if defined(CONFIG_RTK_FC_WIFI_AMSDU_OFFLOAD_BY_PE) && defined(IGMP_SUPPORT_MCTOUC)
				if(dmacMcToUcEn)
				{
					for(j=0;j<IGMP_MAX_EXTDEV_MAC_NUM;j++)
					{
						if(flowCb->egressDevInfo[i].subDmac[j].valid)
						{
							//already here ignore
							if(memcmp(flowCb->egressDevInfo[i].subDmac[j].dmac,mac,6)==0)
								break;
						}
						else
						{
							if(firstDmacUcToMcInvalid==IGMP_MAX_EXTDEV_MAC_NUM)
							{
								firstDmacUcToMcInvalid=j;
								break;
							}
						}
					
					}
					//append new mac
					if(firstDmacUcToMcInvalid!=IGMP_MAX_EXTDEV_MAC_NUM)
					{
						flowCb->egressDevInfo[i].subDmac[firstDmacUcToMcInvalid].valid=1;
						memcpy(flowCb->egressDevInfo[i].subDmac[firstDmacUcToMcInvalid].dmac,mac,6);
					}
					else
					{
						IGMP_WARNING("IGMP_MAX_EXTDEV_MAC_NUM EXCEED!! ");
					}
				}
#endif				
				return SUCCESS;

			}
				
		}
		else
		{
			if(firstInvalid==IGMP_MAX_DEV_NUM)
			{
				firstInvalid =i;
				break;
			}
			
		}
	}
	if(firstInvalid==IGMP_MAX_DEV_NUM) {IGMP_WARNING("IGMP_MAX_DEV_NUM EXCEED!! "); return FAIL;}

	flowCb->egressDevInfo[firstInvalid].valid=1;
	flowCb->egressDevInfo[firstInvalid].devIfIdx =egressIfidx;


#if defined(CONFIG_RTL8367_SUPPORT) && defined(CONFIG_RTL9607C_SERIES) && defined(CONFIG_RTK_SKB_MARK2)
	if(extraPortValid)
	{
		flowCb->egressDevInfo[firstInvalid].subPorts[0].valid=1;
		flowCb->egressDevInfo[firstInvalid].subPorts[0].devExtraPortId = extraPortId;
	}
#endif
#if defined(CONFIG_RTK_FC_WIFI_AMSDU_OFFLOAD_BY_PE) && defined(IGMP_SUPPORT_MCTOUC)
	if(dmacMcToUcEn)
	{
		flowCb->egressDevInfo[firstInvalid].subDmac[0].valid=1;
		memcpy(flowCb->egressDevInfo[i].subDmac[0].dmac,mac,6);
	}
#endif

	return SUCCESS;
}




// if moduleIndex FAIL Append ALL br to Cb
int rtk_igmp_flowCbAppendOtherBrToCb(int32 moduleIndex, struct rtk_igmp_multicastDataInfo *multicastDataInfo,rtk_igmp_flowCbEvt_t *flowCb)
{
	int i,j,k;
	int extraPortValid,mcToUc;
	struct rtk_igmp_mcastFlowEntry *mcastFlowEntry=NULL;

	i=0;j=0;k=0;
	for(i=0 ; i<IGMP_MAX_BR_MODULE_NUM ;i++ )
	{
		if(moduleIndex==FAIL)
		{
			if(rtk_igmp_mCastModuleArray[i].validBit==0    )
				continue;
		}
		else
		{
			if(rtk_igmp_mCastModuleArray[i].validBit==0    || i==moduleIndex)
				continue;
		}
		
		mcastFlowEntry=rtk_igmp_searchMcastFlowEntry(i, multicastDataInfo->ipVersion, multicastDataInfo->sourceIp, multicastDataInfo->groupAddr,multicastDataInfo->sport,multicastDataInfo->dport,multicastDataInfo->vlanTagif?multicastDataInfo->vlanId:FAIL,multicastDataInfo->svlanTagif?multicastDataInfo->svlanId:FAIL,multicastDataInfo->l4proto);
		if (mcastFlowEntry!=NULL)
		{
			for(j=0 ; j <IGMP_MAX_DEV_NUM ;j++)
			{	
				if(mcastFlowEntry->multicastFwdInfo.egressDevInfo[j].valid)
				{
					extraPortValid=0;
					mcToUc=0;
#if defined(CONFIG_RTL8367_SUPPORT) && defined(CONFIG_RTL9607C_SERIES) && defined(CONFIG_RTK_SKB_MARK2)					
					for(k=0;k<IGMP_MAX_EXTDEV_PORT_NUM;k++)
					{
						if(mcastFlowEntry->multicastFwdInfo.egressDevInfo[j].subPorts[k].valid)
						{
							_rtk_igmp_setEgressDevToflowCb(flowCb,mcastFlowEntry->multicastFwdInfo.egressDevInfo[j].devIfIdx,1,mcastFlowEntry->multicastFwdInfo.egressDevInfo[j].subPorts[k].devExtraPortId,0,NULL);
							extraPortValid=1;
						}
					}
#endif
#if defined(CONFIG_RTK_FC_WIFI_AMSDU_OFFLOAD_BY_PE) && defined(IGMP_SUPPORT_MCTOUC)
					for(k=0;k<IGMP_MAX_EXTDEV_MAC_NUM;k++)
					{
						if(mcastFlowEntry->multicastFwdInfo.egressDevInfo[j].subDmac[k].valid)
						{
							_rtk_igmp_setEgressDevToflowCb(flowCb,mcastFlowEntry->multicastFwdInfo.egressDevInfo[j].devIfIdx,0,0,1,mcastFlowEntry->multicastFwdInfo.egressDevInfo[j].subDmac[k].dmac);
							extraPortValid=1;
						}
					}
#endif					
					if(extraPortValid==0 && mcToUc==0)
						_rtk_igmp_setEgressDevToflowCb(flowCb,mcastFlowEntry->multicastFwdInfo.egressDevInfo[j].devIfIdx,0,0,0,0);

				}
				else
					break;

			}
		}
	}


	return SUCCESS;
}


static int32 rtk_igmp_recordMcastFlow(int32 moduleIndex, struct rtk_igmp_multicastDataInfo *multicastDataInfo, struct rtk_igmp_multicastFwdInfo *multicastFwdInfo)
{
	struct rtk_igmp_mcastFlowEntry *mcastFlowEntry=NULL;
	int32 ret,flowIdx;

	IGMP_CTRL("got in rtk_igmp_recordMcastFlow");
	ret= FAIL;
	flowIdx = FAIL;

	if(multicastFwdInfo==NULL)
	{
		return FAIL;
	}

	if( ( (multicastDataInfo->ipVersion==IP_VERSION4) && (multicastDataInfo->sourceIp[0]==0 || multicastDataInfo->groupAddr[0]==0)) ||
		( (multicastDataInfo->ipVersion==IP_VERSION6) && ((multicastDataInfo->sourceIp[0]==0 &&multicastDataInfo->sourceIp[1]==0 && multicastDataInfo->sourceIp[2]==0 && multicastDataInfo->sourceIp[3]==0) ||
		  (multicastDataInfo->groupAddr[0]==0 && multicastDataInfo->groupAddr[1]==0 && multicastDataInfo->groupAddr[2]==0 && multicastDataInfo->groupAddr[3]==0))))
		{IGMP_CTRL("Error v4/v6 source/group address zero"); return FAIL; }

	mcastFlowEntry=rtk_igmp_searchMcastFlowEntry(moduleIndex, multicastDataInfo->ipVersion, multicastDataInfo->sourceIp, multicastDataInfo->groupAddr,multicastDataInfo->sport,multicastDataInfo->dport,multicastDataInfo->vlanTagif?multicastDataInfo->vlanId:FAIL,multicastDataInfo->svlanTagif?multicastDataInfo->svlanId:FAIL,multicastDataInfo->l4proto);

	if(mcastFlowEntry==NULL)
	{


		if(_rtk_igmp_IsflowToDevInfoIsNonValidPort(multicastFwdInfo->egressDevInfo)) {IGMP_CTRL("Pmsk ==0 not record to igmpMcflow"); return FAIL;}


		mcastFlowEntry=rtk_igmp_allocateMcastFlowEntry();
		if(mcastFlowEntry==NULL)
		{
			rtk_igmp_doMcastFlowRecycle(moduleIndex, multicastDataInfo->ipVersion);

			mcastFlowEntry=rtk_igmp_allocateMcastFlowEntry();
			if(mcastFlowEntry==NULL)
			{
				IGMP_CTRL("run out of multicast flow entry!\n");
				return FAIL;
			}
		}

		if(multicastDataInfo->ipVersion==IP_VERSION4)
		{
			mcastFlowEntry->serverAddr[0]=multicastDataInfo->sourceIp[0];
			mcastFlowEntry->groupAddr[0]=multicastDataInfo->groupAddr[0];

		}
		else
		{
			mcastFlowEntry->serverAddr[0]=multicastDataInfo->sourceIp[0];
			mcastFlowEntry->serverAddr[1]=multicastDataInfo->sourceIp[1];
			mcastFlowEntry->serverAddr[2]=multicastDataInfo->sourceIp[2];
			mcastFlowEntry->serverAddr[3]=multicastDataInfo->sourceIp[3];

			mcastFlowEntry->groupAddr[0]=multicastDataInfo->groupAddr[0];
			mcastFlowEntry->groupAddr[1]=multicastDataInfo->groupAddr[1];
			mcastFlowEntry->groupAddr[2]=multicastDataInfo->groupAddr[2];
			mcastFlowEntry->groupAddr[3]=multicastDataInfo->groupAddr[3];
		}


		mcastFlowEntry->ipVersion=multicastDataInfo->ipVersion;
		mcastFlowEntry->sport=multicastDataInfo->sport;
		mcastFlowEntry->dport=multicastDataInfo->dport;
		mcastFlowEntry->l4proto = multicastDataInfo->l4proto;
		
		if(multicastDataInfo->vlanTagif)
			mcastFlowEntry->ingressCvlan = multicastDataInfo->vlanId;
		else
			mcastFlowEntry->ingressCvlan=FAIL;
		
		if(multicastDataInfo->svlanTagif)
			mcastFlowEntry->ingressSvlan = multicastDataInfo->svlanId;
		else
			mcastFlowEntry->ingressSvlan=FAIL;

		memcpy(&mcastFlowEntry->multicastFwdInfo, multicastFwdInfo, sizeof(struct rtk_igmp_multicastFwdInfo ));

		mcastFlowEntry->refreshTime=rtk_igmp_sysUpSeconds;
		mcastFlowEntry->fwdPktCnt=1;

		rtk_igmp_linkMcastFlowEntry(mcastFlowEntry, rtk_igmp_mCastModuleArray[moduleIndex].flowHashTable);

		//return SUCCESS;

	}
	else
	{

		if(_rtk_igmp_IsflowToDevInfoIsNonValidPort(multicastFwdInfo->egressDevInfo))
		{
			IGMP_CTRL("forward to no-member in software should update to hardware");
			rtk_igmp_deleteMcastFlowEntry(mcastFlowEntry,rtk_igmp_mCastModuleArray[moduleIndex].flowHashTable); //forward to no-member in software should update to hardware
			return SUCCESS;
		}

		mcastFlowEntry->refreshTime=rtk_igmp_sysUpSeconds;
		mcastFlowEntry->fwdPktCnt++;

		if(memcmp(&mcastFlowEntry->multicastFwdInfo.egressDevInfo,multicastFwdInfo->egressDevInfo,sizeof(multicastFwdInfo->egressDevInfo))==0 &&
			mcastFlowEntry->multicastFwdInfo.copy2cpu == multicastFwdInfo->copy2cpu )
		{
			IGMP_CTRL("FwdInfo no-change do not call flowSetCbEvt");
			return SUCCESS;
		}
		
		/*update forward port mask information */
		memcpy(&mcastFlowEntry->multicastFwdInfo, multicastFwdInfo, sizeof(struct rtk_igmp_multicastFwdInfo ));


	}

	/* add/update flow*/
	if(multicastDataInfo->privateField & SKIP_LEARNING)
	{
		IGMP_CTRL("Do not flowSetCbEvt to hwcb because FC SKIP_LEARNING");
	}
	else if(hwCb.flowSetCbEvt)
	{
		rtk_igmp_flowCbEvt_t *pFlowCb=&igmpSysdb.flowCb;
		memset(pFlowCb,0,sizeof(*pFlowCb));
		if(mcastFlowEntry->ipVersion == IP_VERSION6)
			pFlowCb->isIPv6=1;
		memcpy(pFlowCb->group,mcastFlowEntry->groupAddr,sizeof(pFlowCb->group));
		memcpy(pFlowCb->sourceAddr,mcastFlowEntry->serverAddr,sizeof(pFlowCb->sourceAddr));
		memcpy(pFlowCb->egressDevInfo,mcastFlowEntry->multicastFwdInfo.egressDevInfo,sizeof(pFlowCb->egressDevInfo));
		pFlowCb->ingressCvlan = mcastFlowEntry->ingressCvlan;
		pFlowCb->ingressSvlan = mcastFlowEntry->ingressSvlan;
		pFlowCb->copy2cpu = mcastFlowEntry->multicastFwdInfo.copy2cpu;
		rtk_igmp_flowCbAppendOtherBrToCb(moduleIndex,multicastDataInfo,pFlowCb);
		hwCb.flowSetCbEvt(pFlowCb);
		mcastFlowEntry->multicastFwdInfo.hwCbAcc=1;
	}


	return SUCCESS;
}

//check other br has same group
static int32 rtk_igmp_otherModuleHasSameGroup(int32 moduleIndex,uint8 ipVersion,uint32* groupAddr)
{

	int i;
	for(i=0 ; i <IGMP_MAX_BR_MODULE_NUM;i++)
	{
		if(i==moduleIndex)
			continue;
		if(rtk_igmp_mCastModuleArray[i].validBit==0)
			continue;
		if(rtk_igmp_searchGroupEntry(i, ipVersion, groupAddr, 0,0))
			return TRUE;
	}
	return FALSE;

}

// moduleIndex==FAIL search all Br Module
// groupAddress ==NULL search all  group
int rtk_igmp_flow_reflush_update_with_hw(int32 moduleIndex,uint32* groupAddr)
{
	struct rtk_igmp_multicastDataInfo multicastDataInfo;
 	struct rtk_igmp_multicastFwdInfo *p_multicastFwdInfo = &igmpSysdb.mcFwdInfo;
	struct rtk_igmp_mcastFlowEntry *mcastFlowEntry=NULL,*tmp_mcastFlowEntry=NULL;

	int32 hashIndex;
	int32 moduleIndex_start,moduleIndex_end;


	if( 0 <= moduleIndex && moduleIndex < IGMP_MAX_BR_MODULE_NUM )
	{
		moduleIndex_start = moduleIndex;
		moduleIndex_end = moduleIndex+1;
	}
	else
	{
		moduleIndex_start = 0;
		moduleIndex_end =IGMP_MAX_BR_MODULE_NUM;
	}

	for(;moduleIndex_start < moduleIndex_end;moduleIndex_start++)
	{
		if(rtk_igmp_mCastModuleArray[moduleIndex_start].validBit==0)
			continue;

		for(hashIndex=0;hashIndex<IGMP_HASH_TBL_SIZE;hashIndex++)
		{
			mcastFlowEntry=rtk_igmp_mCastModuleArray[moduleIndex_start].flowHashTable[hashIndex];
			while(mcastFlowEntry!=NULL)
			{
				tmp_mcastFlowEntry = mcastFlowEntry->next;
				if(groupAddr==NULL || (groupAddr && memcmp(groupAddr,mcastFlowEntry->groupAddr,sizeof(mcastFlowEntry->groupAddr))==0))
				{
					memset(&multicastDataInfo,0,sizeof(multicastDataInfo));
					multicastDataInfo.dport = mcastFlowEntry->dport;
					multicastDataInfo.sport = mcastFlowEntry->sport;
					multicastDataInfo.ipVersion = mcastFlowEntry->ipVersion;
					multicastDataInfo.l4proto = mcastFlowEntry->l4proto;
					
					multicastDataInfo.vlanTagif =mcastFlowEntry->ingressCvlan==FAIL?0:1;
					if(multicastDataInfo.vlanTagif)
						multicastDataInfo.vlanId = mcastFlowEntry->ingressCvlan;
					
					multicastDataInfo.svlanTagif =mcastFlowEntry->ingressSvlan==FAIL?0:1;
					if(multicastDataInfo.svlanTagif)
						multicastDataInfo.svlanId = mcastFlowEntry->ingressSvlan;
					
					memcpy(multicastDataInfo.groupAddr,mcastFlowEntry->groupAddr,sizeof(mcastFlowEntry->groupAddr));
					memcpy(multicastDataInfo.sourceIp,mcastFlowEntry->serverAddr,sizeof(mcastFlowEntry->serverAddr));

					_rtk_igmp_getMulticastDataFwdInfo(moduleIndex_start,0,&multicastDataInfo,p_multicastFwdInfo);

					//for mcastVlan case we need update other br-member by hardware ,just send to update message
					if(rtk_igmp_otherModuleHasSameGroup(moduleIndex_start,mcastFlowEntry->ipVersion,mcastFlowEntry->groupAddr) 
						&& hwCb.flowUpdateCbEvt 
						&& mcastFlowEntry->multicastFwdInfo.hwCbAcc)
					{
						rtk_igmp_flowCbEvt_t *pFlowCb=&igmpSysdb.flowCb;
						memset(pFlowCb,0,sizeof(*pFlowCb));
						if(mcastFlowEntry->ipVersion == IP_VERSION6)
							pFlowCb->isIPv6=1;
						memcpy(pFlowCb->group,mcastFlowEntry->groupAddr,sizeof(pFlowCb->group));
						memcpy(pFlowCb->sourceAddr,mcastFlowEntry->serverAddr,sizeof(pFlowCb->sourceAddr));
						memcpy(pFlowCb->egressDevInfo,mcastFlowEntry->multicastFwdInfo.egressDevInfo,sizeof(pFlowCb->egressDevInfo));
						pFlowCb->ingressCvlan = mcastFlowEntry->ingressCvlan;
						pFlowCb->ingressSvlan = mcastFlowEntry->ingressSvlan;
						IGMP_CTRL("group update Event");
						hwCb.flowUpdateCbEvt(pFlowCb);
					}

				}
				if(hwCb.groupAddCbEvt && hwCb.groupDelCbEvt)
				{
					rtk_igmp_groupCbEvt_t groupCb;
					memset(&groupCb,0,sizeof(groupCb));
					groupCb.isIPv6 = mcastFlowEntry->ipVersion==IP_VERSION4?0:1;
					memcpy(groupCb.group,mcastFlowEntry->groupAddr,sizeof(mcastFlowEntry->groupAddr));
					//group add and delete will flush hw flow
					hwCb.groupAddCbEvt(&groupCb);
					hwCb.groupDelCbEvt(&groupCb);
				}

				mcastFlowEntry=tmp_mcastFlowEntry;
			}
		}

	}
	return SUCCESS;
}



// moduleIndex==FAIL search all Br Module
// groupAddress ==NULL search all  group
static int32 rtk_igmp_flow_reflush_update(int32 moduleIndex,uint32* groupAddr)
{
	struct rtk_igmp_multicastDataInfo multicastDataInfo;
 	struct rtk_igmp_multicastFwdInfo *p_multicastFwdInfo = &igmpSysdb.mcFwdInfo;
	
	struct rtk_igmp_mcastFlowEntry *mcastFlowEntry=NULL,*tmp_mcastFlowEntry=NULL;

	int32 hashIndex;
	int32 moduleIndex_start,moduleIndex_end;


	if( 0 <= moduleIndex && moduleIndex < IGMP_MAX_BR_MODULE_NUM )
	{
		moduleIndex_start = moduleIndex;
		moduleIndex_end = moduleIndex+1;
	}
	else
	{
		moduleIndex_start = 0;
		moduleIndex_end =IGMP_MAX_BR_MODULE_NUM;
	}

	for(;moduleIndex_start < moduleIndex_end;moduleIndex_start++)
	{
		if(rtk_igmp_mCastModuleArray[moduleIndex_start].validBit==0)
			continue;

		for(hashIndex=0;hashIndex<IGMP_HASH_TBL_SIZE;hashIndex++)
		{
			mcastFlowEntry=rtk_igmp_mCastModuleArray[moduleIndex_start].flowHashTable[hashIndex];
			while(mcastFlowEntry!=NULL)
			{
				tmp_mcastFlowEntry = mcastFlowEntry->next;
				if(groupAddr==NULL || (groupAddr && memcmp(groupAddr,mcastFlowEntry->groupAddr,sizeof(mcastFlowEntry->groupAddr))==0))
				{
					memset(&multicastDataInfo,0,sizeof(multicastDataInfo));
					multicastDataInfo.dport = mcastFlowEntry->dport;
					multicastDataInfo.sport = mcastFlowEntry->sport;
					multicastDataInfo.ipVersion = mcastFlowEntry->ipVersion;
					multicastDataInfo.l4proto = mcastFlowEntry->l4proto;
					
					multicastDataInfo.vlanTagif =mcastFlowEntry->ingressCvlan==FAIL?0:1;
					if(multicastDataInfo.vlanTagif)
						multicastDataInfo.vlanId = mcastFlowEntry->ingressCvlan;
					
					multicastDataInfo.svlanTagif =mcastFlowEntry->ingressSvlan==FAIL?0:1;
					if(multicastDataInfo.svlanTagif)
						multicastDataInfo.svlanId = mcastFlowEntry->ingressSvlan;
					
					memcpy(multicastDataInfo.groupAddr,mcastFlowEntry->groupAddr,sizeof(mcastFlowEntry->groupAddr));
					memcpy(multicastDataInfo.sourceIp,mcastFlowEntry->serverAddr,sizeof(mcastFlowEntry->serverAddr));

					_rtk_igmp_getMulticastDataFwdInfo(moduleIndex_start,0,&multicastDataInfo,p_multicastFwdInfo);

					//for mcastVlan case we need update other br-member by hardware ,just send to update message
					if(rtk_igmp_otherModuleHasSameGroup(moduleIndex_start,mcastFlowEntry->ipVersion,mcastFlowEntry->groupAddr) 
						&& hwCb.flowUpdateCbEvt 
						&& mcastFlowEntry->multicastFwdInfo.hwCbAcc)
					{
						rtk_igmp_flowCbEvt_t *pFlowCb=&igmpSysdb.flowCb;
						memset(pFlowCb,0,sizeof(*pFlowCb));
						if(mcastFlowEntry->ipVersion == IP_VERSION6)
							pFlowCb->isIPv6=1;
						memcpy(pFlowCb->group,mcastFlowEntry->groupAddr,sizeof(pFlowCb->group));
						memcpy(pFlowCb->sourceAddr,mcastFlowEntry->serverAddr,sizeof(pFlowCb->sourceAddr));
						memcpy(pFlowCb->egressDevInfo,mcastFlowEntry->multicastFwdInfo.egressDevInfo,sizeof(pFlowCb->egressDevInfo));
						pFlowCb->ingressCvlan = mcastFlowEntry->ingressCvlan;
						pFlowCb->ingressSvlan = mcastFlowEntry->ingressSvlan;
						IGMP_CTRL("group update Event");
						hwCb.flowUpdateCbEvt(pFlowCb);
					}

				}
				mcastFlowEntry=tmp_mcastFlowEntry;
			}
		}

	}
	return SUCCESS;
}

/*multicast flow entry memory management*/
static  struct rtk_igmp_mcastFlowEntry* rtk_igmp_initMcastFlowEntryPool(uint32 poolSize)
{

	uint32 idx=0;
	struct rtk_igmp_mcastFlowEntry *poolHead=NULL;
	struct rtk_igmp_mcastFlowEntry *entryPtr=NULL;


	if (poolSize != IGMP_MAX_FLOW_COUNT)
	{
		goto out;
	}

	/* Allocate memory */
	poolHead = &mcastFlowEntryPool[0];


	if (poolHead != NULL)
	{
		memset(poolHead, 0,  (IGMP_MAX_FLOW_COUNT  * sizeof(struct rtk_igmp_mcastFlowEntry)));
		entryPtr = poolHead;

		/* link the whole group entry pool */
		for (idx = 0 ; idx < IGMP_MAX_FLOW_COUNT ; idx++, entryPtr++)
		{
			if(idx==0)
				entryPtr->previous=NULL;
			else
				entryPtr->previous=entryPtr-1;

			if (idx == (IGMP_MAX_FLOW_COUNT - 1))
				entryPtr->next = NULL;
			else
				entryPtr->next = entryPtr + 1;
			
		}
	}

out:


	return poolHead;

}

/* link multicast flow entry in the front of a forwarding flow list */

static void  rtk_igmp_linkMcastFlowEntry(struct rtk_igmp_mcastFlowEntry* mcastFlowEntry ,  struct rtk_igmp_mcastFlowEntry ** hashTable)
{
	uint32 hashIndex=0;

	if(NULL==mcastFlowEntry)
	{
		return;
	}

	if(NULL==hashTable)
	{
		return;
	}

	if(hwCb.groupAddCbEvt)
	{
		rtk_igmp_groupCbEvt_t groupCb;
		memset(&groupCb,0,sizeof(groupCb));
		groupCb.isIPv6= mcastFlowEntry->ipVersion==IP_VERSION6?1:0;
		groupCb.vlan= mcastFlowEntry->ingressCvlan ;
		groupCb.svlan= mcastFlowEntry->ingressSvlan ;
		memcpy(groupCb.group ,mcastFlowEntry->groupAddr,sizeof(groupCb.group));
		IGMP_CTRL("GROUP ADD CALL BACK2 HW");
		hwCb.groupAddCbEvt(&groupCb);
	}

	hashIndex=rtk_igmp_hashAlgorithm(mcastFlowEntry->ipVersion, mcastFlowEntry->groupAddr);
	if(hashTable[hashIndex]!=NULL)
	{
		hashTable[hashIndex]->previous=mcastFlowEntry;
	}
	mcastFlowEntry->next = hashTable[hashIndex];
	hashTable[hashIndex]=mcastFlowEntry;
	hashTable[hashIndex]->previous=NULL;
	return;

}


static void rtk_igmp_invalidateMCastGroupFlow(int32 moduleIndex,uint32 ipVersion, uint32 *groupAddr)
{
	uint32 hashIndex;
	struct rtk_igmp_mcastFlowEntry* mcastFlowEntry = NULL;
	struct rtk_igmp_mcastFlowEntry* nextMcastFlowEntry = NULL;

	if (NULL==groupAddr)
	{
		return ;
	}
	else if((groupAddr[0]==0)&&(groupAddr[1]==0)&&(groupAddr[2]==0)&&(groupAddr[3]==0))
	{
		//clear all flow by ip version
		for(hashIndex=0;hashIndex<IGMP_HASH_TBL_SIZE;hashIndex++)
		{
			mcastFlowEntry=rtk_igmp_mCastModuleArray[moduleIndex].flowHashTable[hashIndex];
			while (mcastFlowEntry!=NULL)
			{
				nextMcastFlowEntry=mcastFlowEntry->next;
				if(ipVersion==mcastFlowEntry->ipVersion)
					rtk_igmp_deleteMcastFlowEntry(mcastFlowEntry, rtk_igmp_mCastModuleArray[moduleIndex].flowHashTable);
				mcastFlowEntry = nextMcastFlowEntry;
			}
		}
	}
	else
	{
		hashIndex=rtk_igmp_hashAlgorithm(ipVersion, groupAddr);

		mcastFlowEntry=rtk_igmp_mCastModuleArray[moduleIndex].flowHashTable[hashIndex];

		while (mcastFlowEntry!=NULL)
		{
			nextMcastFlowEntry=mcastFlowEntry->next;

			if(ipVersion==mcastFlowEntry->ipVersion)
			{
				if(ipVersion ==IP_VERSION4)
				{
					if ((mcastFlowEntry->groupAddr[0]==groupAddr[0]))
					{
						rtk_igmp_deleteMcastFlowEntry(mcastFlowEntry, rtk_igmp_mCastModuleArray[moduleIndex].flowHashTable);
					}
				}
				else if (ipVersion ==IP_VERSION6)
				{
					if ((mcastFlowEntry->groupAddr[0]==groupAddr[0])&&(mcastFlowEntry->groupAddr[1]==groupAddr[1])&&
					(mcastFlowEntry->groupAddr[2]==groupAddr[2])&&(mcastFlowEntry->groupAddr[3]==groupAddr[3]))
					{
						rtk_igmp_deleteMcastFlowEntry(mcastFlowEntry, rtk_igmp_mCastModuleArray[moduleIndex].flowHashTable);
					}
				}
			}
			mcastFlowEntry = nextMcastFlowEntry;
		}
	}
	return ;
}


/* unlink a multicast flow entry*/
static void rtk_igmp_unlinkMcastFlowEntry(struct rtk_igmp_mcastFlowEntry* mcastFlowEntry,  struct rtk_igmp_mcastFlowEntry ** hashTable)
{
	uint32 hashIndex=0;
	if(NULL==mcastFlowEntry)
	{
		return;
	}

	if(hwCb.groupDelCbEvt)
	{
		rtk_igmp_groupCbEvt_t groupCb;
		memset(&groupCb,0,sizeof(groupCb));
		groupCb.isIPv6= mcastFlowEntry->ipVersion==IP_VERSION6?1:0;
		groupCb.vlan= mcastFlowEntry->ingressCvlan ;
		groupCb.svlan= mcastFlowEntry->ingressSvlan ;
		memcpy(groupCb.group ,mcastFlowEntry->groupAddr,sizeof(groupCb.group));
		IGMP_CTRL("GROUP ADD CALL BACK2 HW");
		hwCb.groupDelCbEvt(&groupCb);
	}


	hashIndex=rtk_igmp_hashAlgorithm(mcastFlowEntry->ipVersion, mcastFlowEntry->groupAddr);
	/* unlink entry node*/
	if(mcastFlowEntry==hashTable[hashIndex]) /*unlink flow list head*/
	{
		hashTable[hashIndex]=mcastFlowEntry->next;
		if(hashTable[hashIndex]!=NULL)
		{
			hashTable[hashIndex]->previous=NULL;
		}
	}
	else
	{
		if(mcastFlowEntry->previous!=NULL)
		{
			mcastFlowEntry->previous->next=mcastFlowEntry->next;
		}

		if(mcastFlowEntry->next!=NULL)
		{
			mcastFlowEntry->next->previous=mcastFlowEntry->previous;
		}
	}

	mcastFlowEntry->previous=NULL;
	mcastFlowEntry->next=NULL;


}


/* clear the content of multicast flow entry */
static void rtk_igmp_clearMcastFlowEntry(struct rtk_igmp_mcastFlowEntry* mcastFlowEntry)
{
	if (NULL!=mcastFlowEntry)
	{
		memset(mcastFlowEntry, 0, sizeof(struct rtk_igmp_mcastFlowEntry));
	}
}


// free a multicast flow entry and link it back to the multicast flow entry pool, default is link to the pool head
static  void rtk_igmp_freeMcastFlowEntry(struct rtk_igmp_mcastFlowEntry* mcastFlowEntry)
{
	if (NULL==mcastFlowEntry)
	{
		return;
	}

	mcastFlowEntry->next = rtk_igmp_mcastFlowEntryPool;
	if(rtk_igmp_mcastFlowEntryPool!=NULL)
	{
		rtk_igmp_mcastFlowEntryPool->previous=mcastFlowEntry;
	}
	rtk_igmp_mcastFlowEntryPool=mcastFlowEntry;

}


static void rtk_igmp_deleteMcastFlowEntry( struct rtk_igmp_mcastFlowEntry* mcastFlowEntry, struct rtk_igmp_mcastFlowEntry ** hashTable)
{

	if(hwCb.flowDelCbEvt && mcastFlowEntry->multicastFwdInfo.hwCbAcc)
	{
		rtk_igmp_flowCbEvt_t *pFlowCb=&igmpSysdb.flowCb;
		memset(pFlowCb,0,sizeof(*pFlowCb));
		if(mcastFlowEntry->ipVersion == IP_VERSION6)
			pFlowCb->isIPv6=1;
		memcpy(pFlowCb->group,mcastFlowEntry->groupAddr,sizeof(pFlowCb->group));
		memcpy(pFlowCb->sourceAddr,mcastFlowEntry->serverAddr,sizeof(pFlowCb->sourceAddr));
		pFlowCb->ingressCvlan = mcastFlowEntry->ingressCvlan;
		pFlowCb->ingressSvlan = mcastFlowEntry->ingressSvlan;
		if(hwCb.flowDelCbEvt(pFlowCb))
			IGMP_WARNING("flowDelCbEvt delete fail");
	}

	if(mcastFlowEntry!=NULL)
	{
		rtk_igmp_unlinkMcastFlowEntry(mcastFlowEntry, hashTable);
		rtk_igmp_clearMcastFlowEntry(mcastFlowEntry);
		rtk_igmp_freeMcastFlowEntry(mcastFlowEntry);
	}

	return;
}

void rtk_igmp_deleteFloodMcastFlowEntry( struct rtk_igmp_mcastFlowEntry* mcastFlowEntry, struct rtk_igmp_mcastFlowEntry ** hashTable)
{

	if(mcastFlowEntry!=NULL)
	{
		rtk_igmp_unlinkMcastFlowEntry(mcastFlowEntry, hashTable);
		rtk_igmp_clearMcastFlowEntry(mcastFlowEntry);
		rtk_igmp_freeMcastFlowEntry(mcastFlowEntry);
	}

	return;
}


#define IGMP_RECORED

int32 rtk_igmp_client_cnt(rtk_igmp_pktHdr_t *pPkthdr,uint32 ipVersion, uint32 *groupAddr, uint32 *clientAddr)
{

	int32 hashIndex;
	struct rtk_igmp_groupEntry *groupEntryPtr;
	struct rtk_igmp_clientEntry* clientEntry=NULL;


	if (ipVersion==IP_VERSION4)
	{

		for (hashIndex=0;hashIndex<IGMP_HASH_TBL_SIZE;hashIndex++)
		{
			groupEntryPtr=rtk_igmp_mCastModuleArray[pPkthdr->moduleIndex].rtk_igmp_ipv4HashTable[hashIndex];
			while (groupEntryPtr!=NULL)
			{
				clientEntry=groupEntryPtr->clientList;
				while (clientEntry!=NULL)
				{
					if((clientAddr[0])==clientEntry->clientAddr[0])
					{
						if(groupAddr[0]==groupEntryPtr->groupAddr[0])
							return SUCCESS;
						else
							return FAIL;
					}
					clientEntry = clientEntry->next;
				}
				groupEntryPtr=groupEntryPtr->next;
			}

		}
	}


	if(ipVersion==IP_VERSION6 )
	{
		for(hashIndex=0;hashIndex<IGMP_HASH_TBL_SIZE;hashIndex++)
		{
			groupEntryPtr=rtk_igmp_mCastModuleArray[pPkthdr->moduleIndex].rtk_igmp_ipv6HashTable[hashIndex];
			while(groupEntryPtr!=NULL)
			{
				clientEntry=groupEntryPtr->clientList;
				while (clientEntry!=NULL)
				{
					if(memcmp(clientAddr,clientEntry->clientAddr,sizeof(clientEntry->clientAddr)))
					{
						if(memcmp(groupAddr,groupEntryPtr->groupAddr,sizeof(groupEntryPtr->groupAddr)))
							return SUCCESS;
						else
							return FAIL;
					}
					clientEntry = clientEntry->next;
				}
				groupEntryPtr=groupEntryPtr->next;
			}
		}
	}


	return FAIL;



}


rtk_igmp_nf_ret_e_t rtk_igmp_group_limitCheck(rtk_igmp_pktHdr_t *pPkthdr,uint32 ipVersion, uint32 *groupAddr , uint32 *clientAddr ,uint32 *mcSourceAddr,uint8 *saMac,uint32 vlanId,uint32 svlanId ,uint32 devIfindex,rtk_igmp_nf_ret_e_t overLimitAction)
{

	//struct rtk_igmp_groupEntry* groupEntry=NULL;
	int32 hashIndex;
	int32 igmpTotalGroupCnt=0,mldTotalGroupCnt=0,totalGroupCnt=0;
	int32 igmpTotalClientCnt=0,mldTotalClientCnt=0;					//we should deduct same Client
	int32 specificClientJoinIGMPCnt=0,specificClientJoinMLDCnt=0;
	int32 igmpDevGroupCnt=0,mldDevGroupCnt=0;
	int32 igmpGroupAndDevHit=0,mldGroupAndDevHit=0;
	struct rtk_igmp_groupEntry *groupEntryPtr;
	struct rtk_igmp_clientEntry* clientEntry=NULL;

	rtk_igmp_DevConfdb_t *devConf = &pPkthdr->devInfo->devConf;
	rtk_igmp_brConfdb_t *brDevconf = &rtk_igmp_mCastModuleArray[pPkthdr->moduleIndex].brDevInfo.brDevConf;

	if(ipVersion==IP_VERSION4)
	{
		pPkthdr->devInfo->devStatistic.igmp4JoinCnt++;
		rtk_igmp_mCastModuleArray[pPkthdr->moduleIndex].brDevInfo.brStatistic.igmp4JoinCnt++;
	}
	else
	{
		pPkthdr->devInfo->devStatistic.igmp6JoinCnt++;
		rtk_igmp_mCastModuleArray[pPkthdr->moduleIndex].brDevInfo.brStatistic.igmp6JoinCnt++;
	}


	for (hashIndex=0;hashIndex<IGMP_HASH_TBL_SIZE;hashIndex++)
	{
		groupEntryPtr=rtk_igmp_mCastModuleArray[pPkthdr->moduleIndex].rtk_igmp_ipv4HashTable[hashIndex];
		while (groupEntryPtr!=NULL)
		{
			igmpTotalGroupCnt++;
			clientEntry=groupEntryPtr->clientList;
			while (clientEntry!=NULL)
			{
				if(rtk_igmp_client_cnt(pPkthdr,IP_VERSION4,groupEntryPtr->groupAddr,clientEntry->clientAddr)==SUCCESS)
					igmpTotalClientCnt++;
				if(ipVersion==IP_VERSION4 &&  (clientAddr[0])==clientEntry->clientAddr[0])
					specificClientJoinIGMPCnt++;
				if(clientEntry->inIfidx== devIfindex)
					igmpDevGroupCnt++;
				if(ipVersion==IP_VERSION4 && groupAddr[0]==groupEntryPtr->groupAddr[0] && clientEntry->inIfidx== devIfindex)
					igmpGroupAndDevHit=1;

				clientEntry = clientEntry->next;
			}
			groupEntryPtr=groupEntryPtr->next;
		}

	}

	for(hashIndex=0;hashIndex<IGMP_HASH_TBL_SIZE;hashIndex++)
	{
		groupEntryPtr=rtk_igmp_mCastModuleArray[pPkthdr->moduleIndex].rtk_igmp_ipv6HashTable[hashIndex];
		while(groupEntryPtr!=NULL)
		{
			mldTotalGroupCnt++;
			clientEntry=groupEntryPtr->clientList;
			while (clientEntry!=NULL)
			{
				if(rtk_igmp_client_cnt(pPkthdr,IP_VERSION6,groupEntryPtr->groupAddr,clientEntry->clientAddr)==SUCCESS)
					mldTotalClientCnt++;
				if(ipVersion==IP_VERSION6 && !memcmp(clientAddr,clientEntry->clientAddr,sizeof(clientEntry->clientAddr)))
					specificClientJoinMLDCnt++;
				if( clientEntry->inIfidx== devIfindex)
					mldDevGroupCnt++;
				if(ipVersion==IP_VERSION6 && !memcmp(groupAddr,groupEntryPtr->groupAddr,sizeof(groupEntryPtr->groupAddr)) && clientEntry->inIfidx== devIfindex)
					mldGroupAndDevHit=1;


				clientEntry = clientEntry->next;
			}
			groupEntryPtr=groupEntryPtr->next;
		}
	}

	IGMP_CTRL("igmpTotalGroupCnt:%d mldTotalGroupCnt:%d",igmpTotalGroupCnt,mldTotalGroupCnt);
	IGMP_CTRL("igmpTotalClientCnt:%d mldTotalClientCnt:%d",igmpTotalClientCnt,mldTotalClientCnt);
	IGMP_CTRL("igmpDevGroupCnt:%d mldDevGroupCnt:%d",igmpDevGroupCnt,mldDevGroupCnt);
	IGMP_CTRL("specificClientJoinIGMPCnt:%d specificClientJoinMLDCnt:%d",specificClientJoinIGMPCnt,specificClientJoinMLDCnt);

	/* Max system group check*/
	totalGroupCnt = igmpTotalGroupCnt+ mldTotalGroupCnt;
	groupEntryPtr=rtk_igmp_searchGroupEntry(pPkthdr->moduleIndex, ipVersion, groupAddr,vlanId,svlanId);


	if(groupEntryPtr==NULL)
	{
		//(this is new group)
		if(brDevconf->sysIp4MaxGroupSize  && (igmpTotalGroupCnt >=brDevconf->sysIp4MaxGroupSize))
		{
			IGMP_CTRL("Group out of DevBr:%s Limit sysIp4MaxGroupSize=%d	igmpTotalGroupCnt=%d",rtk_igmp_mCastModuleArray[pPkthdr->moduleIndex].brDevInfo.devName,brDevconf->sysIp4MaxGroupSize,igmpTotalGroupCnt);
			return overLimitAction;
		}
		if(brDevconf->sysIp6MaxGroupSize && (mldTotalGroupCnt >=brDevconf->sysIp6MaxGroupSize))
		{
			IGMP_CTRL("Group out of DevBr:%s Limit sysIp6MaxGroupSize=%d	mldTotalGroupCnt=%d",rtk_igmp_mCastModuleArray[pPkthdr->moduleIndex].brDevInfo.devName,brDevconf->sysIp6MaxGroupSize,mldTotalGroupCnt);
			return overLimitAction;

		}
		if(brDevconf->sysMaxGroupSize && (totalGroupCnt >= brDevconf->sysMaxGroupSize))
		{
			IGMP_CTRL("Group out of DevBr:%s Limit sysMaxGroupSize=%d	totalGroupCnt=%d",rtk_igmp_mCastModuleArray[pPkthdr->moduleIndex].brDevInfo.devName,brDevconf->sysMaxGroupSize,totalGroupCnt);
			return overLimitAction;
		}
		
		if(rtk_igmp_weightOverCheck(pPkthdr->devInfo,&rtk_igmp_mCastModuleArray[pPkthdr->moduleIndex].brDevInfo,ipVersion,groupAddr)!=SUCCESS)
		{
			IGMP_CTRL("Ingore Learning By weightOverCheck %s",overLimitAction==RTK_IGMP_NF_DROP?"DROP":overLimitAction==RTK_IGMP_NF_ACCEPT?"ACCEPT":"CONTINUE");
			return overLimitAction;
		}
		if(ipVersion==IP_VERSION4 && (devConf->igmp.igmpMaxGroupSize) && (igmpDevGroupCnt >= devConf->igmp.igmpMaxGroupSize ))
		{
			IGMP_CTRL("GorupPer Dev:%s out of limit igmpMaxGroupSize=%d  igmpPortGroupCnt=%d",pPkthdr->devInfo->devName,devConf->igmp.igmpMaxGroupSize,igmpDevGroupCnt);
			return overLimitAction;
		}
		
		if(ipVersion==IP_VERSION6 && (devConf->igmp6.igmp6MaxGroupSize) && (mldDevGroupCnt >= devConf->igmp6.igmp6MaxGroupSize))
		{
			IGMP_CTRL("GorupPer Dev:%s out of limit igmpMaxGroupSize=%d  mldPortGroupCnt=%d",pPkthdr->devInfo->devName,devConf->igmp6.igmp6MaxGroupSize,mldDevGroupCnt);
			return overLimitAction;
		}

	}
	else 
	{
		
		if(!(mldGroupAndDevHit||igmpGroupAndDevHit))
		{
			//(group exist but not this port)	
			if(ipVersion==IP_VERSION4 && (devConf->igmp.igmpMaxGroupSize) && (igmpDevGroupCnt >= devConf->igmp.igmpMaxGroupSize ))
			{
				IGMP_CTRL("GorupPer Dev:%s out of limit igmpMaxGroupSize=%d  igmpPortGroupCnt=%d",pPkthdr->devInfo->devName,devConf->igmp.igmpMaxGroupSize,igmpDevGroupCnt);
				return overLimitAction;
			}

			if(ipVersion==IP_VERSION6 && (devConf->igmp6.igmp6MaxGroupSize) && (mldDevGroupCnt >= devConf->igmp6.igmp6MaxGroupSize))
			{
				IGMP_CTRL("GorupPer Dev:%s out of limit igmpMaxGroupSize=%d  mldPortGroupCnt=%d",pPkthdr->devInfo->devName,devConf->igmp6.igmp6MaxGroupSize,mldDevGroupCnt);
				return overLimitAction;
			}
		}
		else
		{
			//(group exist and this port resend report do noting)
		}
	}

	if(specificClientJoinIGMPCnt)
	{/* Old Client , Per Client join group Number limit check*/
		uint32 clientInGroup=0;
		if(groupEntryPtr!=NULL)
		{
			clientEntry=groupEntryPtr->clientList;
			while (clientEntry!=NULL)
			{
				if(groupEntryPtr->ipVersion ==IP_VERSION4 &&  (clientAddr[0])==clientEntry->clientAddr[0])
				{
					clientInGroup=1;
				}
				else if (groupEntryPtr->ipVersion ==IP_VERSION6  && memcmp(clientAddr,clientEntry->clientAddr,sizeof(clientEntry->clientAddr)))
				{
					clientInGroup=1;
				}
				clientEntry = clientEntry->next;
			}

		}

		if((groupEntryPtr==NULL || clientInGroup==0) && (brDevconf->perIp4ClientMaxJoinGroupSize) && (specificClientJoinIGMPCnt >= brDevconf->perIp4ClientMaxJoinGroupSize))
		{
			IGMP_CTRL("System DevBr:%s Client out of Limit specificClientJoinIGMPCnt:%d perIp4ClientMaxJoinGroupSize=%d",rtk_igmp_mCastModuleArray[pPkthdr->moduleIndex].brDevInfo.devName,specificClientJoinIGMPCnt,brDevconf->perIp4ClientMaxJoinGroupSize);
			return overLimitAction;
		}
	}
	else
	{ /*New Client Max system Client check*/
		if((brDevconf->sysIp4MaxClientSize) && (igmpTotalClientCnt>=brDevconf->sysIp4MaxClientSize))
		{
			IGMP_CTRL("System DevBr:%s Client out of Limit igmpTotalClientCnt=%d sysIp4MaxClientSize=%d",rtk_igmp_mCastModuleArray[pPkthdr->moduleIndex].brDevInfo.devName,igmpTotalClientCnt,brDevconf->sysIp4MaxClientSize);
			return overLimitAction;
		}

		if((brDevconf->sysMaxClientSize) && ((igmpTotalClientCnt+mldTotalClientCnt)>=brDevconf->sysMaxClientSize))
		{
			IGMP_CTRL("System DevBr:%s Client out of Limit igmpTotalClientCnt+mldTotalClientCnt=%d sysMaxClientSize=%d",rtk_igmp_mCastModuleArray[pPkthdr->moduleIndex].brDevInfo.devName,(igmpTotalClientCnt+mldTotalClientCnt),brDevconf->sysMaxClientSize);
			return overLimitAction;
		} 

	}



	if(specificClientJoinMLDCnt)
	{/* Old Client , Per Client join group Number limit check*/
		uint32 clientInGroup=0;
		if(groupEntryPtr!=NULL)
		{
			clientEntry=groupEntryPtr->clientList;
			while (clientEntry!=NULL)
			{
				if(groupEntryPtr->ipVersion ==IP_VERSION4 &&  (clientAddr[0])==clientEntry->clientAddr[0])
				{
					clientInGroup=1;
				}
				else if (groupEntryPtr->ipVersion ==IP_VERSION6  && memcmp(clientAddr,clientEntry->clientAddr,sizeof(clientEntry->clientAddr)))
				{
					clientInGroup=1;
				}
				clientEntry = clientEntry->next;
			}

		}

		if((groupEntryPtr==NULL || clientInGroup==0) && (brDevconf->perIp6ClientMaxJoinGroupSize) && (specificClientJoinMLDCnt >= brDevconf->perIp6ClientMaxJoinGroupSize))
		{
			IGMP_CTRL("System DevBr:%s Client out of Limit specificClientJoinMLDCnt:%d perClientMaxJoinGroupSize=%d",rtk_igmp_mCastModuleArray[pPkthdr->moduleIndex].brDevInfo.devName,specificClientJoinMLDCnt,brDevconf->perIp6ClientMaxJoinGroupSize);
			return overLimitAction;
		}

	}
	else
	{ /*New Client Max system Client check*/
		if((brDevconf->sysIp6MaxClientSize) && (mldTotalClientCnt>=brDevconf->sysIp6MaxClientSize))
		{
			IGMP_CTRL("System DevBr:%s Client out of Limit mldTotalClientCnt=%d sysIp6MaxClientSize=%d",rtk_igmp_mCastModuleArray[pPkthdr->moduleIndex].brDevInfo.devName,mldTotalClientCnt,brDevconf->sysIp6MaxClientSize);
			return overLimitAction;
		}

		if((brDevconf->sysMaxClientSize) && ((igmpTotalClientCnt+mldTotalClientCnt)>=brDevconf->sysMaxClientSize))
		{
			IGMP_CTRL("System DevBr:%s Client out of Limit igmpTotalClientCnt+mldTotalClientCnt=%d sysMaxClientSize=%d",rtk_igmp_mCastModuleArray[pPkthdr->moduleIndex].brDevInfo.devName,(igmpTotalClientCnt+mldTotalClientCnt),brDevconf->sysMaxClientSize);
			return overLimitAction;
		}
	}

	return RTK_IGMP_NF_CONTINUE;
}



static rtk_igmp_nf_ret_e_t rtk_igmp_processJoin( rtk_igmp_pktHdr_t *pPkthdr, const struct net_device *SrcDev)
{
	rtk_igmp_nf_ret_e_t ret=RTK_IGMP_NF_CONTINUE;
	uint32 groupAddress[4]={0, 0, 0, 0};
	uint32 clientAddr[4]={0, 0, 0, 0};
	struct rtk_igmp_groupEntry* groupEntry=NULL;
	struct rtk_igmp_groupEntry* newGroupEntry=NULL;
	struct rtk_igmp_clientEntry* clientEntry=NULL;
	struct rtk_igmp_clientEntry* newClientEntry=NULL;
	uint32 ipVersion;
	uint8 * pktBuf;
	uint32 hashIndex=0;
	uint32 clientAgingTime=0;
#if defined(CONFIG_RTL_PER_PORT_CLIENT_NUM)
	int enoughClientSpace=TRUE;
#endif

	if(pPkthdr->iph && pPkthdr->ip6h==NULL && pPkthdr->igmph)
	{
		ipVersion= IP_VERSION4;
		clientAgingTime = pPkthdr->devInfo->devConf.igmp.igmpMcMemberAgingInterval;
		pktBuf=(uint8 *)pPkthdr->igmph;
		clientAddr[0]=ntohl(pPkthdr->iph->saddr);
	}
	else if (pPkthdr->iph==NULL && pPkthdr->ip6h && pPkthdr->icmp6h)
	{
		ipVersion= IP_VERSION6;
		clientAgingTime = pPkthdr->devInfo->devConf.igmp6.igmp6McMemberAgingInterval;
		pktBuf=(uint8 *)pPkthdr->icmp6h;
		memcpy(&clientAddr[0],&pPkthdr->ip6h->saddr.s6_addr32[0],sizeof(clientAddr));
	}
	else if (pPkthdr->iph && pPkthdr->ip6h)
	{
		ipVersion= BOTH_IPV4_IPV6;
		IGMP_CTRL("Not support");
		return RTK_IGMP_NF_ACCEPT;
	}
	else
	{
		IGMP_CTRL("Not support");
		return RTK_IGMP_NF_ACCEPT;
	}


	if(ipVersion==IP_VERSION4)
	{
		if(pktBuf[0]==0x12)
		{
			IGMP_CTRL("IGMPv1_Report");
#if defined(CONFIG_RTL_LAN_COMPATIBILITY)
            if(mCastSnoopingGlobalConfig.igmpCompatibilityEnable==0)
            {
                    //ignore v1 report for compatibility purpose
                    return RTK_IGMP_NF_ACCEPT;
            }
            else
            {
                    igmpcompatibilityStatus=COMPATIBILITY_IGMPV1;
                    igmpV1Timer = rtk_igmp_sysUpSeconds + oldVersionHostPresentTimeout;
            }

#endif
			groupAddress[0]=ntohl(((struct igmpv1Pkt *)pktBuf)->groupAddr);
		}

		if(pktBuf[0]==0x16)
		{
			IGMP_CTRL("IGMPv2_Join");
#if defined(CONFIG_RTL_LAN_COMPATIBILITY)
            if(mCastSnoopingGlobalConfig.igmpCompatibilityEnable==0)
            {
                    //ignore v2 report for compatibility purpose
                    return RTK_IGMP_NF_ACCEPT;
            }
            else
            {
                    igmpV2Timer = rtk_igmp_sysUpSeconds + oldVersionHostPresentTimeout;
                    if(igmpcompatibilityStatus==COMPATIBILITY_IGMPV3)
                            igmpcompatibilityStatus=COMPATIBILITY_IGMPV2;
            }
#endif
			groupAddress[0]=ntohl(((struct igmpv2Pkt *)pktBuf)->groupAddr);
		}

	}
	else
	{
		IGMP_CTRL("MLDv1_Join");
		memcpy(groupAddress,((struct mldv1Pkt *)pktBuf)->mCastAddr,sizeof(groupAddress));
	}

{
	//check group to cpu
	rtk_igmp_userResvGroup_t *grp2cpu=NULL;
	grp2cpu = _rtk_igmp_checkGroupToPsTbl(ipVersion, groupAddress);
	if(grp2cpu && grp2cpu->patten.isCopy2cpu==0)
	{
		IGMP_CTRL("IGNORE RESERVED ADDRESS");
		return RTK_IGMP_NF_ACCEPT;
	}
}
	ret = rtk_igmp_group_limitCheck(pPkthdr,ipVersion,groupAddress,clientAddr,NULL,NULL,0,0,pPkthdr->devIfindex,RTK_IGMP_NF_DROP);
	if(ret!=RTK_IGMP_NF_CONTINUE)
	{
		IGMP_CTRL("Limit check FAIL Ingore Snooping learning ret=%d",ret);
		return ret;
	}


	if(rtk_igmp_filterWhiteListCheck(pPkthdr->devInfo,ipVersion,0,groupAddress,clientAddr,NULL,pPkthdr->smac)!= SUCCESS)
	{
		IGMP_CTRL("Ingore Learning By White List check  DROP");
		return RTK_IGMP_NF_DROP;
	}


	hashIndex=rtk_igmp_hashAlgorithm(ipVersion, groupAddress);

	groupEntry=rtk_igmp_searchGroupEntry(pPkthdr->moduleIndex,ipVersion, groupAddress, 0,0);


	if (groupEntry==NULL)   /*means new group address, create new group entry*/
	{
		IGMP_CTRL("record new gip");
		newGroupEntry=rtk_igmp_allocateGroupEntry();

		if(newGroupEntry==NULL)
		{
			IGMP_CTRL("run out of group entry!\n");
			goto out;
		}

		//assert(newGroupEntry->clientList==NULL);

		newGroupEntry->groupAddr[0]=groupAddress[0];
		newGroupEntry->groupAddr[1]=groupAddress[1];
		newGroupEntry->groupAddr[2]=groupAddress[2];
		newGroupEntry->groupAddr[3]=groupAddress[3];

		newGroupEntry->ipVersion=ipVersion;
		newGroupEntry->vlanId= 0;
		newGroupEntry->svlanId= 0;


		if(ipVersion==IP_VERSION4)
		{
			rtk_igmp_linkGroupEntry(newGroupEntry, rtk_igmp_mCastModuleArray[ pPkthdr->moduleIndex].rtk_igmp_ipv4HashTable);
		}
		else
		{
			rtk_igmp_linkGroupEntry(newGroupEntry, rtk_igmp_mCastModuleArray[ pPkthdr->moduleIndex].rtk_igmp_ipv6HashTable);
		}

		groupEntry=newGroupEntry;
	}

	clientEntry=rtk_igmp_searchClientEntry(ipVersion, groupEntry, pPkthdr->devIfindex, clientAddr,pPkthdr);
	if (clientEntry==NULL)
	{
		IGMP_CTRL("record new client");
		newClientEntry=rtk_igmp_allocateClientEntry(clientAddr,pPkthdr->smac,pPkthdr->devIfindex,pPkthdr);
		if (newClientEntry==NULL)
		{
			IGMP_CTRL("run out of client entry!\n");
			goto out;
		}
#if defined(CONFIG_RTL_PER_PORT_CLIENT_NUM)
		enoughClientSpace=rtk_igmp_checkClientEmptySpace(pPkthdr->moduleIndex, ipVersion, pPkthdr->devIfindex, groupAddress, clientAddr);
		if(enoughClientSpace==FALSE)
		{
			newClientEntry->flag |= FLAG_WAIT_STATUS;
		}
		newClientEntry->ipVersion = ipVersion;
		newClientEntry->moduleIndex = pPkthdr->moduleIndex;
#endif

		rtk_igmp_linkClientEntry(groupEntry, newClientEntry);
		clientEntry=newClientEntry;
	}

	if(pPkthdr->ingressCtagif )
	{
		clientEntry->reportCtagif = 1;
		clientEntry->reportVlanId = pPkthdr->ingressCvid;
	}
	
	if(pPkthdr->ingressStagif )
	{
		clientEntry->reportStagif = 1;
		clientEntry->reportSVlanId = pPkthdr->ingressSvid;
	}

	if (ipVersion==IP_VERSION4)
	{
		if (pktBuf[0]==0x12) 
			clientEntry->igmpVersion=IGMP_V1;	
		else
			clientEntry->igmpVersion=IGMP_V2;
	}
	else
		clientEntry->igmpVersion=MLD_V1;

	rtk_igmp_deleteSourceList(clientEntry);
#if defined(CONFIG_RTL_NEW_IGMP_REPORT_BEHAVIOR)
        if(ipVersion==IP_VERSION4)
                clientEntry->groupFilterTimer=rtk_igmp_sysUpSeconds+mCastSnoopingGlobalConfig.groupMemberAgingTime[0];
        else
                clientEntry->groupFilterTimer=rtk_igmp_sysUpSeconds+mCastSnoopingGlobalConfig.groupMemberAgingTime[1];
#else
	clientEntry->groupFilterTimer=rtk_igmp_sysUpSeconds+clientAgingTime;
#endif

#if defined(CONFIG_RTL_PER_PORT_CLIENT_NUM)
        if(clientEntry->flag&FLAG_WAIT_STATUS)
        {
                if(enoughClientSpace==FALSE)
                {
                        rtk_igmp_enterWaitStatus(pPkthdr->moduleIndex, ipVersion);
                        return RTK_IGMP_NF_ACCEPT;
                }
        }
#endif


	rtk_igmp_flow_reflush_update(FAIL,groupAddress);


out:
	return RTK_IGMP_NF_ACCEPT;//(multicastRouterPortMask & (~(1<<portNum)) & IGMP_SUPPORT_PORT_MASK);
}

static  uint32 rtk_igmp_processLeave(rtk_igmp_pktHdr_t *pPkthdr, const struct net_device *SrcDev)
{
	uint32 groupAddress[4]={0, 0, 0, 0};
	uint32 clientAddr[4]={0, 0, 0, 0};
	struct rtk_igmp_groupEntry* groupEntry=NULL;
	struct rtk_igmp_clientEntry *clientEntry=NULL;
	struct rtk_igmp_sourceEntry *sourceEntry=NULL;
	struct rtk_igmp_sourceEntry *nextSourceEntry=NULL;

	uint32 ipVersion;
	uint8 * pktBuf;
	uint32 hashIndex=0;
	uint32 _fastLeave=0;
	uint32 lastAgingTime=0;

	if(pPkthdr->iph && pPkthdr->ip6h==NULL && pPkthdr->igmph)
	{
		ipVersion= IP_VERSION4;
		_fastLeave = pPkthdr->devInfo->devConf.igmp.igmpfastLeave;
		pktBuf=(uint8 *)pPkthdr->igmph;
		clientAddr[0]=ntohl(pPkthdr->iph->saddr);
		lastAgingTime =  pPkthdr->devInfo->devConf.igmp.igmpMcLastMemberAgingInterval;
	}
	else if (pPkthdr->iph==NULL && pPkthdr->ip6h && pPkthdr->icmp6h)
	{
		ipVersion= IP_VERSION6;
		_fastLeave = pPkthdr->devInfo->devConf.igmp6.igmp6fastLeave;
		pktBuf=(uint8 *)pPkthdr->icmp6h;
		memcpy(&clientAddr[0],&pPkthdr->ip6h->saddr.s6_addr32[0],sizeof(clientAddr));
		lastAgingTime = pPkthdr->devInfo->devConf.igmp6.igmp6McLastMemberAgingInterval;
	}
	else if (pPkthdr->iph && pPkthdr->ip6h)
	{
		ipVersion= BOTH_IPV4_IPV6;
		IGMP_CTRL("Not support");
		return RTK_IGMP_NF_ACCEPT;
	}
	else
	{
		IGMP_CTRL("Not support");
		return RTK_IGMP_NF_ACCEPT;
	}

	//uint32 multicastRouterPortMask=rtk_igmp_getMulticastRouterPortMask(moduleIndex, ipVersion, rtk_igmp_sysUpSeconds);
	IGMP_CTRL("IGMP/MLD Leave packet");

#if defined(CONFIG_RTL_LAN_COMPATIBILITY)
        if(ipVersion==IP_VERSION4)
        {
                if(mCastSnoopingGlobalConfig.igmpCompatibilityEnable==0)
                {
                        return RTK_IGMP_NF_ACCEPT;
                }
                else
                {
                        igmpV2Timer = rtk_igmp_sysUpSeconds + oldVersionHostPresentTimeout;
                        if(igmpcompatibilityStatus==COMPATIBILITY_IGMPV1)
                                return RTK_IGMP_NF_ACCEPT;

                        igmpcompatibilityStatus=COMPATIBILITY_IGMPV2;
                }
        }
#endif
	if(ipVersion==IP_VERSION4)
	{
		groupAddress[0]=ntohl(((struct igmpv2Pkt *)pktBuf)->groupAddr);
	}
	else
	{
		memcpy(groupAddress,((struct mldv1Pkt *)pktBuf)->mCastAddr,IPV6ADDRLEN);
	}


	hashIndex=rtk_igmp_hashAlgorithm(ipVersion, groupAddress);

	groupEntry=rtk_igmp_searchGroupEntry(pPkthdr->moduleIndex, ipVersion, groupAddress, 0,0);

	if(groupEntry!=NULL)
	{
		clientEntry=rtk_igmp_searchClientEntry( ipVersion, groupEntry, pPkthdr->devIfindex, clientAddr,pPkthdr);
		if(clientEntry!=NULL)
		{
			if(_fastLeave)
			{
				rtk_igmp_deleteClientEntry(groupEntry, clientEntry);
			}
			else
			{
				sourceEntry = clientEntry->sourceList;
				while(sourceEntry!=NULL)
				{
					nextSourceEntry=sourceEntry->next;
#if defined(CONFIG_RTL_NEW_IGMP_REPORT_BEHAVIOR)
                                        if(ipVersion==IP_VERSION4)
                                        {
                                                if(sourceEntry->portTimer>rtk_igmp_sysUpSeconds+mCastSnoopingGlobalConfig.lastMemberAgingTime[0])
                                                {
                                                        sourceEntry->portTimer=rtk_igmp_sysUpSeconds+mCastSnoopingGlobalConfig.lastMemberAgingTime[0];
                                                }
                                        }
                                        else
                                        {
                                                if(sourceEntry->portTimer>rtk_igmp_sysUpSeconds+mCastSnoopingGlobalConfig.lastMemberAgingTime[1])
                                                {
                                                        sourceEntry->portTimer=rtk_igmp_sysUpSeconds+mCastSnoopingGlobalConfig.lastMemberAgingTime[1];
                                                }
                                        }
#else

					if(sourceEntry->portTimer>rtk_igmp_sysUpSeconds+lastAgingTime)
					{
						sourceEntry->portTimer=rtk_igmp_sysUpSeconds+lastAgingTime;
					}
#endif
					sourceEntry=nextSourceEntry;
				}

#if defined(CONFIG_RTL_NEW_IGMP_REPORT_BEHAVIOR)
                                if(ipVersion==IP_VERSION4)
                                {
                                        if(clientEntry->groupFilterTimer>rtk_igmp_sysUpSeconds+mCastSnoopingGlobalConfig.lastMemberAgingTime[0])
                                        {
                                                clientEntry->groupFilterTimer=rtk_igmp_sysUpSeconds+mCastSnoopingGlobalConfig.lastMemberAgingTime[0];
                                        }
                                }

                                else
                                {
                                        if(clientEntry->groupFilterTimer>rtk_igmp_sysUpSeconds+mCastSnoopingGlobalConfig.lastMemberAgingTime[1])
                                        {
                                                clientEntry->groupFilterTimer=rtk_igmp_sysUpSeconds+mCastSnoopingGlobalConfig.lastMemberAgingTime[1];
                                        }
                                }
#else
				if(clientEntry->groupFilterTimer>rtk_igmp_sysUpSeconds+lastAgingTime)
				{
					clientEntry->groupFilterTimer=rtk_igmp_sysUpSeconds+lastAgingTime;
				}
#endif

			}

		}

	}


	if((groupEntry!=NULL) && (groupEntry->clientList==NULL))
		{
			if(ipVersion==IP_VERSION4)
			{
				rtk_igmp_deleteGroupEntry(pPkthdr->moduleIndex,groupEntry,rtk_igmp_mCastModuleArray[pPkthdr->moduleIndex].rtk_igmp_ipv4HashTable);
			}
			else
			{
				rtk_igmp_deleteGroupEntry(pPkthdr->moduleIndex,groupEntry,rtk_igmp_mCastModuleArray[pPkthdr->moduleIndex].rtk_igmp_ipv6HashTable);
			}
		}


	if(_fastLeave)
	{
		rtk_igmp_flow_reflush_update(FAIL,groupAddress);
	}

	return RTK_IGMP_NF_ACCEPT;
}

static  int32 rtk_igmp_processIsInclude(rtk_igmp_pktHdr_t *pPkthdr, uint32 ipVersion, uint32 *clientAddr, uint8 *pktBuf)
{
	rtk_igmp_nf_ret_e_t ret;
	int32 j=0;
	uint32 groupAddress[4]={0, 0, 0, 0};
	struct rtk_igmp_groupEntry* groupEntry=NULL;
	struct rtk_igmp_groupEntry* newGroupEntry=NULL;
	struct rtk_igmp_clientEntry* clientEntry=NULL;
	struct rtk_igmp_clientEntry* newClientEntry=NULL;
	struct rtk_igmp_sourceEntry *newSourceEntry=NULL;

	uint32 hashIndex=0;
	uint32 clientAgingTime=0;
	uint16 numOfSrc=0;
	uint32 *p_NsourceAddr=NULL;				//Net endian
	uint32 _HsourceAddr[4]={0, 0, 0, 0};	//Host endian
	uint32 _fastLeave=0;
#if defined(CONFIG_RTL_PER_PORT_CLIENT_NUM)
    int enoughClientSpace=TRUE;
#endif


	if(ipVersion==IP_VERSION4)
	{
		IGMP_CTRL("process IGMPv3-IsInclude");
		_fastLeave = pPkthdr->devInfo->devConf.igmp.igmpfastLeave;
		groupAddress[0]=ntohl(((struct groupRecord *)pktBuf)->groupAddr);
		numOfSrc=ntohs(((struct groupRecord *)pktBuf)->numOfSrc);
		p_NsourceAddr=&(((struct groupRecord *)pktBuf)->srcList);
		clientAgingTime = pPkthdr->devInfo->devConf.igmp.igmpMcMemberAgingInterval;

	}
	else
	{
		IGMP_CTRL("process MLDv2-IsInclude");
		_fastLeave = pPkthdr->devInfo->devConf.igmp6.igmp6fastLeave;
		memcpy(groupAddress,((struct mCastAddrRecord *)pktBuf)->mCastAddr,IPV6ADDRLEN);
		clientAgingTime = pPkthdr->devInfo->devConf.igmp6.igmp6McMemberAgingInterval;
		numOfSrc=ntohs(((struct mCastAddrRecord *)pktBuf)->numOfSrc);
		p_NsourceAddr=&(((struct mCastAddrRecord *)pktBuf)->srcList);
	}

	{
		//check group to cpu
		rtk_igmp_userResvGroup_t *grp2cpu=NULL;
		grp2cpu = _rtk_igmp_checkGroupToPsTbl(ipVersion, groupAddress);
		if(grp2cpu && grp2cpu->patten.isCopy2cpu==0)
		{
			IGMP_CTRL("IGNORE RESERVED ADDRESS");
			return RTK_IGMP_NF_ACCEPT;
		}
	}


	ret=rtk_igmp_group_limitCheck(pPkthdr,ipVersion,groupAddress,clientAddr,NULL,NULL,0,0,pPkthdr->devIfindex,RTK_IGMP_NF_DROP);
	if(ret!=RTK_IGMP_NF_CONTINUE)
	{
		IGMP_CTRL("Limit check FAIL Ingore Snooping learning ret=%d",ret);
		return ret;
	}

	if(numOfSrc==1)
	{
		if(ipVersion==IP_VERSION4)
			_HsourceAddr[0] = ntohl(*p_NsourceAddr);
		else
			memcpy(_HsourceAddr,p_NsourceAddr,sizeof(_HsourceAddr));
		
		if(rtk_igmp_filterWhiteListCheck(pPkthdr->devInfo,ipVersion,1,groupAddress,clientAddr,_HsourceAddr,pPkthdr->smac)!= SUCCESS)
		{
			IGMP_CTRL("Ingore Learning By White List check DROP");
			return RTK_IGMP_NF_DROP;
		}
	}
	else
	{
		if(rtk_igmp_filterWhiteListCheck(pPkthdr->devInfo,ipVersion,1,groupAddress,clientAddr,NULL,pPkthdr->smac)!= SUCCESS)
		{
			IGMP_CTRL("Ingore Learning By White List check  DROP");
			return RTK_IGMP_NF_DROP;
		}
	}
	
	hashIndex=rtk_igmp_hashAlgorithm(ipVersion, groupAddress);

	groupEntry=rtk_igmp_searchGroupEntry(pPkthdr->moduleIndex, ipVersion, groupAddress, 0,0);

	if (groupEntry==NULL)   /*means new group address, create new group entry*/
	{
		IGMP_CTRL("New GIP");
		newGroupEntry=rtk_igmp_allocateGroupEntry();
		if(newGroupEntry==NULL)
		{
			IGMP_CTRL("run out of group entry!\n");
			return RTK_IGMP_NF_DROP;
		}

		/*set new multicast entry*/

		newGroupEntry->groupAddr[0]=groupAddress[0];
		newGroupEntry->groupAddr[1]=groupAddress[1];
		newGroupEntry->groupAddr[2]=groupAddress[2];
		newGroupEntry->groupAddr[3]=groupAddress[3];

		newGroupEntry->ipVersion=ipVersion;
		newGroupEntry->vlanId=0;
		newGroupEntry->svlanId=0;

		if(ipVersion==IP_VERSION4)
		{
			rtk_igmp_linkGroupEntry(newGroupEntry, rtk_igmp_mCastModuleArray[pPkthdr->moduleIndex].rtk_igmp_ipv4HashTable);
		}
		else
		{
			rtk_igmp_linkGroupEntry(newGroupEntry, rtk_igmp_mCastModuleArray[pPkthdr->moduleIndex].rtk_igmp_ipv6HashTable);
		}

		groupEntry=newGroupEntry;
	}

	/*from here groupEntry is the same as newGroupEntry*/
	clientEntry=rtk_igmp_searchClientEntry(ipVersion, groupEntry, pPkthdr->devIfindex, clientAddr,pPkthdr);
	if (clientEntry==NULL)
	{
		IGMP_CTRL("New GIP.Clt");
		newClientEntry=rtk_igmp_allocateClientEntry(clientAddr,pPkthdr->smac,pPkthdr->devIfindex,pPkthdr);
		if (newClientEntry==NULL)
		{
			IGMP_CTRL("run out of client entry!\n");
			return RTK_IGMP_NF_DROP;
		}

#if defined(CONFIG_RTL_PER_PORT_CLIENT_NUM)
		enoughClientSpace=rtk_igmp_checkClientEmptySpace(pPkthdr->moduleIndex, ipVersion, pPkthdr->devIfindex, groupAddress, clientAddr);
		if(enoughClientSpace==FALSE)
		{
			newClientEntry->flag |= FLAG_WAIT_STATUS;
		}
		newClientEntry->ipVersion = ipVersion;
		newClientEntry->moduleIndex = pPkthdr->moduleIndex;
#endif
		rtk_igmp_linkClientEntry(groupEntry, newClientEntry);
		clientEntry=newClientEntry;
		clientEntry->groupFilterTimer=rtk_igmp_sysUpSeconds;
	}

	if(pPkthdr->ingressCtagif )
	{
		clientEntry->reportCtagif = 1;
		clientEntry->reportVlanId = pPkthdr->ingressCvid;
	}
	if(pPkthdr->ingressStagif )
	{
		clientEntry->reportStagif = 1;
		clientEntry->reportSVlanId = pPkthdr->ingressSvid;
	}

	if (ipVersion==IP_VERSION4)
		clientEntry->igmpVersion=IGMP_V3;
	else //if (ipVersion==IP_VERSION6)
		clientEntry->igmpVersion=MLD_V2;


	IGMP_CTRL("GIP.Clt");
	/*here to handle the source list*/

	if (_fastLeave)
	{
		IGMP_CTRL("enableFastLeave==TRUE");
		clientEntry->groupFilterTimer=rtk_igmp_sysUpSeconds;
		rtk_igmp_deleteSourceList(clientEntry);
		/*link the new source list*/
		for (j=0; j<numOfSrc; j++)
		{

			if(ipVersion==IP_VERSION4)
				_HsourceAddr[0] = ntohl(*p_NsourceAddr);
			else
				memcpy(_HsourceAddr,p_NsourceAddr,sizeof(_HsourceAddr));

			newSourceEntry=rtk_igmp_allocateSourceEntry();
			if (newSourceEntry==NULL)
			{
				IGMP_CTRL("run out of source entry!\n");
				return RTK_IGMP_NF_DROP;
			}

			if (ipVersion==IP_VERSION4)
			{
				newSourceEntry->sourceAddr[0]=ntohl(p_NsourceAddr[0]);
				p_NsourceAddr++;
			}
			else
			{
				newSourceEntry->sourceAddr[0]=p_NsourceAddr[0];
				newSourceEntry->sourceAddr[1]=p_NsourceAddr[1];
				newSourceEntry->sourceAddr[2]=p_NsourceAddr[2];
				newSourceEntry->sourceAddr[3]=p_NsourceAddr[3];
				p_NsourceAddr=p_NsourceAddr+4;
			}
#if defined(CONFIG_RTL_NEW_IGMP_REPORT_BEHAVIOR)
                        if (ipVersion==IP_VERSION4)
                                newSourceEntry->portTimer=rtk_igmp_sysUpSeconds+mCastSnoopingGlobalConfig.groupMemberAgingTime[0];
                        else
                                newSourceEntry->portTimer=rtk_igmp_sysUpSeconds+mCastSnoopingGlobalConfig.groupMemberAgingTime[1];
#else
			newSourceEntry->portTimer=rtk_igmp_sysUpSeconds+clientAgingTime;
#endif
			rtk_igmp_linkSourceEntry(clientEntry,newSourceEntry);
		}

	}
	else
	{

		for (j=0; j<numOfSrc; j++)
		{
			if(ipVersion==IP_VERSION4)
				_HsourceAddr[0] = ntohl(*p_NsourceAddr);
			else
				memcpy(_HsourceAddr,p_NsourceAddr,sizeof(_HsourceAddr));

			newSourceEntry=rtk_igmp_searchSourceEntry(ipVersion, _HsourceAddr, clientEntry);
			if (clientEntry->groupFilterTimer>rtk_igmp_sysUpSeconds)
			{
				if (newSourceEntry!=NULL) //had recorded source-ip
				{
#if defined(CONFIG_RTL_NEW_IGMP_REPORT_BEHAVIOR)
                	if (ipVersion==IP_VERSION4)
                    	newSourceEntry->portTimer=rtk_igmp_sysUpSeconds+mCastSnoopingGlobalConfig.groupMemberAgingTime[0];
                    else
                    	newSourceEntry->portTimer=rtk_igmp_sysUpSeconds+mCastSnoopingGlobalConfig.groupMemberAgingTime[1];
#else
					newSourceEntry->portTimer=rtk_igmp_sysUpSeconds+clientAgingTime;
#endif
				}
				else
				{
					IGMP_CTRL("New GIP.Clt.Src(%d)",j);
					newSourceEntry=rtk_igmp_allocateSourceEntry();
					if (newSourceEntry==NULL)
					{
						IGMP_CTRL("run out of source entry!\n");
						return RTK_IGMP_NF_DROP;
					}

					if (ipVersion==IP_VERSION4)
					{
						newSourceEntry->sourceAddr[0]=ntohl(p_NsourceAddr[0]);
						IGMP_CTRL("SrcList[%d]=%pI4h" , j, (newSourceEntry->sourceAddr));
					}
					else
					{
						newSourceEntry->sourceAddr[0]=p_NsourceAddr[0];
						newSourceEntry->sourceAddr[1]=p_NsourceAddr[1];
						newSourceEntry->sourceAddr[2]=p_NsourceAddr[2];
						newSourceEntry->sourceAddr[3]=p_NsourceAddr[3];
					}
					rtk_igmp_linkSourceEntry(clientEntry,newSourceEntry);
#if defined(CONFIG_RTL_NEW_IGMP_REPORT_BEHAVIOR)
                    if (ipVersion==IP_VERSION4)
                    	newSourceEntry->portTimer=rtk_igmp_sysUpSeconds+mCastSnoopingGlobalConfig.groupMemberAgingTime[0];
                    else
                    	newSourceEntry->portTimer=rtk_igmp_sysUpSeconds+mCastSnoopingGlobalConfig.groupMemberAgingTime[1];
#else
					newSourceEntry->portTimer=rtk_igmp_sysUpSeconds+clientAgingTime;
#endif
				}
			}
			else
			{
				if (newSourceEntry==NULL)
				{
					IGMP_CTRL("New GIP.Clt.Src(%d)",j);
					newSourceEntry=rtk_igmp_allocateSourceEntry();
					if (newSourceEntry==NULL)
					{
						IGMP_CTRL("run out of source entry!\n");
						return RTK_IGMP_NF_DROP;
					}

					if (ipVersion==IP_VERSION4)
					{
						newSourceEntry->sourceAddr[0]=ntohl(p_NsourceAddr[0]);
						IGMP_CTRL("SrcList[%d]=%pI4h"  , j, (newSourceEntry->sourceAddr));
					}
					else
					{
						newSourceEntry->sourceAddr[0]=p_NsourceAddr[0];
						newSourceEntry->sourceAddr[1]=p_NsourceAddr[1];
						newSourceEntry->sourceAddr[2]=p_NsourceAddr[2];
						newSourceEntry->sourceAddr[3]=p_NsourceAddr[3];
					}

					rtk_igmp_linkSourceEntry(clientEntry,newSourceEntry);
				}
#if defined(CONFIG_RTL_NEW_IGMP_REPORT_BEHAVIOR)
                	if (ipVersion==IP_VERSION4)
                    	newSourceEntry->portTimer=rtk_igmp_sysUpSeconds+mCastSnoopingGlobalConfig.groupMemberAgingTime[0];
                    else
                    	newSourceEntry->portTimer=rtk_igmp_sysUpSeconds+mCastSnoopingGlobalConfig.groupMemberAgingTime[1];
#else
				newSourceEntry->portTimer=rtk_igmp_sysUpSeconds+clientAgingTime;
#endif
			}
			if (ipVersion==IP_VERSION4)
				p_NsourceAddr++;
			else
				p_NsourceAddr=p_NsourceAddr+4;

		} ////: considerate per M-SIP

	}

#if defined(CONFIG_RTL_PER_PORT_CLIENT_NUM)
        if(clientEntry->flag&FLAG_WAIT_STATUS)
        {
                if(enoughClientSpace==FALSE)
                {
                        rtk_igmp_enterWaitStatus(pPkthdr->moduleIndex, ipVersion);
                        return RTK_IGMP_NF_ACCEPT;
                }
        }
#endif

	rtk_igmp_flow_reflush_update(FAIL,groupAddress);
	

	return RTK_IGMP_NF_CONTINUE;
}

static  int32 rtk_igmp_processIsExclude(rtk_igmp_pktHdr_t *pPkthdr, uint32 ipVersion, uint32 *clientAddr, uint8 *pktBuf)
{
	rtk_igmp_nf_ret_e_t ret;
	int32 j=0;
	uint32 groupAddress[4]={0, 0, 0, 0};
	struct rtk_igmp_groupEntry* groupEntry=NULL;
	struct rtk_igmp_groupEntry* newGroupEntry=NULL;
	struct rtk_igmp_clientEntry* clientEntry=NULL;
	struct rtk_igmp_clientEntry* newClientEntry=NULL;
	struct rtk_igmp_sourceEntry *newSourceEntry=NULL;

	uint32 hashIndex=0;
	uint16 numOfSrc=0;
	uint32 *p_NsourceAddr=NULL;				//Net endian
	uint32 _HsourceAddr[4]={0, 0, 0, 0};	//Host endian
	uint32 clientAgingTime=0;
#if defined(CONFIG_RTL_PER_PORT_CLIENT_NUM)
        int enoughClientSpace=TRUE;
#endif

	if(ipVersion==IP_VERSION4)
	{
		groupAddress[0]=ntohl(((struct groupRecord *)pktBuf)->groupAddr);
		numOfSrc=ntohs(((struct groupRecord *)pktBuf)->numOfSrc);
		p_NsourceAddr=&(((struct groupRecord *)pktBuf)->srcList);
		clientAgingTime = pPkthdr->devInfo->devConf.igmp.igmpMcMemberAgingInterval;
		IGMP_CTRL("process IGMPv3-IsExclude");
	}
	else
	{

		memcpy(groupAddress,((struct mCastAddrRecord *)pktBuf)->mCastAddr,IPV6ADDRLEN);
		numOfSrc=ntohs(((struct mCastAddrRecord *)pktBuf)->numOfSrc);
		p_NsourceAddr=&(((struct mCastAddrRecord *)pktBuf)->srcList);
		clientAgingTime = pPkthdr->devInfo->devConf.igmp6.igmp6McMemberAgingInterval;
		IGMP_CTRL("process MLDv2-IsExclude %pI6",groupAddress);
	}

	{
		//check group to cpu
		rtk_igmp_userResvGroup_t *grp2cpu=NULL;
		grp2cpu = _rtk_igmp_checkGroupToPsTbl(ipVersion, groupAddress);
		if(grp2cpu && grp2cpu->patten.isCopy2cpu==0)
		{
			IGMP_CTRL("IGNORE RESERVED ADDRESS");
			return RTK_IGMP_NF_ACCEPT;
		}
	}

#if defined(CONFIG_RTL_LAN_COMPATIBILITY)
        if(ipVersion==IP_VERSION4)
        {
                if(mCastSnoopingGlobalConfig.igmpCompatibilityEnable==0)
                {
                        IGMP_CTRL("not process igmp isExclude packet, igmpCompatibilityEnable =%d",mCastSnoopingGlobalConfig.igmpCompatibilityEnable);
                        return RTK_IGMP_NF_ACCEPT;
                }
        }
#endif


	ret = rtk_igmp_group_limitCheck(pPkthdr,ipVersion,groupAddress,clientAddr,NULL,NULL,0,0,pPkthdr->devIfindex,RTK_IGMP_NF_DROP);
	if(ret!=RTK_IGMP_NF_CONTINUE)
	{
		IGMP_CTRL("Limit check FAIL Ingore Snooping learning ret=%d",ret);
		return ret;
	}
	

	if(rtk_igmp_filterWhiteListCheck(pPkthdr->devInfo,ipVersion,1,groupAddress,clientAddr,NULL,pPkthdr->smac)!= SUCCESS)
	{
		IGMP_CTRL("Ingore Learning By White List check  DROP");
		return RTK_IGMP_NF_DROP;
	}



	hashIndex=rtk_igmp_hashAlgorithm( ipVersion, groupAddress);

	groupEntry=rtk_igmp_searchGroupEntry(pPkthdr->moduleIndex, ipVersion, groupAddress, 0,0);
	if (groupEntry==NULL)   /*means new group address, create new group entry*/
	{
		newGroupEntry=rtk_igmp_allocateGroupEntry();
		if (newGroupEntry==NULL)
		{
			IGMP_CTRL("run out of group entry!\n");
			return RTK_IGMP_NF_DROP;
		}

		/*set new multicast entry*/
		newGroupEntry->vlanId=0;
		newGroupEntry->svlanId=0;		
		
		newGroupEntry->ipVersion=ipVersion;
		if (ipVersion==IP_VERSION4)
		{
			newGroupEntry->groupAddr[0]=groupAddress[0];
			rtk_igmp_linkGroupEntry(newGroupEntry, rtk_igmp_mCastModuleArray[pPkthdr->moduleIndex].rtk_igmp_ipv4HashTable);
		}
		else
		{
			newGroupEntry->groupAddr[0]=groupAddress[0];
			newGroupEntry->groupAddr[1]=groupAddress[1];
			newGroupEntry->groupAddr[2]=groupAddress[2];
			newGroupEntry->groupAddr[3]=groupAddress[3];
			rtk_igmp_linkGroupEntry(newGroupEntry, rtk_igmp_mCastModuleArray[pPkthdr->moduleIndex].rtk_igmp_ipv6HashTable);
		}

		groupEntry=newGroupEntry;
	}

	/*from here groupEntry is the same as  newGroupEntry*/
	clientEntry=rtk_igmp_searchClientEntry(ipVersion, groupEntry, pPkthdr->devIfindex, clientAddr,pPkthdr);
	if (clientEntry==NULL)
	{
		newClientEntry=rtk_igmp_allocateClientEntry(clientAddr,pPkthdr->smac,pPkthdr->devIfindex,pPkthdr);
		if (newClientEntry==NULL)
		{
			IGMP_CTRL("run out of client entry!\n");
			return RTK_IGMP_NF_DROP;
		}

#if defined(CONFIG_RTL_PER_PORT_CLIENT_NUM)
                enoughClientSpace=rtk_igmp_checkClientEmptySpace(pPkthdr->moduleIndex, ipVersion, pPkthdr->devIfindex, groupAddress, clientAddr);
                if(enoughClientSpace==FALSE)
                {
                        newClientEntry->flag |= FLAG_WAIT_STATUS;
                }
                newClientEntry->ipVersion = ipVersion;
                newClientEntry->moduleIndex = pPkthdr->moduleIndex;
#endif
		rtk_igmp_linkClientEntry(groupEntry, newClientEntry);
		clientEntry=newClientEntry;
	}

	if(pPkthdr->ingressCtagif )
	{
		clientEntry->reportCtagif = 1;
		clientEntry->reportVlanId = pPkthdr->ingressCvid;
	}
	if(pPkthdr->ingressStagif )
	{
		clientEntry->reportStagif = 1;
		clientEntry->reportSVlanId = pPkthdr->ingressSvid;
	}


	/*from here clientEntry  is the same as newClientEntry*/
	if (ipVersion==IP_VERSION4)
		clientEntry->igmpVersion=IGMP_V3;
	else //if (ipVersion==IP_VERSION6)
		clientEntry->igmpVersion=MLD_V2;


	/*flush the old source list*/
	// delete ((X-A) | (Y-A)) or (A-B)
	{
		struct rtk_igmp_sourceEntry *sourceEntry=clientEntry->sourceList;
		while (sourceEntry!=NULL)
		{
			sourceEntry->setOpFlag=1;
			sourceEntry=sourceEntry->next;
		}
	}


	/*link the new source list*/
	for (j=0; j<numOfSrc; j++)
	{
		if(ipVersion==IP_VERSION4)
			_HsourceAddr[0] = ntohl(*p_NsourceAddr);
		else
			memcpy(_HsourceAddr,p_NsourceAddr,sizeof(_HsourceAddr));
		newSourceEntry=rtk_igmp_searchSourceEntry(ipVersion, _HsourceAddr, clientEntry);
		if (clientEntry->groupFilterTimer>rtk_igmp_sysUpSeconds) //ex-mo
		{
			if (newSourceEntry!=NULL) // A*X or A*Y
			{
				if (newSourceEntry->portTimer <= rtk_igmp_sysUpSeconds) // A*Y
				{
					//rtk_igmp_deleteSourceEntry(clientEntry,newSourceEntry);
					newSourceEntry->portTimer=rtk_igmp_sysUpSeconds;
				}
				else //A*X
				{
				}
				newSourceEntry->setOpFlag=0;
			}
			else //A-X-Y = GMI
			{
				IGMP_CTRL("New GIP.Clt.Src(%d)",j);
				newSourceEntry=rtk_igmp_allocateSourceEntry();
				if (newSourceEntry==NULL)
				{
					IGMP_CTRL("run out of source entry!\n");
					return RTK_IGMP_NF_DROP;
				}

				if (ipVersion==IP_VERSION4)
				{
					newSourceEntry->sourceAddr[0]=ntohl(p_NsourceAddr[0]);
					IGMP_CTRL("SrcList[%d]=%pI4h"  , j, (newSourceEntry->sourceAddr));
				}
				else
				{
					newSourceEntry->sourceAddr[0]=p_NsourceAddr[0];
					newSourceEntry->sourceAddr[1]=p_NsourceAddr[1];
					newSourceEntry->sourceAddr[2]=p_NsourceAddr[2];
					newSourceEntry->sourceAddr[3]=p_NsourceAddr[3];
				}
				rtk_igmp_linkSourceEntry(clientEntry,newSourceEntry);
				newSourceEntry->portTimer=rtk_igmp_sysUpSeconds+clientAgingTime;
				newSourceEntry->setOpFlag=0;
			}
		}
		else
		{
			if (newSourceEntry==NULL) //B-A
			{
				IGMP_CTRL("New GIP.Clt.Src(Record index:%d)",j);
				newSourceEntry=rtk_igmp_allocateSourceEntry();
				if (newSourceEntry==NULL)
				{
					IGMP_CTRL("run out of source entry!\n");
					return RTK_IGMP_NF_DROP;
				}

				if (ipVersion==IP_VERSION4)
				{
					newSourceEntry->sourceAddr[0]=ntohl(p_NsourceAddr[0]);
					IGMP_CTRL("SrcList[%d]=%pI4h"  , j, (newSourceEntry->sourceAddr));
				}
				else
				{
					newSourceEntry->sourceAddr[0]=p_NsourceAddr[0];
					newSourceEntry->sourceAddr[1]=p_NsourceAddr[1];
					newSourceEntry->sourceAddr[2]=p_NsourceAddr[2];
					newSourceEntry->sourceAddr[3]=p_NsourceAddr[3];
				}
				rtk_igmp_linkSourceEntry(clientEntry,newSourceEntry);
				newSourceEntry->portTimer=rtk_igmp_sysUpSeconds;
			}
			else //B*A
			{
			}
			newSourceEntry->setOpFlag=0;
		}
		if (ipVersion==IP_VERSION4)
			p_NsourceAddr++;
		else
			p_NsourceAddr=p_NsourceAddr+4;

	} ////: considerate per M-SIP

	// delete ((X-A) | (Y-A)) or (A-B)
	{
		struct rtk_igmp_sourceEntry *sourceEntry=clientEntry->sourceList;
		struct rtk_igmp_sourceEntry *nextSourceEntry;
		while (sourceEntry!=NULL)
		{
			nextSourceEntry=sourceEntry->next;
			if (sourceEntry->setOpFlag)
			{
				rtk_igmp_deleteSourceEntry(clientEntry, sourceEntry);
			}
			sourceEntry=nextSourceEntry;
		}
	}

#if defined(CONFIG_RTL_NEW_IGMP_REPORT_BEHAVIOR)
        if (ipVersion==IP_VERSION4)
                clientEntry->groupFilterTimer=rtk_igmp_sysUpSeconds+mCastSnoopingGlobalConfig.groupMemberAgingTime[0];
        else
                clientEntry->groupFilterTimer=rtk_igmp_sysUpSeconds+mCastSnoopingGlobalConfig.groupMemberAgingTime[1];
#else
	clientEntry->groupFilterTimer=rtk_igmp_sysUpSeconds+clientAgingTime;
#endif

#if defined(CONFIG_RTL_PER_PORT_CLIENT_NUM)
        if(clientEntry->flag&FLAG_WAIT_STATUS)
        {
                if(enoughClientSpace==FALSE)
                {
                        rtk_igmp_enterWaitStatus(pPkthdr->moduleIndex, ipVersion);
                        return RTK_IGMP_NF_ACCEPT;
                }
        }
#endif
	rtk_igmp_flow_reflush_update(FAIL,groupAddress);

	//} ////: considerate per M-SIP

	return RTK_IGMP_NF_CONTINUE;

}

static int32 rtk_igmp_processToInclude(rtk_igmp_pktHdr_t *pPkthdr, uint32 ipVersion, uint32 *clientAddr, uint8 *pktBuf)
{
	rtk_igmp_nf_ret_e_t ret;
	int32 j=0;
	uint32 groupAddress[4]={0, 0, 0, 0};
	struct rtk_igmp_groupEntry* groupEntry=NULL;
	struct rtk_igmp_groupEntry* newGroupEntry=NULL;
	struct rtk_igmp_clientEntry* clientEntry=NULL;
	struct rtk_igmp_clientEntry* newClientEntry=NULL;
	struct rtk_igmp_sourceEntry *sourceEntry=NULL;
	//struct rtk_igmp_sourceEntry *nextSourceEntry=NULL;
	struct rtk_igmp_sourceEntry *newSourceEntry=NULL;

	uint32 _fastLeave=0;
	uint32 hashIndex=0;
	uint16 numOfSrc=0;
	uint32 *p_NsourceAddr=NULL; 			//Net endian
	uint32 _HsourceAddr[4]={0, 0, 0, 0};	//Host endian
	uint32 clientAgingTime=0;
	uint32 lastAgingTime=0;

	//uint16 numOfQuerySrc=0;
#if defined(CONFIG_RTL_PER_PORT_CLIENT_NUM)
        int enoughClientSpace=TRUE;
#endif

	if (ipVersion==IP_VERSION4)
	{
		IGMP_CTRL("process IGMPv3-ToInclude");
		_fastLeave = pPkthdr->devInfo->devConf.igmp.igmpfastLeave;
		groupAddress[0]=ntohl(((struct groupRecord *)pktBuf)->groupAddr);
		numOfSrc=ntohs(((struct groupRecord *)pktBuf)->numOfSrc);
		p_NsourceAddr=&(((struct groupRecord *)pktBuf)->srcList);
		clientAgingTime = pPkthdr->devInfo->devConf.igmp.igmpMcMemberAgingInterval;
		lastAgingTime = pPkthdr->devInfo->devConf.igmp.igmpMcLastMemberAgingInterval;
	}
	else
	{
		IGMP_CTRL("process MLDv2-ToInclude");
		_fastLeave = pPkthdr->devInfo->devConf.igmp6.igmp6fastLeave;
		memcpy(groupAddress,((struct mCastAddrRecord *)pktBuf)->mCastAddr,IPV6ADDRLEN);
		numOfSrc=ntohs(((struct mCastAddrRecord *)pktBuf)->numOfSrc);
		p_NsourceAddr=&(((struct mCastAddrRecord *)pktBuf)->srcList);
		clientAgingTime = pPkthdr->devInfo->devConf.igmp6.igmp6McMemberAgingInterval;
		lastAgingTime = pPkthdr->devInfo->devConf.igmp6.igmp6McLastMemberAgingInterval;
	}
	{
		//check group to cpu
		rtk_igmp_userResvGroup_t *grp2cpu=NULL;
		grp2cpu = _rtk_igmp_checkGroupToPsTbl(ipVersion, groupAddress);
		if(grp2cpu && grp2cpu->patten.isCopy2cpu==0)
		{
			IGMP_CTRL("IGNORE RESERVED ADDRESS");
			return RTK_IGMP_NF_ACCEPT;
		}
	}


#if defined(CONFIG_RTL_LAN_COMPATIBILITY)
    if(ipVersion==IP_VERSION4)
    {
            if((mCastSnoopingGlobalConfig.igmpCompatibilityEnable==0) || (igmpcompatibilityStatus==COMPATIBILITY_IGMPV1))
            {
                    IGMP_CTRL("not process igmp toInclude mode packet,igmpCompatibilityEnable =%d,igmpcompatibilityStatus =%d",mCastSnoopingGlobalConfig.igmpCompatibilityEnable,igmpcompatibilityStatus);
                    return RTK_IGMP_NF_ACCEPT;
            }
    }
#endif
	ret=rtk_igmp_group_limitCheck(pPkthdr,ipVersion,groupAddress,clientAddr,NULL,NULL,0,0,pPkthdr->devIfindex,RTK_IGMP_NF_DROP);
	if(ret!=RTK_IGMP_NF_CONTINUE)
	{
		IGMP_CTRL("Limit check FAIL Ingore Snooping learning ret=%d",ret);
		return ret;
	}

	if(numOfSrc==1)
	{
		if(ipVersion==IP_VERSION4)
			_HsourceAddr[0] = ntohl(*p_NsourceAddr);
		else
			memcpy(_HsourceAddr,p_NsourceAddr,sizeof(_HsourceAddr));
		
		if(rtk_igmp_filterWhiteListCheck(pPkthdr->devInfo,ipVersion,1,groupAddress,clientAddr,_HsourceAddr,pPkthdr->smac)!= SUCCESS)
		{
			IGMP_CTRL("Ingore Learning By White List check DROP");
			return RTK_IGMP_NF_DROP;
		}
	}
	else
	{
		if(rtk_igmp_filterWhiteListCheck(pPkthdr->devInfo,ipVersion,1,groupAddress,clientAddr,NULL,pPkthdr->smac)!= SUCCESS)
		{
			IGMP_CTRL("Ingore Learning By White List check  DROP");
			return RTK_IGMP_NF_DROP;
		}
	}
	
	if (numOfSrc==0)
		IGMP_CTRL("UPDATE ALL Source timer for Leave TO_IN{0}");

	hashIndex=rtk_igmp_hashAlgorithm(ipVersion, groupAddress);

	groupEntry=rtk_igmp_searchGroupEntry(pPkthdr->moduleIndex, ipVersion, groupAddress, 0,0);
	if (groupEntry==NULL)	/*means new group address, create new group entry*/
	{
		newGroupEntry=rtk_igmp_allocateGroupEntry();
		if (newGroupEntry==NULL)
		{
			IGMP_CTRL("run out of group entry!\n");
			return RTK_IGMP_NF_DROP;
		}

		newGroupEntry->vlanId=0;
		newGroupEntry->svlanId=0;
		
		newGroupEntry->ipVersion=ipVersion;
		if (ipVersion==IP_VERSION4)
		{
			newGroupEntry->groupAddr[0]=groupAddress[0];
			rtk_igmp_linkGroupEntry(newGroupEntry, rtk_igmp_mCastModuleArray[pPkthdr->moduleIndex].rtk_igmp_ipv4HashTable);
		}
		else
		{
			newGroupEntry->groupAddr[0]=groupAddress[0];
			newGroupEntry->groupAddr[1]=groupAddress[1];
			newGroupEntry->groupAddr[2]=groupAddress[2];
			newGroupEntry->groupAddr[3]=groupAddress[3];
			rtk_igmp_linkGroupEntry(newGroupEntry, rtk_igmp_mCastModuleArray[pPkthdr->moduleIndex].rtk_igmp_ipv6HashTable);
		}

		groupEntry=newGroupEntry;
	}

	/*from here groupEntry is the same as newGroupEntry*/
	clientEntry=rtk_igmp_searchClientEntry(ipVersion, groupEntry, pPkthdr->devIfindex, clientAddr,pPkthdr);
	if (clientEntry==NULL)
	{
		newClientEntry=rtk_igmp_allocateClientEntry(clientAddr,pPkthdr->smac,pPkthdr->devIfindex,pPkthdr);
		if (newClientEntry==NULL)
		{
			IGMP_CTRL("run out of client entry!\n");
			return RTK_IGMP_NF_DROP;
		}

#if defined(CONFIG_RTL_PER_PORT_CLIENT_NUM)
                enoughClientSpace=rtk_igmp_checkClientEmptySpace(pPkthdr->moduleIndex, ipVersion, pPkthdr->devIfindex, groupAddress, clientAddr);
                if(enoughClientSpace==FALSE)
                {
                        newClientEntry->flag |= FLAG_WAIT_STATUS;
                }
                newClientEntry->ipVersion = ipVersion;
                newClientEntry->moduleIndex = pPkthdr->moduleIndex;
#endif
		rtk_igmp_linkClientEntry(groupEntry, newClientEntry);
		clientEntry=newClientEntry;
	}

	if(pPkthdr->ingressCtagif )
	{
		clientEntry->reportCtagif = 1;
		clientEntry->reportVlanId = pPkthdr->ingressCvid;
	}
	if(pPkthdr->ingressStagif )
	{
		clientEntry->reportStagif = 1;
		clientEntry->reportSVlanId = pPkthdr->ingressSvid;
	}


	/*here to handle the source list*/
	if (ipVersion==IP_VERSION4)
		clientEntry->igmpVersion=IGMP_V3;
	else //if (ipVersion==IP_VERSION6)
		clientEntry->igmpVersion=MLD_V2;


	if (_fastLeave)
	{
		IGMP_CTRL("enableFastLeave==TRUE");
		clientEntry->groupFilterTimer=rtk_igmp_sysUpSeconds;
		rtk_igmp_deleteSourceList(clientEntry);
		/*link the new source list*/
		for (j=0; j<numOfSrc; j++)
		{

			if(ipVersion==IP_VERSION4)
				_HsourceAddr[0] = ntohl(*p_NsourceAddr);
			else
				memcpy(_HsourceAddr,p_NsourceAddr,sizeof(_HsourceAddr));
			
			newSourceEntry=rtk_igmp_allocateSourceEntry();
			if (newSourceEntry==NULL)
			{
				IGMP_CTRL("run out of source entry!\n");
				return RTK_IGMP_NF_DROP;
			}

			if (ipVersion==IP_VERSION4)
			{
				newSourceEntry->sourceAddr[0]=ntohl(p_NsourceAddr[0]);
				p_NsourceAddr++;
			}
			else
			{
				newSourceEntry->sourceAddr[0]=p_NsourceAddr[0];
				newSourceEntry->sourceAddr[1]=p_NsourceAddr[1];
				newSourceEntry->sourceAddr[2]=p_NsourceAddr[2];
				newSourceEntry->sourceAddr[3]=p_NsourceAddr[3];
				p_NsourceAddr=p_NsourceAddr+4;
			}
#if defined(CONFIG_RTL_NEW_IGMP_REPORT_BEHAVIOR)
                        if (ipVersion==IP_VERSION4)
                                newSourceEntry->portTimer =rtk_igmp_sysUpSeconds+mCastSnoopingGlobalConfig.groupMemberAgingTime[0];
                        else
                                newSourceEntry->portTimer =rtk_igmp_sysUpSeconds+mCastSnoopingGlobalConfig.groupMemberAgingTime[1];
#else
			newSourceEntry->portTimer=rtk_igmp_sysUpSeconds+clientAgingTime;
#endif
			rtk_igmp_linkSourceEntry(clientEntry,newSourceEntry);
		}

	}
	else
	{

		// query in-mo for Q(A-B)
		// query ex-mo for Q(G,X-A) & Q(G)
		//struct rtk_igmp_sourceEntry *sourceEntry=clientEntry->sourceList;
		sourceEntry=clientEntry->sourceList;
		while (sourceEntry!=NULL)
		{
			if (sourceEntry->portTimer>rtk_igmp_sysUpSeconds) //ex-mo X or in-mo A
			{
				sourceEntry->setOpFlag=1;
				//numOfQuerySrc++;
			}
			else
				sourceEntry->setOpFlag=0;

#if defined(CONFIG_RTL_NEW_IGMP_REPORT_BEHAVIOR)
                if (ipVersion==IP_VERSION4)
                        lastAgingTime = mCastSnoopingGlobalConfig.groupMemberAgingTime[0];
                else
                        lastAgingTime = mCastSnoopingGlobalConfig.groupMemberAgingTime[1];
#endif

			//sync all source timer to rtk_igmp_sysUpSeconds+rtk_igmp_mCastTimerParas.lastMemberAgingTimel
			if(sourceEntry->portTimer >rtk_igmp_sysUpSeconds+lastAgingTime)
			{
				sourceEntry->portTimer = rtk_igmp_sysUpSeconds+lastAgingTime;
			}
			
			sourceEntry=sourceEntry->next;
		}

		//IGMP_CTRL("numOfQuerySrc=%d, numOfSrc=%d", numOfQuerySrc, numOfSrc);


		/*add new source list*/
		for (j=0; j<numOfSrc; j++)
		{
			if(ipVersion==IP_VERSION4)
				_HsourceAddr[0] = ntohl(*p_NsourceAddr);
			else
				memcpy(_HsourceAddr,p_NsourceAddr,sizeof(_HsourceAddr));

			sourceEntry=rtk_igmp_searchSourceEntry(ipVersion, _HsourceAddr, clientEntry);
			if (sourceEntry!=NULL)
			{
#if defined(CONFIG_RTL_NEW_IGMP_REPORT_BEHAVIOR)
                if (ipVersion==IP_VERSION4)
                        sourceEntry->portTimer=rtk_igmp_sysUpSeconds+mCastSnoopingGlobalConfig.groupMemberAgingTime[0];
                else
                        sourceEntry->portTimer=rtk_igmp_sysUpSeconds+mCastSnoopingGlobalConfig.groupMemberAgingTime[1];
#else
				sourceEntry->portTimer=rtk_igmp_sysUpSeconds+clientAgingTime;
#endif 
				if (sourceEntry->setOpFlag)
				{
					//numOfQuerySrc--;
					sourceEntry->setOpFlag=0;
				}
			}
			else
			{
				newSourceEntry=rtk_igmp_allocateSourceEntry();
				if (newSourceEntry==NULL)
				{
					IGMP_CTRL("run out of source entry!\n");
					return RTK_IGMP_NF_DROP;
				}

				if (ipVersion==IP_VERSION4)
				{
					newSourceEntry->sourceAddr[0]=ntohl(p_NsourceAddr[0]);
				}
				else
				{
					newSourceEntry->sourceAddr[0]=p_NsourceAddr[0];
					newSourceEntry->sourceAddr[1]=p_NsourceAddr[1];
					newSourceEntry->sourceAddr[2]=p_NsourceAddr[2];
					newSourceEntry->sourceAddr[3]=p_NsourceAddr[3];
				}

#if defined(CONFIG_RTL_NEW_IGMP_REPORT_BEHAVIOR)
                                if (ipVersion==IP_VERSION4)
                                        newSourceEntry->portTimer=rtk_igmp_sysUpSeconds+mCastSnoopingGlobalConfig.groupMemberAgingTime[0];
                                else
                                        newSourceEntry->portTimer=rtk_igmp_sysUpSeconds+mCastSnoopingGlobalConfig.groupMemberAgingTime[1];
#else
				newSourceEntry->portTimer=rtk_igmp_sysUpSeconds+clientAgingTime;
#endif

				newSourceEntry->setOpFlag=0;
				rtk_igmp_linkSourceEntry(clientEntry,newSourceEntry);
			}

			if (ipVersion==IP_VERSION4)
			{
				p_NsourceAddr++;
			}
			else
			{
				p_NsourceAddr=p_NsourceAddr+4;
			}
		}


#if defined(CONFIG_RTL_NEW_IGMP_REPORT_BEHAVIOR)
                if (ipVersion==IP_VERSION4)
                        lastAgingTime = mCastSnoopingGlobalConfig.lastMemberAgingTime[0];
                else
                        lastAgingTime = mCastSnoopingGlobalConfig.lastMemberAgingTime[1];
#endif

		if (clientEntry->groupFilterTimer>rtk_igmp_sysUpSeconds+lastAgingTime) //ex-mo
		{
			clientEntry->groupFilterTimer=rtk_igmp_sysUpSeconds+lastAgingTime;
			IGMP_CTRL("Shrink ex-mo client to lastMemberAgingTime=%d groupFilterTimer=%d rtk_igmp_sysUpSeconds=%d", lastAgingTime,clientEntry->groupFilterTimer,rtk_igmp_sysUpSeconds);
		}
#if 0
		// query in-mo for Q(G,A-B)
		// query ex-mo for Q(G,X-A) & Q(G)
		IGMP_CTRL("Group-Source Specific Query");
		rtk_igmp_igmpQueryTimerExpired(0, IGMP_V3, (char *)groupAddress, numOfQuerySrc, clientEntry->sourceList, NULL);
		if (clientEntry->groupFilterTimer>rtk_igmp_sysUpSeconds && numOfQuerySrc!=0) //ex-mo
		{
			IGMP_CTRL("Group Specific Query");
			rtk_igmp_igmpQueryTimerExpired(0, IGMP_V3, (char *)groupAddress, 0, NULL, NULL);
		}
#endif

	}

		rtk_igmp_flow_reflush_update(FAIL,groupAddress);

#if defined(CONFIG_RTL_PER_PORT_CLIENT_NUM)
        if(clientEntry->flag&FLAG_WAIT_STATUS)
        {
                if(enoughClientSpace==FALSE)
                {
                        rtk_igmp_enterWaitStatus(pPkthdr->moduleIndex, ipVersion);
                        return RTK_IGMP_NF_ACCEPT;
                }
        }
#endif

	return RTK_IGMP_NF_CONTINUE;
}

static  int32 rtk_igmp_processToExclude(rtk_igmp_pktHdr_t *pPkthdr, uint32 ipVersion, uint32 *clientAddr, uint8 *pktBuf)
{
	rtk_igmp_nf_ret_e_t ret;
	int j=0;
	uint32 groupAddress[4]={0, 0, 0, 0};
	struct rtk_igmp_groupEntry* groupEntry=NULL;
	struct rtk_igmp_groupEntry* newGroupEntry=NULL;
	struct rtk_igmp_clientEntry* clientEntry=NULL;
	struct rtk_igmp_clientEntry* newClientEntry=NULL;
	struct rtk_igmp_sourceEntry *sourceEntry=NULL;
	struct rtk_igmp_sourceEntry *newSourceEntry=NULL;

	uint32 hashIndex=0;
	uint16 numOfSrc=0;
	uint32 *p_NsourceAddr=NULL;				//Net endian
	uint32 _HsourceAddr[4]={0, 0, 0, 0};	//Host endian
	//uint32 tmpAddress[4]={0, 0, 0, 0};
	//uint16 numOfQuerySrc=0;
	uint32 clientAgingTime=0;

#if defined(CONFIG_RTL_PER_PORT_CLIENT_NUM)
    int enoughClientSpace=TRUE;
#endif
	if (ipVersion==IP_VERSION4)
	{
		IGMP_CTRL("process IGMPv3-ToExclude");
		groupAddress[0]=ntohl(((struct groupRecord *)pktBuf)->groupAddr);
		numOfSrc=ntohs(((struct groupRecord *)pktBuf)->numOfSrc);
		p_NsourceAddr=&(((struct groupRecord *)pktBuf)->srcList);
		clientAgingTime = pPkthdr->devInfo->devConf.igmp.igmpMcMemberAgingInterval;
	}
	else
	{
		IGMP_CTRL("process MLDv2-ToExclude");
		memcpy(groupAddress,((struct mCastAddrRecord *)pktBuf)->mCastAddr,IPV6ADDRLEN);
		numOfSrc=ntohs(((struct mCastAddrRecord *)pktBuf)->numOfSrc);
		p_NsourceAddr=&(((struct mCastAddrRecord *)pktBuf)->srcList);
		clientAgingTime = pPkthdr->devInfo->devConf.igmp6.igmp6McMemberAgingInterval;
	}

	{
		//check group to cpu
		rtk_igmp_userResvGroup_t *grp2cpu=NULL;
		grp2cpu = _rtk_igmp_checkGroupToPsTbl(ipVersion, groupAddress);
		if(grp2cpu && grp2cpu->patten.isCopy2cpu==0)
		{
			IGMP_CTRL("IGNORE RESERVED ADDRESS");
			return RTK_IGMP_NF_ACCEPT;
		}
	}

#if defined(CONFIG_RTL_LAN_COMPATIBILITY)
    if(ipVersion==IP_VERSION4)
    {
            if(mCastSnoopingGlobalConfig.igmpCompatibilityEnable==0)
            {
                    IGMP_CTRL("igmpCompatibilityEnable is 0,not learning igmp ToExclude packet [%x]",groupAddress[0]);
                    return RTK_IGMP_NF_ACCEPT;
            }

            if(igmpcompatibilityStatus!=COMPATIBILITY_IGMPV3)
            {
                    numOfSrc = 0;
                    IGMP_CTRL("igmpcompatibilityStatus is %d,set ToExclude packet numOfSrc is 0 [%x]",igmpcompatibilityStatus,groupAddress[0]);
            }
    }
#endif


	ret=rtk_igmp_group_limitCheck(pPkthdr,ipVersion,groupAddress,clientAddr,NULL,NULL,0,0,pPkthdr->devIfindex,RTK_IGMP_NF_DROP);
	if(ret!=RTK_IGMP_NF_CONTINUE)
	{
		IGMP_CTRL("Limit check FAIL Ingore Snooping learning ret=%d",ret);
		return ret;
	}

	if(rtk_igmp_filterWhiteListCheck(pPkthdr->devInfo,ipVersion,1,groupAddress,clientAddr,NULL,pPkthdr->smac)!= SUCCESS)
	{
		IGMP_CTRL("Ingore Learning By White List check  DROP");
		return RTK_IGMP_NF_DROP;
	}



	hashIndex=rtk_igmp_hashAlgorithm(ipVersion, groupAddress);

	groupEntry=rtk_igmp_searchGroupEntry(pPkthdr->moduleIndex, ipVersion, groupAddress, 0,0);
	if (groupEntry==NULL)   /*means new group address, create new group entry*/
	{
		newGroupEntry=rtk_igmp_allocateGroupEntry();
		if(newGroupEntry==NULL)
		{
			IGMP_CTRL("run out of group entry!\n");
			return RTK_IGMP_NF_DROP;
		}

		newGroupEntry->vlanId=0;
		newGroupEntry->svlanId=0;
		newGroupEntry->ipVersion=ipVersion;
		if (ipVersion==IP_VERSION4)
		{
			newGroupEntry->groupAddr[0]=groupAddress[0];
			rtk_igmp_linkGroupEntry(newGroupEntry, rtk_igmp_mCastModuleArray[pPkthdr->moduleIndex].rtk_igmp_ipv4HashTable);
		}
		else
		{
			newGroupEntry->groupAddr[0]=groupAddress[0];
			newGroupEntry->groupAddr[1]=groupAddress[1];
			newGroupEntry->groupAddr[2]=groupAddress[2];
			newGroupEntry->groupAddr[3]=groupAddress[3];
			rtk_igmp_linkGroupEntry(newGroupEntry, rtk_igmp_mCastModuleArray[pPkthdr->moduleIndex].rtk_igmp_ipv6HashTable);
		}

		groupEntry=newGroupEntry;
	}

	clientEntry=rtk_igmp_searchClientEntry(ipVersion, groupEntry, pPkthdr->devIfindex, clientAddr,pPkthdr);
	if (clientEntry==NULL)
	{
		newClientEntry=rtk_igmp_allocateClientEntry(clientAddr,pPkthdr->smac,pPkthdr->devIfindex,pPkthdr);
		if (newClientEntry==NULL)
		{
			IGMP_CTRL("run out of client entry!\n");
			return RTK_IGMP_NF_DROP;
		}
#if defined(CONFIG_RTL_PER_PORT_CLIENT_NUM)
                enoughClientSpace=rtk_igmp_checkClientEmptySpace(pPkthdr->moduleIndex, ipVersion, pPkthdr->devIfindex, groupAddress, clientAddr);
                if(enoughClientSpace==FALSE)
                {
                        newClientEntry->flag |= FLAG_WAIT_STATUS;
                }
                newClientEntry->ipVersion = ipVersion;
                newClientEntry->moduleIndex = pPkthdr->moduleIndex;
#endif

		rtk_igmp_linkClientEntry(groupEntry, newClientEntry);
		clientEntry=newClientEntry;
	}

	if(pPkthdr->ingressCtagif )
	{
		clientEntry->reportCtagif = 1;
		clientEntry->reportVlanId = pPkthdr->ingressCvid;
	}
	if(pPkthdr->ingressStagif )
	{
		clientEntry->reportStagif = 1;
		clientEntry->reportSVlanId = pPkthdr->ingressSvid;
	}


	/*flush the old source list*/

	// delete ((X-A) | (Y-A)) or (A-B)
	sourceEntry=clientEntry->sourceList;
	while (sourceEntry!=NULL)
	{
		sourceEntry->setOpFlag=1;
		sourceEntry=sourceEntry->next;
	}

	if (ipVersion==IP_VERSION4)
		clientEntry->igmpVersion=IGMP_V3;
	else //if(ipVersion==IP_VERSION6)
		clientEntry->igmpVersion=MLD_V2;

	/*link the new source list*/
	for (j=0; j<numOfSrc; j++)
	{
		if(ipVersion==IP_VERSION4)
			_HsourceAddr[0] = ntohl(*p_NsourceAddr);
		else
			memcpy(_HsourceAddr,p_NsourceAddr,sizeof(_HsourceAddr));

		sourceEntry=rtk_igmp_searchSourceEntry(ipVersion, _HsourceAddr, clientEntry);
		if (clientEntry->groupFilterTimer>rtk_igmp_sysUpSeconds) //ex-mo
		{
			if (sourceEntry!=NULL) // A*X + A*Y
			{
				sourceEntry->setOpFlag=0;
				if (sourceEntry->portTimer>rtk_igmp_sysUpSeconds) //A*X
				{
				}
				else
				{
					if (ipVersion==IP_VERSION4)
						p_NsourceAddr++;
					else
						p_NsourceAddr=p_NsourceAddr+4;

					continue;
				}
			}
			else	// A-X-Y
			{
				newSourceEntry=rtk_igmp_allocateSourceEntry();
				if (newSourceEntry==NULL)
				{
					IGMP_CTRL("run out of source entry!\n");
					return RTK_IGMP_NF_DROP;
				}

				if (ipVersion==IP_VERSION4)
				{
					newSourceEntry->sourceAddr[0]=ntohl(p_NsourceAddr[0]);
				}
				else //if (ipVersion==IP_VERSION4)
				{
					newSourceEntry->sourceAddr[0]=p_NsourceAddr[0];
					newSourceEntry->sourceAddr[1]=p_NsourceAddr[1];
					newSourceEntry->sourceAddr[2]=p_NsourceAddr[2];
					newSourceEntry->sourceAddr[3]=p_NsourceAddr[3];
				}

				/*time out the sources included in the MODE_IS_EXCLUDE report*/
				newSourceEntry->portTimer=rtk_igmp_sysUpSeconds;
				rtk_igmp_linkSourceEntry(clientEntry,newSourceEntry);

				newSourceEntry->portTimer=clientEntry->groupFilterTimer;
				newSourceEntry->setOpFlag=0;
			}

#if 0
			if (ipVersion==IP_VERSION4)
			{
				//querySourceAddr
				IGMP_CTRL("Block.GIP.Clt.SrcIP(%pI4n) in A-Y",(p_NsourceAddr));////

				*tmpAddress = *p_NsourceAddr;
				*p_NsourceAddr = *(p_NsourceAddr + (numOfSrc-j-1));
				*(p_NsourceAddr + (numOfSrc-j-1)) = *tmpAddress;
				IGMP_CTRL("sourceAddr Head(%pI4n) Tail(%pI4n)",(p_NsourceAddr), ((p_NsourceAddr+(numOfSrc-j-1))));
			}
			else
			{
				int i ;
				IGMP_CTRL("Block.GIP6.Clt.SrcIP6(%pI6) in A-Y",(p_NsourceAddr));////

				for (i=0; i<4; i++)
				{
					*(tmpAddress+i) = *(p_NsourceAddr+i);
					*(p_NsourceAddr+i) = *(p_NsourceAddr+i + ((numOfSrc -j - 1)));
					*(p_NsourceAddr+i + ((numOfSrc - j - 1))) = *(tmpAddress+i);
				}
			}


			j--;
			numOfSrc--;
			//numOfQuerySrc++;
#endif			
			continue;	//no need to move sourceAddr

		}
		else //in-mo
		{
			if (sourceEntry!=NULL) // A*B
			{
				sourceEntry->setOpFlag=0;

#if 0
				if (ipVersion==IP_VERSION4)
				{
					//querySourceAddr
					IGMP_CTRL("To_Ex.GIP.Clt.SrcIP(%pI4n) in A*B",(p_NsourceAddr));////

					*tmpAddress = *p_NsourceAddr;
					*p_NsourceAddr = *(p_NsourceAddr + (numOfSrc-j-1));
					*(p_NsourceAddr + (numOfSrc-j-1)) = *tmpAddress;
					IGMP_CTRL("sourceAddr Head(%pI4n) Tail(%pI4n)",(p_NsourceAddr), ((p_NsourceAddr+(numOfSrc-j-1))));
				}
				else
				{
					int i ;
					IGMP_CTRL("To_Ex.GIP6.Clt.SrcIP6(%pI6) in A*B",(p_NsourceAddr));////

					for (i=0; i<4; i++)
					{
						*(tmpAddress+i) = *(p_NsourceAddr+i);
						*(p_NsourceAddr+i) = *(p_NsourceAddr+i + ((numOfSrc -j - 1)));
						*(p_NsourceAddr+i + ((numOfSrc - j - 1))) = *(tmpAddress+i);
					}
				}

				j--;
				numOfSrc--;
				//numOfQuerySrc++;
#endif				
				continue;	//no need to move sourceAddr

			}
			else //B-A
			{
				newSourceEntry=rtk_igmp_allocateSourceEntry();
				if (newSourceEntry==NULL)
				{
					IGMP_CTRL("run out of source entry!\n");
					return RTK_IGMP_NF_DROP;
				}

				if (ipVersion==IP_VERSION4)
				{
					newSourceEntry->sourceAddr[0]=ntohl(p_NsourceAddr[0]);
					p_NsourceAddr++;
				}
				else //if (ipVersion==IP_VERSION4)
				{
					newSourceEntry->sourceAddr[0]=p_NsourceAddr[0];
					newSourceEntry->sourceAddr[1]=p_NsourceAddr[1];
					newSourceEntry->sourceAddr[2]=p_NsourceAddr[2];
					newSourceEntry->sourceAddr[3]=p_NsourceAddr[3];
					p_NsourceAddr=p_NsourceAddr+4;
				}
				/*time out the sources included in the MODE_IS_EXCLUDE report*/
				newSourceEntry->portTimer=rtk_igmp_sysUpSeconds;
				rtk_igmp_linkSourceEntry(clientEntry,newSourceEntry);
				newSourceEntry->setOpFlag=0;
			}

		}
	}

	// delete ((X-A) | (Y-A)) or (A-B)
	{
		struct rtk_igmp_sourceEntry *nextSourceEntry;
		sourceEntry=clientEntry->sourceList;
		while (sourceEntry!=NULL)
		{
			nextSourceEntry=sourceEntry->next;
			if (sourceEntry->setOpFlag)
			{
				rtk_igmp_deleteSourceEntry(clientEntry, sourceEntry);
			}
			sourceEntry=nextSourceEntry;
		}
	}

#if defined(CONFIG_RTL_NEW_IGMP_REPORT_BEHAVIOR)
        if (ipVersion==IP_VERSION4)
                clientEntry->groupFilterTimer=rtk_igmp_sysUpSeconds+mCastSnoopingGlobalConfig.groupMemberAgingTime[0];
        else
                clientEntry->groupFilterTimer=rtk_igmp_sysUpSeconds+mCastSnoopingGlobalConfig.groupMemberAgingTime[1];
#else
	clientEntry->groupFilterTimer=rtk_igmp_sysUpSeconds+clientAgingTime;
#endif

#if 0
	if (ipVersion==IP_VERSION4)
	{
		int i ;
		for (i=0; i<numOfQuerySrc; i++)
		{
			IGMP_CTRL("Query GIP(%pI4h)-SrcIP(%pI4n)", (groupAddress), ((p_NsourceAddr+i)));
		}
		//IGMPQueryVersion=3;
		//rtk_igmp_igmpQueryTimerExpired(0, IGMP_V3, (char *)groupAddress, numOfQuerySrc, NULL, (char *)p_NsourceAddr);
	}
	else
	{
		int i ;
		for (i=0; i<numOfQuerySrc; i+=4)
		{
			IGMP_CTRL("Query GIP6(%pI6)-SrcIP(%pI6)", (groupAddress), ((p_NsourceAddr+i)));
		}
		//IGMPQueryVersion=3; ???????????????????????????????????????????????
		//rtk_igmp_mldQueryTimerExpired(0);
	}
#endif


#if defined(CONFIG_RTL_PER_PORT_CLIENT_NUM)
        if(clientEntry->flag&FLAG_WAIT_STATUS)
        {
                if(enoughClientSpace==FALSE)
                {
                        rtk_igmp_enterWaitStatus(pPkthdr->moduleIndex, ipVersion);
                        return RTK_IGMP_NF_ACCEPT;
                }
        }
#endif

	rtk_igmp_flow_reflush_update(FAIL,groupAddress);


	return RTK_IGMP_NF_CONTINUE;
}

static  int32 rtk_igmp_processAllow(rtk_igmp_pktHdr_t *pPkthdr, uint32 ipVersion, uint32 *clientAddr, uint8 *pktBuf)
{
	rtk_igmp_nf_ret_e_t ret;
	int32 j=0;
	uint32 groupAddress[4]={0, 0, 0, 0};
	struct rtk_igmp_groupEntry* groupEntry=NULL;
	struct rtk_igmp_groupEntry* newGroupEntry=NULL;
	struct rtk_igmp_clientEntry* clientEntry=NULL;
	struct rtk_igmp_clientEntry* newClientEntry=NULL;
	struct rtk_igmp_sourceEntry *sourceEntry=NULL;
	struct rtk_igmp_sourceEntry *newSourceEntry=NULL;

	uint32 hashIndex=0;
	uint16 numOfSrc=0;
	uint32 *p_NsourceAddr=NULL;				//Net endian
	uint32 _HsourceAddr[4]={0, 0, 0, 0};	//Host endian
	uint32 clientAgingTime=0;
#if defined(CONFIG_RTL_PER_PORT_CLIENT_NUM)
	int enoughClientSpace=TRUE;
#endif
	if (ipVersion==IP_VERSION4)
	{
		IGMP_CTRL("process IGMPv3 Allow packet");
		groupAddress[0]=ntohl(((struct groupRecord *)pktBuf)->groupAddr);
		numOfSrc=ntohs(((struct groupRecord *)pktBuf)->numOfSrc);
		p_NsourceAddr=&(((struct groupRecord *)pktBuf)->srcList);
		clientAgingTime = pPkthdr->devInfo->devConf.igmp.igmpMcMemberAgingInterval;

	}
	else
	{
		IGMP_CTRL("process MLDv2 Allow packet");
		memcpy(groupAddress,((struct mCastAddrRecord *)pktBuf)->mCastAddr,IPV6ADDRLEN);
		numOfSrc=ntohs(((struct mCastAddrRecord *)pktBuf)->numOfSrc);
		p_NsourceAddr=&(((struct mCastAddrRecord *)pktBuf)->srcList);
		clientAgingTime = pPkthdr->devInfo->devConf.igmp6.igmp6McMemberAgingInterval;
	}
	{
		//check group to cpu
		rtk_igmp_userResvGroup_t *grp2cpu=NULL;
		grp2cpu = _rtk_igmp_checkGroupToPsTbl(ipVersion, groupAddress);
		if(grp2cpu && grp2cpu->patten.isCopy2cpu==0)
		{
			IGMP_CTRL("IGNORE RESERVED ADDRESS");
			return RTK_IGMP_NF_ACCEPT;
		}
	}



	ret = rtk_igmp_group_limitCheck(pPkthdr,ipVersion,groupAddress,clientAddr,NULL,NULL,0,0,pPkthdr->devIfindex,RTK_IGMP_NF_DROP);
	if(ret!=RTK_IGMP_NF_CONTINUE)
	{
		IGMP_CTRL("Limit check FAIL Ingore Snooping learning ret=%d",ret);
		return ret;
	}
	if(numOfSrc==1)
	{
		if(ipVersion==IP_VERSION4)
			_HsourceAddr[0] = ntohl(*p_NsourceAddr);
		else
			memcpy(_HsourceAddr,p_NsourceAddr,sizeof(_HsourceAddr));
		
		if(rtk_igmp_filterWhiteListCheck(pPkthdr->devInfo,ipVersion,1,groupAddress,clientAddr,_HsourceAddr,pPkthdr->smac)!= SUCCESS)
		{
			IGMP_CTRL("Ingore Learning By White List check DROP");
			return RTK_IGMP_NF_DROP;
		}
	}
	else
	{
		if(rtk_igmp_filterWhiteListCheck(pPkthdr->devInfo,ipVersion,1,groupAddress,clientAddr,NULL,pPkthdr->smac)!= SUCCESS)
		{
			IGMP_CTRL("Ingore Learning By White List check  DROP");
			return RTK_IGMP_NF_DROP;
		}
	}

	hashIndex=rtk_igmp_hashAlgorithm( ipVersion, groupAddress);
	groupEntry=rtk_igmp_searchGroupEntry(pPkthdr->moduleIndex, ipVersion, groupAddress, 0,0);
	if (groupEntry==NULL)   /*means new group address, create new group entry*/
	{
		newGroupEntry=rtk_igmp_allocateGroupEntry();
		if (newGroupEntry==NULL)
		{
			IGMP_CTRL("run out of group entry!\n");
			return RTK_IGMP_NF_DROP;
		}

		newGroupEntry->vlanId=0;
		newGroupEntry->svlanId=0;		
		newGroupEntry->ipVersion=ipVersion;
		if (ipVersion==IP_VERSION4)
		{
			newGroupEntry->groupAddr[0]=groupAddress[0];
			rtk_igmp_linkGroupEntry(newGroupEntry, rtk_igmp_mCastModuleArray[pPkthdr->moduleIndex].rtk_igmp_ipv4HashTable);
		}
		else
		{
			newGroupEntry->groupAddr[0]=groupAddress[0];
			newGroupEntry->groupAddr[1]=groupAddress[1];
			newGroupEntry->groupAddr[2]=groupAddress[2];
			newGroupEntry->groupAddr[3]=groupAddress[3];
			rtk_igmp_linkGroupEntry(newGroupEntry, rtk_igmp_mCastModuleArray[pPkthdr->moduleIndex].rtk_igmp_ipv6HashTable);
		}

		groupEntry=newGroupEntry;
	}

	clientEntry=rtk_igmp_searchClientEntry(ipVersion, groupEntry, pPkthdr->devIfindex, clientAddr,pPkthdr);
	if (clientEntry==NULL)
	{
		newClientEntry=rtk_igmp_allocateClientEntry(clientAddr,pPkthdr->smac,pPkthdr->devIfindex,pPkthdr);
		if(newClientEntry==NULL)
		{
			IGMP_CTRL("run out of client entry!\n");
			return RTK_IGMP_NF_DROP;
		}
#if defined(CONFIG_RTL_PER_PORT_CLIENT_NUM)
                enoughClientSpace=rtk_igmp_checkClientEmptySpace(pPkthdr->moduleIndex, ipVersion, pPkthdr->devIfindex, groupAddress, clientAddr);
                if(enoughClientSpace==FALSE)
                {
                        newClientEntry->flag |= FLAG_WAIT_STATUS;
                }
                newClientEntry->ipVersion = ipVersion;
                newClientEntry->moduleIndex = pPkthdr->moduleIndex;
#endif

		rtk_igmp_linkClientEntry(groupEntry, newClientEntry);
		clientEntry=newClientEntry;
		clientEntry->groupFilterTimer=rtk_igmp_sysUpSeconds;
	}

	if(pPkthdr->ingressCtagif )
	{
		clientEntry->reportCtagif = 1;
		clientEntry->reportVlanId = pPkthdr->ingressCvid;
	}
	if(pPkthdr->ingressStagif )
	{
		clientEntry->reportStagif = 1;
		clientEntry->reportSVlanId = pPkthdr->ingressSvid;
	}


	if (ipVersion==IP_VERSION4)
		clientEntry->igmpVersion=IGMP_V3;
	else	//if (ipVersion==IP_VERSION6)
		clientEntry->igmpVersion=MLD_V2;


	/*here to handle the source list*/
	for (j=0; j<numOfSrc; j++)
	{
		if(ipVersion==IP_VERSION4)
			_HsourceAddr[0] = ntohl(*p_NsourceAddr);
		else
			memcpy(_HsourceAddr,p_NsourceAddr,sizeof(_HsourceAddr));

		sourceEntry=rtk_igmp_searchSourceEntry(ipVersion, _HsourceAddr, clientEntry);
		if (sourceEntry==NULL)
		{
			newSourceEntry=rtk_igmp_allocateSourceEntry();
			if (newSourceEntry==NULL)
			{
				IGMP_CTRL("run out of source entry!\n");
				return RTK_IGMP_NF_DROP;
			}

			if (ipVersion==IP_VERSION4)
			{
				newSourceEntry->sourceAddr[0]=ntohl(p_NsourceAddr[0]);
			}
			else
			{
				newSourceEntry->sourceAddr[0]=p_NsourceAddr[0];
				newSourceEntry->sourceAddr[1]=p_NsourceAddr[1];
				newSourceEntry->sourceAddr[2]=p_NsourceAddr[2];
				newSourceEntry->sourceAddr[3]=p_NsourceAddr[3];
			}
#if defined(CONFIG_RTL_NEW_IGMP_REPORT_BEHAVIOR)
                        if (ipVersion==IP_VERSION4)
                                newSourceEntry->portTimer =rtk_igmp_sysUpSeconds+mCastSnoopingGlobalConfig.groupMemberAgingTime[0];
                        else
                                newSourceEntry->portTimer =rtk_igmp_sysUpSeconds+mCastSnoopingGlobalConfig.groupMemberAgingTime[1];
#else
			newSourceEntry->portTimer=rtk_igmp_sysUpSeconds+clientAgingTime;
#endif
			rtk_igmp_linkSourceEntry(clientEntry,newSourceEntry);
		}
		else
		{
			/*just update source timer*/
#if defined(CONFIG_RTL_NEW_IGMP_REPORT_BEHAVIOR)
                        if (ipVersion==IP_VERSION4)
                                sourceEntry->portTimer =rtk_igmp_sysUpSeconds+mCastSnoopingGlobalConfig.groupMemberAgingTime[0];
                        else
                                sourceEntry->portTimer =rtk_igmp_sysUpSeconds+mCastSnoopingGlobalConfig.groupMemberAgingTime[1];
#else
			sourceEntry->portTimer=rtk_igmp_sysUpSeconds+clientAgingTime;
#endif
		}

		if (ipVersion==IP_VERSION4)
			p_NsourceAddr++;
		else
			p_NsourceAddr=p_NsourceAddr+4;

	}
#if defined(CONFIG_RTL_PER_PORT_CLIENT_NUM)
        if(clientEntry->flag&FLAG_WAIT_STATUS)
        {
                if(enoughClientSpace==FALSE)
                {
                        rtk_igmp_enterWaitStatus(pPkthdr->moduleIndex, ipVersion);
                        return RTK_IGMP_NF_ACCEPT;
                }
        }
#endif

	rtk_igmp_flow_reflush_update(FAIL,groupAddress);

	return RTK_IGMP_NF_CONTINUE;
}

static int32 rtk_igmp_processBlock(rtk_igmp_pktHdr_t *pPkthdr, uint32 ipVersion, uint32 *clientAddr, uint8 *pktBuf)
{
	rtk_igmp_nf_ret_e_t ret;
	int32 j=0;
	uint32 groupAddress[4]={0, 0, 0, 0};

	struct rtk_igmp_groupEntry* groupEntry=NULL;
	struct rtk_igmp_clientEntry* clientEntry=NULL;
	//struct rtk_igmp_clientEntry* newClientEntry=NULL;
	struct rtk_igmp_sourceEntry *sourceEntry=NULL;
	struct rtk_igmp_sourceEntry *newSourceEntry=NULL;

	uint32 hashIndex=0;
	uint16 numOfSrc=0;
	uint32 *p_NsourceAddr=NULL;				//Net endian
	uint32 _HsourceAddr[4]={0, 0, 0, 0};	//Host endian
	uint32 _fastLeave=0;

	//uint16 numOfQuerySrc=0;
	//uint32 *querySourceAddr=NULL;
	//uint32 tmpAddress[4]={0, 0, 0, 0};

	if (ipVersion==IP_VERSION4)
	{
		_fastLeave = pPkthdr->devInfo->devConf.igmp.igmpfastLeave;
		IGMP_CTRL("process IGMPv3-Block");
		groupAddress[0]=ntohl(((struct groupRecord *)pktBuf)->groupAddr);
		numOfSrc=ntohs(((struct groupRecord *)pktBuf)->numOfSrc);
		p_NsourceAddr=&(((struct groupRecord *)pktBuf)->srcList);
	}
	else
	{
		IGMP_CTRL("process MLDv2-Block");
		_fastLeave = pPkthdr->devInfo->devConf.igmp6.igmp6fastLeave;
		memcpy(groupAddress,((struct mCastAddrRecord *)pktBuf)->mCastAddr,IPV6ADDRLEN);

		numOfSrc=ntohs(((struct mCastAddrRecord *)pktBuf)->numOfSrc);
		p_NsourceAddr=&(((struct mCastAddrRecord *)pktBuf)->srcList);
	}

	{
		//check group to cpu
		rtk_igmp_userResvGroup_t *grp2cpu=NULL;
		grp2cpu = _rtk_igmp_checkGroupToPsTbl(ipVersion, groupAddress);
		if(grp2cpu && grp2cpu->patten.isCopy2cpu==0)
		{
			IGMP_CTRL("IGNORE RESERVED ADDRESS");
			return RTK_IGMP_NF_ACCEPT;
		}
	}

#if defined(CONFIG_RTL_LAN_COMPATIBILITY)
    if(ipVersion==IP_VERSION4)
    {
            if(mCastSnoopingGlobalConfig.igmpCompatibilityEnable)
            {
                    if((igmpcompatibilityStatus==COMPATIBILITY_IGMPV1)||(igmpcompatibilityStatus==COMPATIBILITY_IGMPV2))
                    {
                            IGMP_CTRL("igmpcompatibilityStatus is %d,not learning igmp block [%x]",igmpcompatibilityStatus,groupAddress[0]);
                            return RTK_IGMP_NF_ACCEPT;
                    }
                    IGMP_CTRL("igmpcompatibilityStatus is %d, learning igmp block[%x]",igmpcompatibilityStatus,groupAddress[0]);
            }
    }
#endif

	ret = rtk_igmp_group_limitCheck(pPkthdr,ipVersion,groupAddress,clientAddr,NULL,NULL,0,0,pPkthdr->devIfindex,RTK_IGMP_NF_DROP);
	if(ret!=RTK_IGMP_NF_CONTINUE)
	{
		IGMP_CTRL("Limit check FAIL Ingore Snooping learning ret=%d",ret);
		return ret;
	}

	if(rtk_igmp_filterWhiteListCheck(pPkthdr->devInfo,ipVersion,1,groupAddress,clientAddr,NULL,pPkthdr->smac)!= SUCCESS)
	{
		IGMP_CTRL("Ingore Learning By White List check  DROP");
		return RTK_IGMP_NF_DROP;
	}



	hashIndex=rtk_igmp_hashAlgorithm(ipVersion, groupAddress);

	groupEntry=rtk_igmp_searchGroupEntry(pPkthdr->moduleIndex, ipVersion, groupAddress, 0,0);
	if (groupEntry==NULL)
	{
		goto out;
	}

	clientEntry=rtk_igmp_searchClientEntry(ipVersion, groupEntry, pPkthdr->devIfindex, clientAddr,pPkthdr);
	if (clientEntry==NULL)
	{
		goto out;
	}

	if (ipVersion==IP_VERSION4)
		clientEntry->igmpVersion=IGMP_V3;
	else //if (ipVersion==IP_VERSION6)
		clientEntry->igmpVersion=MLD_V2;


	if (clientEntry->groupFilterTimer>rtk_igmp_sysUpSeconds) /*means exclude mode*/
	{
		IGMP_CTRL("<Ex-Mo> GIP.Clt");
		//numOfQuerySrc=0;
		for (j=0; j<numOfSrc; j++)
		{
			//IGMP_CTRL("current j(%d), numOfSrc(%d), numOfQuerySrc(%d)", j, numOfSrc, numOfQuerySrc);////
			if(ipVersion==IP_VERSION4)
				_HsourceAddr[0] = ntohl(*p_NsourceAddr);
			else
				memcpy(_HsourceAddr,p_NsourceAddr,sizeof(_HsourceAddr));

			sourceEntry=rtk_igmp_searchSourceEntry(ipVersion, _HsourceAddr, clientEntry);
			if (sourceEntry==NULL) // A-X-Y
			{
				newSourceEntry=rtk_igmp_allocateSourceEntry();
				if (newSourceEntry==NULL)
				{
					IGMP_CTRL("run out of source entry!\n");
					return RTK_IGMP_NF_DROP;
				}

				if (ipVersion==IP_VERSION4)
				{
					newSourceEntry->sourceAddr[0]=ntohl(p_NsourceAddr[0]);
				}
				else
				{
					newSourceEntry->sourceAddr[0]=p_NsourceAddr[0];
					newSourceEntry->sourceAddr[1]=p_NsourceAddr[1];
					newSourceEntry->sourceAddr[2]=p_NsourceAddr[2];
					newSourceEntry->sourceAddr[3]=p_NsourceAddr[3];
				}

				newSourceEntry->portTimer=clientEntry->groupFilterTimer;

				rtk_igmp_linkSourceEntry(clientEntry,newSourceEntry);
			}
			else // A*X+A*Y
			{
				if (_fastLeave)
				{
					sourceEntry->portTimer=rtk_igmp_sysUpSeconds;
				}


				if (sourceEntry->portTimer>rtk_igmp_sysUpSeconds) // A*X
				{
				}
				else	//A*Y
				{
					if (ipVersion==IP_VERSION4)
					{
						p_NsourceAddr++;
					}
					else
					{
						p_NsourceAddr=p_NsourceAddr+4;
					}
					continue;
				}

			}

#if 0
			// A-X-Y + A*X = A-Y
			if (ipVersion==IP_VERSION4)
			{
				//querySourceAddr
				IGMP_CTRL("Block.GIP.Clt.SrcIP(%pI4n) in A-Y",(p_NsourceAddr));////

				*tmpAddress = *p_NsourceAddr;
				*p_NsourceAddr = *(p_NsourceAddr + (numOfSrc-j-1));
				*(p_NsourceAddr + (numOfSrc-j-1)) = *tmpAddress;
				IGMP_CTRL("sourceAddr Head(%pI4n) Tail(%pI4n)",(p_NsourceAddr), ((p_NsourceAddr+(numOfSrc-j-1))));
			}
			else
			{
				int i ;
				IGMP_CTRL("Block.GIP6.Clt.SrcIP6(%pI6) in A-Y",(p_NsourceAddr));////

				for (i=0; i<4; i++)
				{
					*(tmpAddress+i) = *(p_NsourceAddr+i);
					*(p_NsourceAddr+i) = *(p_NsourceAddr+i + ((numOfSrc -j - 1)));
					*(p_NsourceAddr+i + ((numOfSrc - j - 1))) = *(tmpAddress+i);
				}
			}

			j--;
			numOfSrc--;
			//numOfQuerySrc++;
#endif			
			continue;	//no need to move sourceAddr


		}
	}
	else           /*means include mode*/
	{
		IGMP_CTRL("<In-Mo> GIP.Clt");

		//numOfQuerySrc=0;
		for (j=0; j<numOfSrc; j++)
          	{
			//IGMP_CTRL("current j(%d), numOfSrc(%d), numOfQuerySrc(%d)", j, numOfSrc, numOfQuerySrc);////
			if(ipVersion==IP_VERSION4)
				_HsourceAddr[0] = ntohl(*p_NsourceAddr);
			else
				memcpy(_HsourceAddr,p_NsourceAddr,sizeof(_HsourceAddr));

			sourceEntry=rtk_igmp_searchSourceEntry(ipVersion, _HsourceAddr,clientEntry);
			if (sourceEntry!=NULL) //
			{
				if (_fastLeave)
				{
					sourceEntry->portTimer=rtk_igmp_sysUpSeconds;
				}

#if 0
				if (ipVersion==IP_VERSION4)
				{
					//querySourceAddr
					IGMP_CTRL("Block.GIP.Clt.SrcIP(%pI4n) in A*B",(p_NsourceAddr));////

					*tmpAddress = *p_NsourceAddr;

					*p_NsourceAddr = *(p_NsourceAddr + (numOfSrc-j-1));
					*(p_NsourceAddr + (numOfSrc-j-1)) = *tmpAddress;
					IGMP_CTRL("sourceAddr Head(%pI4n) Tail(%pI4n)",(p_NsourceAddr), ((p_NsourceAddr+(numOfSrc-j-1))));

				}
				else
				{
					int i ;
					IGMP_CTRL("Block.GIP6.Clt.SrcIP6(%pI6) in A*B",(p_NsourceAddr));////

					for (i=0; i<4; i++)
					{
						*(tmpAddress+i) = *(p_NsourceAddr+i);

						*(p_NsourceAddr+i) = *(p_NsourceAddr+i + ((numOfSrc-j-1)));
						*(p_NsourceAddr+i + ((numOfSrc-j-1))) = *(tmpAddress+i);

					}
				}
				j--;
				numOfSrc--;
				//numOfQuerySrc++;
#endif				
				continue;	//no need to move sourceAddr
			}
			else
			{
				if (ipVersion==IP_VERSION4)
				{
					//querySourceAddr
					IGMP_CTRL("Block.GIP.Clt.SrcIP(%pI4n) no in A*B",(p_NsourceAddr));////
				}
				else
				{
					IGMP_CTRL("Block.GIP6.Clt.SrcIP6(%pI6) no in A*B",(p_NsourceAddr));////
				}

			}

			if (ipVersion==IP_VERSION4)
			{
				p_NsourceAddr++;
			}
			else
			{
				p_NsourceAddr=p_NsourceAddr+4;
			}

			//IGMP_CTRL("");
 		}

	}

#if 0
	if (ipVersion==IP_VERSION4)
	{
		int i ;
		for (i=0; i<numOfQuerySrc; i++)
		{
			IGMP_CTRL("Query GIP(%pI4h)-SrcIP(%pI4n)", (groupAddress), ((p_NsourceAddr+i)));
		}
		//IGMPQueryVersion=3;
		//rtk_igmp_igmpQueryTimerExpired(0, IGMP_V3, (char *)groupAddress, numOfQuerySrc, NULL, (char *)p_NsourceAddr);
	}
	else
	{
		int i ;
		for (i=0; i<numOfQuerySrc; i+=4)
		{
			IGMP_CTRL("Query GIP6(%pI6)-SrcIP(%pI6)", (groupAddress), ((p_NsourceAddr+i)));
		}
		//IGMPQueryVersion=3; ???????????????????????????????????????????????
		//rtk_igmp_mldQueryTimerExpired(0);
	}
#endif


out:

	rtk_igmp_flow_reflush_update(FAIL,groupAddress);

	return RTK_IGMP_NF_CONTINUE;
}

//we only return RTK_IGMP_NF_DROP/RTK_IGMP_NF_ACCEPT
static uint32 rtk_igmp_processIgmpv3Mldv2Reports(rtk_igmp_pktHdr_t *pPkthdr, const struct net_device *SrcDev)
{
	uint32 clientAddr[4]={0, 0, 0, 0};
	uint32 i=0;
	uint16 numOfRecords=0;
	uint8 *groupRecords=NULL;
	uint8 recordType=0xff;
	uint16 numOfSrc=0;
	int32 returnVal=0;
	uint32 ipVersion;
	uint8 * pktBuf;
	uint16 igmpDataLen=0;

//	uint32 multicastRouterPortMask=rtk_igmp_getMulticastRouterPortMask(moduleIndex, ipVersion, rtk_igmp_sysUpSeconds);
	IGMP_CTRL("IGMPv3/MLDv2 Report packet");

	if(pPkthdr->iph && pPkthdr->ip6h==NULL && pPkthdr->igmph)
	{
		ipVersion= IP_VERSION4;
		pktBuf=(uint8 *)pPkthdr->igmph;
		clientAddr[0]=ntohl(pPkthdr->iph->saddr);
		igmpDataLen = ntohs(pPkthdr->iph->tot_len)-(pPkthdr->iph->ihl*4);
	}
	else if (pPkthdr->iph==NULL && pPkthdr->ip6h && pPkthdr->icmp6h)
	{
		ipVersion= IP_VERSION6;
		pktBuf=(uint8 *)pPkthdr->icmp6h;
		memcpy(&clientAddr[0],&pPkthdr->ip6h->saddr.s6_addr32[0],sizeof(clientAddr));
		igmpDataLen = ntohs(pPkthdr->ip6h->payload_len);
	}
	else if (pPkthdr->iph && pPkthdr->ip6h)
	{
		ipVersion= BOTH_IPV4_IPV6;
		IGMP_CTRL("Not support");
		return RTK_IGMP_NF_ACCEPT;
	}
	else
	{
		IGMP_CTRL("Not support");
		return RTK_IGMP_NF_ACCEPT;
	}


	if (ipVersion==IP_VERSION4)
	{
		numOfRecords=ntohs(((struct igmpv3Report *)pktBuf)->numOfRecords);
		if (numOfRecords!=0)
		{
			groupRecords=(uint8 *)(&(((struct igmpv3Report *)pktBuf)->recordList));
		}
	}
	else
	{
		numOfRecords=ntohs(((struct mldv2Report *)pktBuf)->numOfRecords);
		if (numOfRecords!=0)
		{
			groupRecords=(uint8 *)(&(((struct mldv2Report *)pktBuf)->recordList));
		}
	}


	for (i=0; i<numOfRecords; i++)
	{

		

		if (ipVersion==IP_VERSION4)
		{
			//check groupRecords is valid vaule 
			if (((void*)groupRecords-(void*)pPkthdr->igmph) > igmpDataLen)
				{ IGMP_CTRL(" IGMPv3 BufOver %p - %p > %d numOfRecords=%d",groupRecords,pPkthdr->igmph,igmpDataLen,numOfRecords); return RTK_IGMP_NF_ACCEPT;}

			recordType=((struct groupRecord *)groupRecords)->type;
		}
		else
		{
			//check groupRecords is valid vaule 
			if (((void*)groupRecords-(void*)pPkthdr->icmp6h) > igmpDataLen)
				{ IGMP_CTRL(" MLDv2 BufOver %p - %p > %d numOfRecords=%d",groupRecords,pPkthdr->icmp6h,igmpDataLen,numOfRecords); return RTK_IGMP_NF_ACCEPT;}			
			recordType=((struct mCastAddrRecord *)groupRecords)->type;
		}


		switch (recordType)
		{
			case MODE_IS_INCLUDE:
				returnVal=rtk_igmp_processIsInclude(pPkthdr, ipVersion, clientAddr, groupRecords);
			break;

			case MODE_IS_EXCLUDE:
				returnVal=rtk_igmp_processIsExclude(pPkthdr, ipVersion, clientAddr, groupRecords);
			break;

			case CHANGE_TO_INCLUDE_MODE:
				returnVal=rtk_igmp_processToInclude(pPkthdr, ipVersion, clientAddr, groupRecords);
			break;

			case CHANGE_TO_EXCLUDE_MODE:
				returnVal=rtk_igmp_processToExclude(pPkthdr, ipVersion, clientAddr, groupRecords);
			break;

			case ALLOW_NEW_SOURCES:
				returnVal=rtk_igmp_processAllow(pPkthdr, ipVersion, clientAddr, groupRecords);
			break;

			case BLOCK_OLD_SOURCES:
				returnVal=rtk_igmp_processBlock(pPkthdr, ipVersion, clientAddr ,groupRecords);
			break;

			default:break;

		}

		if( (numOfRecords==1)&&(returnVal==RTK_IGMP_NF_DROP || returnVal==RTK_IGMP_NF_ACCEPT))
			return returnVal;

		if (ipVersion==IP_VERSION4)
		{
			numOfSrc=ntohs(((struct groupRecord *)groupRecords)->numOfSrc);
			/*shift pointer to another group record*/
			groupRecords=groupRecords+8+numOfSrc*4+(((struct groupRecord *)(groupRecords))->auxLen)*4;
		}
		else
		{
			numOfSrc=ntohs(((struct mCastAddrRecord *)groupRecords)->numOfSrc);
			/*shift pointer to another group record*/
			groupRecords=groupRecords+20+numOfSrc*16+(((struct mCastAddrRecord *)(groupRecords))->auxLen)*4;
		}

	}

	return RTK_IGMP_NF_ACCEPT;//(multicastRouterPortMask & (~(1<<portNum)) & IGMP_SUPPORT_PORT_MASK);

}

rtk_rate_limit_ret_t check_rate_limit_umc(rtk_igmp_DevConfdb_t *devConf,uint32 is_igmp)
{
	unsigned long current_time=jiffies;
	unsigned long last_cycle_start_time;
	unsigned long diff;
	unsigned long cycle_duration=RATE_LIMIT_CYCLE_DEFINE_SECONDS*CONFIG_HZ;

	if(is_igmp){
		last_cycle_start_time=devConf->igmp.ratelimit_unkMc.cycle_start_time;
	}else{
		last_cycle_start_time=devConf->igmp6.ratelimit_unkMc.cycle_start_time;
	}

	//get diff time
	if(current_time < last_cycle_start_time){ //timer overflow case
		diff=(0xffffffff-last_cycle_start_time)+current_time;
	}else{
		diff=current_time-last_cycle_start_time;
	}

	if(diff > cycle_duration){
		//In a new cycle. Calculates the new cycle start time.
		last_cycle_start_time=current_time-(diff%cycle_duration);
		if(is_igmp){
			devConf->igmp.ratelimit_unkMc.cycle_start_time=last_cycle_start_time;
			devConf->igmp.ratelimit_unkMc.counter=1;
		}else{
			devConf->igmp6.ratelimit_unkMc.cycle_start_time=last_cycle_start_time;
			devConf->igmp6.ratelimit_unkMc.counter=1;
		}
		return RTK_RATE_LIMIT_ACCEPT_PACKET;
	}

	if(is_igmp){
		if(devConf->igmp.ratelimit_unkMc.counter < devConf->igmp.ratelimit_unkMc.rate){
			devConf->igmp.ratelimit_unkMc.counter++;
		}else{
			IGMP_RATE_LIMIT_DROP("Rate Limit: IGMP drop incoming packets");
			return RTK_RATE_LIMIT_DROP_PACKET;
		}
	}else{
		if(devConf->igmp6.ratelimit_unkMc.counter < devConf->igmp6.ratelimit_unkMc.rate){
			devConf->igmp6.ratelimit_unkMc.counter++;
		}else{
			IGMP_RATE_LIMIT_DROP("Rate Limit: MLD drop incoming packets");
			return RTK_RATE_LIMIT_DROP_PACKET;
		}
	}
	return RTK_RATE_LIMIT_ACCEPT_PACKET;
}


rtk_rate_limit_ret_t check_rate_limit(rtk_igmp_pktHdr_t *pPkthdr)
{
	rtk_igmp_DevConfdb_t *devConf;
	unsigned long current_time=jiffies;
	unsigned long last_cycle_start_time;
	unsigned long diff;
	unsigned long cycle_duration=RATE_LIMIT_CYCLE_DEFINE_SECONDS*CONFIG_HZ;

	int is_igmp=0;

	if(pPkthdr->iph && pPkthdr->ip6h==NULL){
		IGMP_CTRL("Rate Limit: IGMP Packet");
		is_igmp=1;
	}else if (pPkthdr->iph==NULL && pPkthdr->ip6h){
		IGMP_CTRL("Rate Limit: MLD Packet");
		is_igmp=0;
	}else if (pPkthdr->iph && pPkthdr->ip6h){
		IGMP_CTRL("Rate Limit: Not support");
		return RTK_RATE_LIMIT_ACCEPT_PACKET;
	}else{
		IGMP_CTRL("Rate Limit: Not support");
		return RTK_RATE_LIMIT_ACCEPT_PACKET;
	}

	devConf = &pPkthdr->devInfo->devConf;

	if(is_igmp){
		last_cycle_start_time=devConf->igmp.ratelimit.cycle_start_time;
	}else{
		last_cycle_start_time=devConf->igmp6.ratelimit.cycle_start_time;
	}

	//get diff time
	if(current_time < last_cycle_start_time){ //timer overflow case
		diff=(0xffffffff-last_cycle_start_time)+current_time;
	}else{
		diff=current_time-last_cycle_start_time;
	}

	if(diff > cycle_duration){
		//In a new cycle. Calculates the new cycle start time.
		last_cycle_start_time=current_time-(diff%cycle_duration);
		if(is_igmp){
			devConf->igmp.ratelimit.cycle_start_time=last_cycle_start_time;
			devConf->igmp.ratelimit.counter=1;
		}else{
			devConf->igmp6.ratelimit.cycle_start_time=last_cycle_start_time;
			devConf->igmp6.ratelimit.counter=1;
		}
		return RTK_RATE_LIMIT_ACCEPT_PACKET;
	}

	if(is_igmp){
		if(devConf->igmp.ratelimit.counter < devConf->igmp.ratelimit.rate){
			devConf->igmp.ratelimit.counter++;
		}else{
			IGMP_RATE_LIMIT_DROP("Rate Limit: IGMP drop incoming packets");
			return RTK_RATE_LIMIT_DROP_PACKET;
		}
	}else{
		if(devConf->igmp6.ratelimit.counter < devConf->igmp6.ratelimit.rate){
			devConf->igmp6.ratelimit.counter++;
		}else{
			IGMP_RATE_LIMIT_DROP("Rate Limit: MLD drop incoming packets");
			return RTK_RATE_LIMIT_DROP_PACKET;
		}
	}
	return RTK_RATE_LIMIT_ACCEPT_PACKET;
}

rtk_igmp_nf_ret_e_t rtk_igmp_mld_process( rtk_igmp_pktHdr_t *pPkthdr, const struct net_device *SrcDev)
{
	rtk_igmp_nf_ret_e_t ret=RTK_IGMP_NF_ACCEPT;
	rtk_igmp_DevConfdb_t *devConf;

	//eth0.x or nas0_x
	pPkthdr->devInfo=_rtk_igmp_devIfidx_to_devInfo(pPkthdr->devIfindex);
	if(pPkthdr->devInfo==NULL){IGMP_CTRL("pPkthdr->devInfo==NULL %s",SrcDev->name); return RTK_IGMP_NF_ACCEPT;}
	pPkthdr->moduleIndex = rtk_igmp_srcNetDevToModuleIdx(SrcDev,pPkthdr->devInfo);
	if(pPkthdr->moduleIndex ==FAIL) {IGMP_CTRL("moduleIndex not found!"); return RTK_IGMP_NF_ACCEPT;}

	devConf = &pPkthdr->devInfo->devConf;

	//check if smac in black list
	if(pPkthdr->ethHdr)
	{
		if(rtk_igmp_filterblackListCheck(pPkthdr->devInfo,pPkthdr->iph?IP_VERSION4:IP_VERSION6,pPkthdr->smac) == SUCCESS)
		{
			IGMP_CTRL("igmp/mld packet mac[%pM] in balck list, skip learning \n",pPkthdr->smac);
			return RTK_IGMP_NF_ACCEPT;
		}
	}


	if(pPkthdr->igmph)
	{

		if(devConf->igmp.igmpSnoopingDisable)
		{
			return RTK_IGMP_NF_ACCEPT;
		}

		switch(pPkthdr->igmph->type)
		{
			case IGMP_HOST_MEMBERSHIP_REPORT:
				IGMP_CTRL("IGMPv1 Report");

				if(devConf->igmp.ratelimit.rate)
				{
					if(RTK_RATE_LIMIT_DROP_PACKET==check_rate_limit(pPkthdr)){
						IGMP_RATE_LIMIT_DROP("Drop Packet due to the rate limit");
						return RTK_IGMP_NF_DROP;
					}
				}
				if((devConf->igmp.igmpVerDrp_v1) ||  (devConf->igmp.igmpReportDrop) )
				{
					IGMP_CTRL("igmpVerDrp_v1 || igmpReportDrop NF_DROP");
					return RTK_IGMP_NF_DROP;
				}
				if(devConf->igmp.igmpVerIgr_v1)
				{
					IGMP_CTRL("igmpVerIgr_v1 NF_ACCEPT");
					return RTK_IGMP_NF_ACCEPT;
				}
				ret=rtk_igmp_processJoin(pPkthdr,SrcDev);
				break;

			case IGMPV2_HOST_MEMBERSHIP_REPORT:
				IGMP_CTRL("IGMPv2 Report");

				if(devConf->igmp.ratelimit.rate)
				{
					if(RTK_RATE_LIMIT_DROP_PACKET==check_rate_limit(pPkthdr)){
						IGMP_RATE_LIMIT_DROP("Drop Packet due to the rate limit");
						return RTK_IGMP_NF_DROP;
					}
				}
				if((devConf->igmp.igmpVerDrp_v2) ||  (devConf->igmp.igmpReportDrop) )
				{
					IGMP_CTRL("igmpVerDrp_v2 || igmpReportDrop NF_DROP");
					return RTK_IGMP_NF_DROP;
				}
				if(devConf->igmp.igmpVerIgr_v2)
				{
					IGMP_CTRL("igmpVerIgr_v2 NF_ACCEPT");
					return RTK_IGMP_NF_ACCEPT;
				}
				ret=rtk_igmp_processJoin(pPkthdr,SrcDev);
				break;

			case IGMPV3_HOST_MEMBERSHIP_REPORT:
				IGMP_CTRL("IGMPv3 Report");
				if(devConf->igmp.ratelimit.rate)
				{
					if(RTK_RATE_LIMIT_DROP_PACKET==check_rate_limit(pPkthdr)){
						IGMP_RATE_LIMIT_DROP("Drop Packet due to the rate limit");
						return RTK_IGMP_NF_DROP;
					}
				}
				if((devConf->igmp.igmpVerDrp_v3) ||  (devConf->igmp.igmpReportDrop) )
				{
					IGMP_CTRL("igmpVerDrp_v3 || igmpReportDrop NF_DROP");
					return RTK_IGMP_NF_DROP;
				}
				if(devConf->igmp.igmpVerIgr_v3)
				{
					IGMP_CTRL("igmpVerIgr_v3 NF_ACCEPT");
					return RTK_IGMP_NF_ACCEPT;
				}
				ret=rtk_igmp_processIgmpv3Mldv2Reports(pPkthdr,SrcDev);
				break;

			//we don't want to care query packet.
			case IGMP_HOST_MEMBERSHIP_QUERY:
				IGMP_CTRL("IGMP Query");
				if(devConf->igmp.ratelimit.rate)
				{
					if(RTK_RATE_LIMIT_DROP_PACKET==check_rate_limit(pPkthdr)){
						IGMP_RATE_LIMIT_DROP("Drop Packet due to the rate limit");
						return RTK_IGMP_NF_DROP;
					}
				}
				//rtk_igmp_processQueries
#if defined(CONFIG_RTL_NEW_IGMP_REPORT_BEHAVIOR)
				ret=rtk_igmp_processQueries(pPkthdr,SrcDev);
#endif
				break;

			case IGMP_HOST_LEAVE_MESSAGE:
				IGMP_CTRL("IGMPv2 Leave");
				if(devConf->igmp.ratelimit.rate)
				{
					if(RTK_RATE_LIMIT_DROP_PACKET==check_rate_limit(pPkthdr)){
						IGMP_RATE_LIMIT_DROP("Drop Packet due to the rate limit");
						return RTK_IGMP_NF_DROP;
					}
				}
				if(devConf->igmp.igmpVerDrp_v2)
				{
					IGMP_CTRL("igmpVerDrp_v2  NF_DROP");
					return RTK_IGMP_NF_DROP;
				}
				if(devConf->igmp.igmpVerIgr_v2)
				{
					IGMP_CTRL("igmpVerIgr_v2  NF_ACCEPT");
					return RTK_IGMP_NF_ACCEPT;
				}
				ret=rtk_igmp_processLeave(pPkthdr,SrcDev);
				break;
			default:
				return RTK_IGMP_NF_ACCEPT;
				break;
		}
	}
	else if(pPkthdr->icmp6h)
	{
		if(devConf->igmp6.igmp6SnoopingDisable)
		{
			return RTK_IGMP_NF_ACCEPT;
		}
		switch(pPkthdr->icmp6h->icmp6_type)
		{
			case ICMPV6_MLD2_REPORT:
				IGMP_CTRL("MLDv2 REPORT");
				if(devConf->igmp6.ratelimit.rate)
				{
					if(RTK_RATE_LIMIT_DROP_PACKET==check_rate_limit(pPkthdr)){
						IGMP_RATE_LIMIT_DROP("Drop Packet due to the rate limit");
						return RTK_IGMP_NF_DROP;
					}
				}
				if(devConf->igmp6.igmp6VerDrp_v2 || devConf->igmp6.igmp6ReportDrop)
				{
					IGMP_CTRL("igmp6VerDrp_v2 || igmp6ReportDrop  NF_DROP");
					return RTK_IGMP_NF_DROP;
				}
				if(devConf->igmp6.igmp6VerIgr_v2)
				{
					IGMP_CTRL("igmp6VerIgr_v2  NF_ACCEPT");
					return RTK_IGMP_NF_ACCEPT;
				}
				ret=rtk_igmp_processIgmpv3Mldv2Reports(pPkthdr,SrcDev);
				break;

			//we don't want to care query packet.
			case ICMPV6_MGM_QUERY:
				IGMP_CTRL("MLDv QUERY");
				if(devConf->igmp6.ratelimit.rate)
				{
					if(RTK_RATE_LIMIT_DROP_PACKET==check_rate_limit(pPkthdr)){
						IGMP_RATE_LIMIT_DROP("Drop Packet due to the rate limit");
						return RTK_IGMP_NF_DROP;
					}
				}
				break;

			case ICMPV6_MGM_REPORT:
				IGMP_CTRL("MLDv1 REPORT");
				if(devConf->igmp6.ratelimit.rate)
				{
					if(RTK_RATE_LIMIT_DROP_PACKET==check_rate_limit(pPkthdr)){
						IGMP_RATE_LIMIT_DROP("Drop Packet due to the rate limit");
						return RTK_IGMP_NF_DROP;
					}
				}
				if(devConf->igmp6.igmp6VerDrp_v1 || devConf->igmp6.igmp6ReportDrop)
				{
					IGMP_CTRL("igmp6VerDrp_v1 || igmp6ReportDrop  NF_DROP");
					return RTK_IGMP_NF_DROP;
				}
				if(devConf->igmp6.igmp6VerIgr_v1)
				{
					IGMP_CTRL("igmp6VerIgr_v1  NF_ACCEPT");
					return RTK_IGMP_NF_ACCEPT;
				}
				ret=rtk_igmp_processJoin(pPkthdr,SrcDev);
				break;

			case ICMPV6_MGM_REDUCTION:
				IGMP_CTRL("MLDv1 Done");
				if(devConf->igmp6.ratelimit.rate)
				{
					if(RTK_RATE_LIMIT_DROP_PACKET==check_rate_limit(pPkthdr)){
						IGMP_RATE_LIMIT_DROP("Drop Packet due to the rate limit");
						return RTK_IGMP_NF_DROP;
					}
				}
				if(devConf->igmp6.igmp6VerDrp_v1)
				{
					IGMP_CTRL("igmp6VerDrp_v1  NF_DROP");
					return RTK_IGMP_NF_DROP;
				}
				if(devConf->igmp6.igmp6VerIgr_v1)
				{
					IGMP_CTRL("igmp6VerIgr_v1  NF_ACCEPT");
					return RTK_IGMP_NF_ACCEPT;
				}
				ret=rtk_igmp_processLeave(pPkthdr,SrcDev);
				break;

			default:
				return RTK_IGMP_NF_ACCEPT;
				break;
		}
	}

	IGMP_CTRL("IGMP/MLD Process ret:%d %s",ret,ret==RTK_IGMP_NF_DROP?"RTK_IGMP_NF_DROP":ret==RTK_IGMP_NF_ACCEPT?"RTK_IGMP_NF_ACCEPT":"RTK_IGMP_NF_CONTINUE");
	return ret;

}


#define MDB_SEARCH

int _rtk_igmp_setEgressDevToFwdInfo(struct rtk_igmp_multicastFwdInfo *multicastFwdInfo ,uint32 egressIfidx,uint8 extraPortValid,uint8 extraPortId,uint8 dmacMcToUcEn,uint8 *mac)
{

	int i,j;
	int32 firstInvalid=IGMP_MAX_DEV_NUM;
	int32 firstsubPortInvalid=IGMP_MAX_EXTDEV_PORT_NUM;
	int32 firstDmacUcToMcInvalid = IGMP_MAX_EXTDEV_MAC_NUM;

	i=0;j=0;
	for(i=0 ;i<IGMP_MAX_DEV_NUM;i++)
	{
		if(multicastFwdInfo->egressDevInfo[i].valid)
		{
			//forward to same egress dev reuse it
			if(multicastFwdInfo->egressDevInfo[i].devIfIdx==egressIfidx)
			{
				firstsubPortInvalid=IGMP_MAX_EXTDEV_PORT_NUM;
#if defined(CONFIG_RTL8367_SUPPORT) && defined(CONFIG_RTL9607C_SERIES) && defined(CONFIG_RTK_SKB_MARK2)
				if(extraPortValid)
				{
					for(j=0 ; j<IGMP_MAX_EXTDEV_PORT_NUM ;j++)
					{
						if(multicastFwdInfo->egressDevInfo[i].subPorts[j].valid)
						{
							//already here ignore
							if( multicastFwdInfo->egressDevInfo[i].subPorts[j].devExtraPortId==extraPortId)
								break;
						}
						else
						{
							if(firstsubPortInvalid == IGMP_MAX_EXTDEV_PORT_NUM)
							{
								firstsubPortInvalid=j;
								break;
							}
						}
					}
					//append new extport
					if(firstsubPortInvalid!=IGMP_MAX_EXTDEV_PORT_NUM)
					{
						multicastFwdInfo->egressDevInfo[i].subPorts[firstsubPortInvalid].valid=1;
						multicastFwdInfo->egressDevInfo[i].subPorts[firstsubPortInvalid].devExtraPortId=extraPortId;
					}
				}
#endif

				firstDmacUcToMcInvalid = IGMP_MAX_EXTDEV_MAC_NUM;

#if defined(CONFIG_RTK_FC_WIFI_AMSDU_OFFLOAD_BY_PE) && defined(IGMP_SUPPORT_MCTOUC)
				if(dmacMcToUcEn)
				{
					for(j=0;j<IGMP_MAX_EXTDEV_MAC_NUM;j++)
					{
						if(multicastFwdInfo->egressDevInfo[i].subDmac[j].valid)
						{
							//already here ignore
							if(memcmp(multicastFwdInfo->egressDevInfo[i].subDmac[j].dmac,mac,6)==0)
								break;
						}
						else
						{
							if(firstDmacUcToMcInvalid==IGMP_MAX_EXTDEV_MAC_NUM)
							{
								firstDmacUcToMcInvalid=j;
								break;
							}
						}

					}
					//append new mac
					if(dmacMcToUcEn && firstDmacUcToMcInvalid!=IGMP_MAX_EXTDEV_MAC_NUM)
					{
						multicastFwdInfo->egressDevInfo[i].subDmac[firstDmacUcToMcInvalid].valid=1;
						memcpy(multicastFwdInfo->egressDevInfo[i].subDmac[firstDmacUcToMcInvalid].dmac,mac,6);
					}
				}
#endif				
				
				return SUCCESS;
			}
		}
		else
		{
			if(firstInvalid==IGMP_MAX_DEV_NUM)
			{
				firstInvalid =i;
				break;
			}
			
		}
	}
	if(firstInvalid==IGMP_MAX_DEV_NUM) {IGMP_WARNING("IGMP_MAX_DEV_NUM EXCEED!! "); return FAIL;}

	multicastFwdInfo->egressDevInfo[firstInvalid].valid=1;
	multicastFwdInfo->egressDevInfo[firstInvalid].devIfIdx =egressIfidx;
#if defined(CONFIG_RTL8367_SUPPORT) && defined(CONFIG_RTL9607C_SERIES) && defined(CONFIG_RTK_SKB_MARK2)
	if(extraPortValid)
	{
		multicastFwdInfo->egressDevInfo[firstInvalid].subPorts[0].valid=1;
		multicastFwdInfo->egressDevInfo[firstInvalid].subPorts[0].devExtraPortId = extraPortId;
	}
#endif
#if defined(CONFIG_RTK_FC_WIFI_AMSDU_OFFLOAD_BY_PE) && defined(IGMP_SUPPORT_MCTOUC)
	if(dmacMcToUcEn)
	{
		multicastFwdInfo->egressDevInfo[firstInvalid].subDmac[0].valid=1;
		memcpy(multicastFwdInfo->egressDevInfo[i].subDmac[0].dmac,mac,6);
	}
#endif
#if 0
	//for Debug
	IGMP_WARNING("egressIfidx:%d dmacMcToUcEn:%d ",egressIfidx,dmacMcToUcEn);
	for(i=0;i<IGMP_MAX_DEV_NUM;i++)
	{
		IGMP_WARNING("Egr[%d] intf%d dmacMcToUcEn:%d",i,multicastFwdInfo->egressDevInfo[i].devIfIdx);
		for(j=0;j<IGMP_MAX_EXTDEV_MAC_NUM;j++)
		{
			if(multicastFwdInfo->egressDevInfo[i].subDmac[j].valid)
				IGMP_WARNING("mac[%d]:%pM",j,multicastFwdInfo->egressDevInfo[i].subDmac[j].dmac);
		}

	}
#endif
	return SUCCESS;
}



int _rtk_igmp_setClientToEgressPortInfo(struct rtk_igmp_multicastFwdInfo *multicastFwdInfo ,struct rtk_igmp_clientEntry* clientEntry)
{

	int i;
	int32 firstInvalid=IGMP_MAX_DEV_NUM;
	for(i=0 ;i<IGMP_MAX_DEV_NUM;i++)
	{
		if(multicastFwdInfo->egressDevInfo[i].valid)
			continue;
		else
		{
			firstInvalid =i;
			break;
		}
	}
	if(firstInvalid==IGMP_MAX_DEV_NUM) {IGMP_WARNING("IGMP_MAX_DEV_NUM EXCEED!! "); return FAIL;}

	multicastFwdInfo->egressDevInfo[firstInvalid].valid=1;
	multicastFwdInfo->egressDevInfo[firstInvalid].devIfIdx =clientEntry->inIfidx;

	return SUCCESS;
}

int _rtk_igmp_getWlanDevIfidx(uint16* ifidx)
{
	int i;

	for( i=0 ; i< IGMP_MAX_LAN_DEV_NUM; i++)
	{
		if(rtk_igmp_lanDevInfo[i].dev==NULL)
			continue;
		
#ifdef RTK_NETDEV_PRIV_FLAGS
		if (rtk_netdev_get_flags(rtk_igmp_lanDevInfo[i].dev)&(RTK_IFF_DOMAIN_WLAN))
#else
		if (rtk_igmp_lanDevInfo[i].dev->priv_flags & IFF_DOMAIN_WLAN)
#endif

		{
			*ifidx = rtk_igmp_lanDevInfo[i].ifindex;
			return 1;
		}
	}
	return 0;
}


static int __rtk_igmp_getMulticastDataFwdInfo(int32 moduleIndex,uint32 searchCacheFlowFirst, struct rtk_igmp_multicastDataInfo *multicastDataInfo, struct rtk_igmp_multicastFwdInfo *multicastFwdInfo)
{
	struct rtk_igmp_groupEntry * groupEntry=NULL;
	struct net_bridge *br =NULL;
	struct net_bridge_port *p;
	rtk_igmp_multicastDeviceInfo_t* devInfo;
	rtk_igmp_userResvGroup_t *grp2cpu=NULL;


	grp2cpu	=_rtk_igmp_checkGroupToPsTbl(multicastDataInfo->ipVersion,multicastDataInfo->groupAddr);
	if(grp2cpu)
	{
		if(grp2cpu->patten.isCopy2cpu==0)
		{
			multicastFwdInfo->cpuFlag=TRUE;
			multicastFwdInfo->reservedMCast=TRUE;
			return FAIL;
		}
		else
		{
			multicastFwdInfo->copy2cpu=TRUE;
			IGMP_DATA("Copy to CPU flow");
		}
	}
		 

	if(searchCacheFlowFirst)
	{
		struct rtk_igmp_mcastFlowEntry *mcastFlowEntry=NULL;
		mcastFlowEntry=rtk_igmp_searchMcastFlowEntry(moduleIndex, multicastDataInfo->ipVersion, multicastDataInfo->sourceIp, multicastDataInfo->groupAddr,multicastDataInfo->sport,multicastDataInfo->dport,multicastDataInfo->vlanTagif?multicastDataInfo->vlanId:FAIL,multicastDataInfo->svlanTagif?multicastDataInfo->svlanId:FAIL,multicastDataInfo->l4proto);
		if (mcastFlowEntry!=NULL)
		{
			if( igmpSysdb.unknownFlood && mcastFlowEntry->multicastFwdInfo.unknownMCast && (rtk_igmp_anyDevRateLimitUMC(0) || rtk_igmp_anyDevRateLimitUMC(1)))
			{				
				IGMP_DATA("SKIP CACHE because UMC RATE LIMIT");
				goto SKIP_CACHE;
			}

			memcpy(multicastFwdInfo, &mcastFlowEntry->multicastFwdInfo, sizeof(struct rtk_igmp_multicastFwdInfo));

			if(grp2cpu && grp2cpu->patten.isCopy2cpu)
				multicastFwdInfo->copy2cpu=1;
			
			mcastFlowEntry->fwdPktCnt++;
			if(multicastDataInfo->privateField & DUMMY_PKT)
				mcastFlowEntry->defineFwdPktCnt++;
			
			/* add/update flow*/
			if(multicastDataInfo->privateField & SKIP_LEARNING)
			{
				IGMP_CTRL("Do not flowSetCbEvt to hwcb because FC SKIP_LEARNING");
			}
			else if(hwCb.flowSetCbEvt && mcastFlowEntry->multicastFwdInfo.hwCbAcc==0)
			{
				rtk_igmp_flowCbEvt_t *pFlowCb=&igmpSysdb.flowCb;

				memset(pFlowCb,0,sizeof(*pFlowCb));
				if(mcastFlowEntry->ipVersion == IP_VERSION6)
					pFlowCb->isIPv6=1;
				memcpy(pFlowCb->group,mcastFlowEntry->groupAddr,sizeof(pFlowCb->group));
				memcpy(pFlowCb->sourceAddr,mcastFlowEntry->serverAddr,sizeof(pFlowCb->sourceAddr));
				memcpy(pFlowCb->egressDevInfo,mcastFlowEntry->multicastFwdInfo.egressDevInfo,sizeof(pFlowCb->egressDevInfo));
				pFlowCb->ingressCvlan = mcastFlowEntry->ingressCvlan;
				pFlowCb->ingressSvlan = mcastFlowEntry->ingressSvlan;
				pFlowCb->copy2cpu = multicastFwdInfo->copy2cpu;
				rtk_igmp_flowCbAppendOtherBrToCb(moduleIndex,multicastDataInfo,pFlowCb);
				hwCb.flowSetCbEvt(pFlowCb);
				mcastFlowEntry->multicastFwdInfo.hwCbAcc=1;
			}		
			
			return SUCCESS;
		}
	}

SKIP_CACHE:


	//flooding multicast to Br snooping disable ports
	br = netdev_priv(rtk_igmp_mCastModuleArray[moduleIndex].brDevInfo.dev);
	if(br != NULL)
	{
		uint32 disableSnooping=0;
		list_for_each_entry(p, &br->port_list, list)
		{
			disableSnooping=0;
			devInfo = _rtk_igmp_devIfidx_to_devInfo(p->dev->ifindex);
			if(devInfo)
			{
				if(multicastDataInfo->ipVersion == IP_VERSION4)
					disableSnooping = devInfo->devConf.igmp.igmpSnoopingDisable;
				else if(multicastDataInfo->ipVersion == IP_VERSION6)
					disableSnooping = devInfo->devConf.igmp6.igmp6SnoopingDisable;
				if(disableSnooping)
				{
					if ((multicastDataInfo->ipVersion==IP_VERSION4 && devInfo->devConf.igmp.igmpForwardDisable) || 
						(multicastDataInfo->ipVersion==IP_VERSION6 && devInfo->devConf.igmp6.igmp6ForwardDisable))
						continue;					
					_rtk_igmp_setEgressDevToFwdInfo(multicastFwdInfo,p->dev->ifindex,0,0,0,NULL);
					IGMP_DATA("Dev[%s] disable Snooping flooding to Dev",devInfo->dev->name);
				}
			}
		}
	}

#if defined(CONFIG_COMMON_RT_API) && defined(CONFIG_RTK_L34_FLEETCONNTRACK_ENABLE)
	//add wlan port to forward info if trapWlanToPS is enabled
	if(igmpSysdb.trapWlanToPS)
	{
		rtk_igmp_userGroup_cfg_t userGroup;
		uint16 ifidx;
		memset(&userGroup, 0, sizeof(userGroup));
		if(multicastDataInfo->ipVersion==IP_VERSION6)
		{
			userGroup.isIpv6 =1;
			memcpy(userGroup.group , multicastDataInfo->groupAddr, sizeof(userGroup.group));
		}else
			userGroup.group[0] = multicastDataInfo->groupAddr[0];
	
		//hwCb.userGroup_check(&userGroup);
		rt_igmp_userGroup_check(&userGroup);

		if(userGroup.valid)
		{
			if(_rtk_igmp_getWlanDevIfidx(&ifidx) == 1)
			{
				_rtk_igmp_setEgressDevToFwdInfo(multicastFwdInfo,ifidx,0,0,0,NULL);
			}
		}
	}
#endif

	groupEntry=rtk_igmp_searchGroupEntry(moduleIndex, multicastDataInfo->ipVersion, multicastDataInfo->groupAddr, multicastDataInfo->vlanId, multicastDataInfo->svlanId);
	if (groupEntry==NULL)
	{
		int isipv6=0;
		if(multicastDataInfo->ipVersion==IP_VERSION6)
			isipv6=1;
		
		if(igmpSysdb.unknownFlood)		
		{
			if(br != NULL)
			{
				list_for_each_entry(p, &br->port_list, list)
				{
					devInfo = _rtk_igmp_devIfidx_to_devInfo(p->dev->ifindex);
					if(devInfo)
					{
						if ((isipv6==0 && devInfo->devConf.igmp.igmpForwardDisable) || 
							(isipv6==1 && devInfo->devConf.igmp6.igmp6ForwardDisable))
							continue;
						if((isipv6==0 && devInfo->devConf.igmp.ratelimit_unkMc.rate) || 
						   (isipv6==1 && devInfo->devConf.igmp6.ratelimit_unkMc.rate))
						{
							//if any br lan device rate limit skip add to hardware
							if(rtk_igmp_anyDevRateLimitUMC(0) || rtk_igmp_anyDevRateLimitUMC(1))
								multicastDataInfo->privateField |= SKIP_LEARNING;

							//doing rate check
							if(RTK_RATE_LIMIT_DROP_PACKET==check_rate_limit_umc(&devInfo->devConf,!isipv6))
							{
								multicastFwdInfo->dropPkt=1;
								return FAIL;
							}
						}	
						_rtk_igmp_setEgressDevToFwdInfo(multicastFwdInfo,p->dev->ifindex,0,0,0,NULL);
						IGMP_DATA("Dev[%s] unknownFlood to all br Dev",devInfo->dev->name);
					}
				}
			}
			multicastFwdInfo->unknownMCast=TRUE;
		}

	}
	else
	{

		struct rtk_igmp_clientEntry * clientEntry=NULL;
		struct rtk_igmp_sourceEntry * sourceEntry=NULL;

		for (clientEntry=groupEntry->clientList; clientEntry!=NULL; clientEntry=clientEntry->next)
		{
			uint32 *sourceAddr = multicastDataInfo->sourceIp;
			uint32 careSource;
			uint8 dmacMcToUc=0;
			sourceEntry=NULL;

#if defined(CONFIG_RTL_PER_PORT_CLIENT_NUM)
            if((clientEntry->flag&FLAG_WAIT_STATUS)==1)
                    continue;
#endif
			devInfo = _rtk_igmp_devIfidx_to_devInfo(clientEntry->inIfidx);
			if(devInfo == NULL)
			{
				IGMP_DATA("skip invalid inIfidx=%d",clientEntry->inIfidx);
				continue;
			}
			if ((multicastDataInfo->ipVersion==IP_VERSION4 && devInfo->devConf.igmp.igmpForwardDisable) || 
				(multicastDataInfo->ipVersion==IP_VERSION6 && devInfo->devConf.igmp6.igmp6ForwardDisable))
			{
				IGMP_DATA("skip igmpForwardDisable/igmp6ForwardDisable dev inIfidx=%d",clientEntry->inIfidx);
				continue;		
			}
			if ((multicastDataInfo->ipVersion==IP_VERSION4 && devInfo->devConf.igmp.igmpSnoopingDisable) || 
				(multicastDataInfo->ipVersion==IP_VERSION6 && devInfo->devConf.igmp6.igmp6SnoopingDisable))
			{
				IGMP_DATA("skip igmpSnooping Dsiable Client inIfidx=%d (expect flooding by Snooping disable process)",clientEntry->inIfidx);
				continue;		
			}				
			if(multicastDataInfo->ipVersion==IP_VERSION4)
			{
				careSource = devInfo->devConf.igmp.igmpv3CareSource;
				dmacMcToUc = devInfo->devConf.igmp.igmpDmacMcToUc;
			}
			else
			{
				careSource = devInfo->devConf.igmp6.mldv2CareSource;
				dmacMcToUc = devInfo->devConf.igmp6.igmp6DmacMcToUc;	
			}

			if(careSource)
			{
				if (clientEntry->groupFilterTimer<=rtk_igmp_sysUpSeconds) /*include mode*/
				{
					sourceEntry = rtk_igmp_searchSourceEntry(groupEntry->ipVersion, sourceAddr, clientEntry);
					if (sourceEntry!=NULL)
					{
						if (sourceEntry->portTimer>rtk_igmp_sysUpSeconds)
						{
							IGMP_DATA("include inIfidx=%d",clientEntry->inIfidx);
							_rtk_igmp_setEgressDevToFwdInfo(multicastFwdInfo,clientEntry->inIfidx,clientEntry->extraPortInfo.valid,clientEntry->extraPortInfo.devExtraPortId,dmacMcToUc,clientEntry->clientMacAddr);
							continue;
						}
					}
				}
				else	/*exclude mode*/
				{
					sourceEntry = rtk_igmp_searchSourceEntry(groupEntry->ipVersion, sourceAddr, clientEntry);
					if (sourceEntry == NULL)
					{
						IGMP_DATA("source not record forwad inIfidx=%d",clientEntry->inIfidx);
						_rtk_igmp_setEgressDevToFwdInfo(multicastFwdInfo,clientEntry->inIfidx,clientEntry->extraPortInfo.valid,clientEntry->extraPortInfo.devExtraPortId,dmacMcToUc,clientEntry->clientMacAddr);
					}
					else
					{
						if (sourceEntry->portTimer>rtk_igmp_sysUpSeconds)
						{
							IGMP_DATA("source record forwad inIfidx=%d",clientEntry->inIfidx);
							_rtk_igmp_setEgressDevToFwdInfo(multicastFwdInfo,clientEntry->inIfidx,clientEntry->extraPortInfo.valid,clientEntry->extraPortInfo.devExtraPortId,dmacMcToUc,clientEntry->clientMacAddr);
						}
					}
				}

			}
			else /* don't care source */
			{
				if (clientEntry->groupFilterTimer<=rtk_igmp_sysUpSeconds) /*include mode*/
				{
					sourceEntry = clientEntry->sourceList;
					while (sourceEntry)
					{
						if (sourceEntry->portTimer>rtk_igmp_sysUpSeconds)
						{
							_rtk_igmp_setEgressDevToFwdInfo(multicastFwdInfo,clientEntry->inIfidx,clientEntry->extraPortInfo.valid,clientEntry->extraPortInfo.devExtraPortId,dmacMcToUc,clientEntry->clientMacAddr);
							IGMP_DATA("add in-mo clt.inIfidx(%d)", clientEntry->inIfidx);
							break;
						}
						sourceEntry = sourceEntry->next;
					}
				}
				else /*exclude mode*/
				{
					IGMP_DATA("add ex-mo clt.inIfidx(%d)", clientEntry->inIfidx);
					_rtk_igmp_setEgressDevToFwdInfo(multicastFwdInfo,clientEntry->inIfidx,clientEntry->extraPortInfo.valid,clientEntry->extraPortInfo.devExtraPortId,dmacMcToUc,clientEntry->clientMacAddr);
				}
			}

		}

	}

	if(grp2cpu && grp2cpu->patten.isCopy2cpu)
		multicastFwdInfo->copy2cpu=1;

	rtk_igmp_recordMcastFlow(moduleIndex,multicastDataInfo,multicastFwdInfo);


	return SUCCESS;

}


static int _rtk_igmp_getMulticastDataFwdInfo(int32 moduleIndex,uint32 searchCacheFlowFirst, struct rtk_igmp_multicastDataInfo *multicastDataInfo, struct rtk_igmp_multicastFwdInfo *multicastFwdInfo)
{
	int i,j,k;
	int ret=FAIL;
	int extraPortValid=0;
	uint8 mcToUc;
	struct rtk_igmp_multicastFwdInfo *p_tmp_multicastFwdInfo=&igmpSysdb.mcFwdInfo_tmp;
	uint8 combineID=rtk_igmp_mCastModuleArray[moduleIndex].brDevInfo.brDevConf.combineId;
	
	memset(multicastFwdInfo, 0, sizeof(struct rtk_igmp_multicastFwdInfo));
	i=0;j=0;k=0;

	if(combineID==0)
	{
		//disable combine with other br
		ret=__rtk_igmp_getMulticastDataFwdInfo(moduleIndex,searchCacheFlowFirst,multicastDataInfo,multicastFwdInfo);
	}
	else
	{

		for(i=0 ;i<IGMP_MAX_BR_MODULE_NUM ;i++)
		{
			if(rtk_igmp_mCastModuleArray[i].validBit && combineID==rtk_igmp_mCastModuleArray[i].brDevInfo.brDevConf.combineId)
			{
				memset(p_tmp_multicastFwdInfo, 0, sizeof(*p_tmp_multicastFwdInfo));
				ret=__rtk_igmp_getMulticastDataFwdInfo(i,searchCacheFlowFirst,multicastDataInfo,p_tmp_multicastFwdInfo);
				if(p_tmp_multicastFwdInfo->cpuFlag)
					multicastFwdInfo->cpuFlag=TRUE;
				if(p_tmp_multicastFwdInfo->unknownMCast)
					multicastFwdInfo->unknownMCast=TRUE;
				if(p_tmp_multicastFwdInfo->reservedMCast)
					multicastFwdInfo->reservedMCast=TRUE;
				if(p_tmp_multicastFwdInfo->copy2cpu)
					multicastFwdInfo->copy2cpu=TRUE;
				if(ret!=SUCCESS)
					break;
				
				for(j=0 ;j<IGMP_MAX_DEV_NUM ;j++)
				{
					if(p_tmp_multicastFwdInfo->egressDevInfo[j].valid)
					{
						extraPortValid=0;
						mcToUc=0;
#if defined(CONFIG_RTL8367_SUPPORT) && defined(CONFIG_RTL9607C_SERIES) && defined(CONFIG_RTK_SKB_MARK2)						
						for(k=0;k<IGMP_MAX_EXTDEV_PORT_NUM;k++)
						{
							if(p_tmp_multicastFwdInfo->egressDevInfo[j].subPorts[k].valid)
							{
								_rtk_igmp_setEgressDevToFwdInfo(multicastFwdInfo,p_tmp_multicastFwdInfo->egressDevInfo[j].devIfIdx,1,tmp_multicastFwdInfo.egressDevInfo[j].subPorts[k].devExtraPortId,0,NULL);
								extraPortValid=1;
							}
						}
#endif
#if defined(CONFIG_RTK_FC_WIFI_AMSDU_OFFLOAD_BY_PE) && defined(IGMP_SUPPORT_MCTOUC)
						for(k=0;k<IGMP_MAX_EXTDEV_MAC_NUM;k++)
						{
							if(p_tmp_multicastFwdInfo->egressDevInfo[j].subDmac[k].valid)
							{
								_rtk_igmp_setEgressDevToFwdInfo(multicastFwdInfo,p_tmp_multicastFwdInfo->egressDevInfo[j].devIfIdx,0,0,1,p_tmp_multicastFwdInfo->egressDevInfo[j].subDmac[k].dmac);
								extraPortValid=1;
							}
						}
#endif						
						if(extraPortValid==0 && mcToUc==0)
							_rtk_igmp_setEgressDevToFwdInfo(multicastFwdInfo,p_tmp_multicastFwdInfo->egressDevInfo[j].devIfIdx,0,0,0,0);
					}
					else
						break;
				}				
			}
			
		}

	}

	if(_rtk_igmp_IsflowToDevInfoIsNonValidPort(multicastFwdInfo->egressDevInfo))
	{
		multicastFwdInfo->dropPkt=1;
	}

	return ret;
}



int rtk_igmp_getMulticastDataFwdInfo(const struct net_device *SrcDev,const struct net_device *DstDev,uint32 searchCacheFlowFirst, struct rtk_igmp_multicastDataInfo *multicastDataInfo, struct rtk_igmp_multicastFwdInfo *multicastFwdInfo)
{

	int32 moduleIndex= 0;

	if (multicastFwdInfo==NULL || multicastDataInfo==NULL)
	{
		IGMP_DATA("multicastFwdInfo or multicastDataInfo is null, return\n");
		return FAIL;
	}

	if(DstDev)
	{
		//br_x to moduleIndex in postrouting
		moduleIndex = rtk_igmp_dstNetDevToModuleIdx(DstDev);
		if(moduleIndex ==FAIL) {IGMP_DATA("%s ==> moduleIndex not found!",DstDev->name); return FAIL;}
	}
	else
	{
		//nas0_x/eth0.x  to moduleIndex in prerouting
		moduleIndex = rtk_igmp_srcNetDevToModuleIdx(SrcDev,NULL);
		if(moduleIndex ==FAIL) {IGMP_DATA("%s ==> moduleIndex not found!",SrcDev->name); return FAIL;}
	}

	return _rtk_igmp_getMulticastDataFwdInfo(moduleIndex,searchCacheFlowFirst,multicastDataInfo,multicastFwdInfo);


}

int rtk_igmp_devStatistic_get(rt_igmpHook_devInfo_t devInfo,unsigned int isIpv6,rt_igmpHook_devStatistic_t *statisticInfo)
{
	int32 _devifid=FAIL;
	int getIdx=0;
	int32 activeCnt=0;
	int32 curWeight=0;
	rt_igmpHook_groupStatistic_t groupstatisticInfo;
	
	rtk_igmp_multicastDeviceInfo_t *devcfg=NULL;
	rtk_igmp_multicastModule_t *brcfg=NULL;
	
	_devifid = rtk_igmp_devInfoToIfIdx(devInfo);
	if(_devifid==FAIL)
		return FAIL;

	devcfg =_rtk_igmp_devIfidx_to_devInfo(_devifid);
	brcfg = _rtk_igmp_devIfidx_to_BrDevInfo(_devifid);

	if(devcfg==NULL && brcfg==NULL)
		return FAIL;


	while(1)
	{
		if(rtk_igmp_nextValidGroupStatistic_get(devInfo,&getIdx,isIpv6,&groupstatisticInfo)==SUCCESS && getIdx>=0)
		{
			getIdx++;
			activeCnt++;
			curWeight+=_rtk_igmp_checkGroupWeightTbl(_devifid,isIpv6?IP_VERSION6:IP_VERSION4,groupstatisticInfo.groupAddr);
		}
		else
			break;
	}

	statisticInfo->activeGroupCnt=activeCnt;
	statisticInfo->curWeight =curWeight;
	if(devcfg)
	{
		if(isIpv6)
		{
			statisticInfo->bandwidthExceedCnt = devcfg->devStatistic.igmp6WeightExceedCnt;
			statisticInfo->joinCnt =devcfg->devStatistic.igmp6JoinCnt;
		}
		else
		{
			statisticInfo->bandwidthExceedCnt = devcfg->devStatistic.igmp4WeightExceedCnt;
			statisticInfo->joinCnt =devcfg->devStatistic.igmp4JoinCnt;
		}
	}
	else if(brcfg)
	{
		if(isIpv6)
		{
			statisticInfo->bandwidthExceedCnt = brcfg->brDevInfo.brStatistic.igmp6WeightExceedCnt;
			statisticInfo->joinCnt =  brcfg->brDevInfo.brStatistic.igmp6JoinCnt;

		}
		else
		{
			statisticInfo->bandwidthExceedCnt = brcfg->brDevInfo.brStatistic.igmp4WeightExceedCnt;
			statisticInfo->joinCnt =  brcfg->brDevInfo.brStatistic.igmp4JoinCnt;
		}
	}
	
	IGMP_CTRL("Get activeGroupCnt=%d  curWeight=%d joinCnt=%d bandwidthExceedCnt=%d",statisticInfo->activeGroupCnt,statisticInfo->curWeight,statisticInfo->joinCnt,statisticInfo->bandwidthExceedCnt);
	
	return SUCCESS;
}


int rtk_igmp_nextValidGroupStatistic_get(rt_igmpHook_devInfo_t devInfo,int *getIdx,unsigned int isIpv6,rt_igmpHook_groupStatistic_t *statisticInfo)
{
	int32 _devifid=FAIL;
	rtk_igmp_multicastDeviceInfo_t *devcfg=NULL;
	rtk_igmp_multicastModule_t *brcfg=NULL;
	int32 searchMin,searchIdx;
	uint32 clinetJoinSec=0;

	int i;
	int32 hitGroupAndDevIdx=FAIL;
	int32 hashIndex;
	int32 moduleIndex;
	struct rtk_igmp_groupEntry *groupEntryPtr;
	struct rtk_igmp_clientEntry* clientEntry=NULL;

	_devifid = rtk_igmp_devInfoToIfIdx(devInfo);
	if(_devifid==FAIL)
		return FAIL;

	devcfg =_rtk_igmp_devIfidx_to_devInfo(_devifid);
	brcfg = _rtk_igmp_devIfidx_to_BrDevInfo(_devifid);

	if(devcfg==NULL && brcfg==NULL)
		return FAIL;

	searchMin=*getIdx;
	searchIdx=atomic_read(&groupIncIdx);


	for(moduleIndex=0 ; moduleIndex<IGMP_MAX_BR_MODULE_NUM;moduleIndex++ )
	{
		if(brcfg &&rtk_igmp_mCastModuleArray[moduleIndex].brDevInfo.ifindex!=_devifid)
			continue;			
		if(rtk_igmp_mCastModuleArray[moduleIndex].validBit==0)
			continue;			
		for (hashIndex=0;hashIndex<IGMP_HASH_TBL_SIZE;hashIndex++)
		{
			if(isIpv6)
				groupEntryPtr=rtk_igmp_mCastModuleArray[moduleIndex].rtk_igmp_ipv6HashTable[hashIndex];
			else
				groupEntryPtr=rtk_igmp_mCastModuleArray[moduleIndex].rtk_igmp_ipv4HashTable[hashIndex];

			while (groupEntryPtr!=NULL)
			{
				//IGMP_API("searchMin=%d groupEntryPtr->groupIdx=%d searchIdx=%d",searchMin,groupEntryPtr->groupIdx,searchIdx);
				if(searchMin <= groupEntryPtr->groupIdx  && groupEntryPtr->groupIdx <= searchIdx)
				{
					statisticInfo->curClientExistTimesSec=-1;
					memset(statisticInfo->curClient,0,sizeof(statisticInfo->curClient));
					
					clientEntry=groupEntryPtr->clientList;
					while (clientEntry!=NULL)
					{
/*						
						if(devcfg)
							IGMP_API("devcfg clientEntry->inIfidx=%d _devifid=%d",clientEntry->inIfidx,_devifid);
						else
							IGMP_API("Br clientEntry->inIfidx=%d _devifid=%d",clientEntry->inIfidx,_devifid);
*/
						if((devcfg && clientEntry->inIfidx ==_devifid) || (brcfg) )
						{
							//hit update group 
							searchIdx = groupEntryPtr->groupIdx;
							hitGroupAndDevIdx=searchIdx;
							memcpy(statisticInfo->groupAddr,groupEntryPtr->groupAddr,sizeof(statisticInfo->groupAddr));
							clinetJoinSec = rtk_igmp_sysUpSeconds -clientEntry->joinSystemSecTime;
							if(clinetJoinSec < statisticInfo->curClientExistTimesSec)
							{
								memcpy(statisticInfo->curClient,clientEntry->clientAddr,sizeof(statisticInfo->curClient));
								statisticInfo->curClientExistTimesSec =clinetJoinSec ;
							}
						}
					
						clientEntry = clientEntry->next;
					}
				}
				groupEntryPtr=groupEntryPtr->next;
			}
			
		}
	}


	if(hitGroupAndDevIdx!=FAIL)
	{
		//we can search sip/vlan from flow input(devIdx,group)
		struct rtk_igmp_mcastFlowEntry *mcastFlowEntry=NULL;
	
		//init to zero
		statisticInfo->vlanId=0;
		statisticInfo->svlanId=0;
		memset(statisticInfo->sourceAddr,0,sizeof(statisticInfo->sourceAddr));
	
		for(moduleIndex=0 ; moduleIndex<IGMP_MAX_BR_MODULE_NUM;moduleIndex++ )
		{
			if(brcfg &&rtk_igmp_mCastModuleArray[moduleIndex].brDevInfo.ifindex!=_devifid)
				continue;			
			if(rtk_igmp_mCastModuleArray[moduleIndex].validBit==0)
				continue;				
			for(hashIndex=0;hashIndex<IGMP_HASH_TBL_SIZE;hashIndex++)
			{
				/*to dump multicast flow information*/
				mcastFlowEntry=rtk_igmp_mCastModuleArray[moduleIndex].flowHashTable[hashIndex];
				while(mcastFlowEntry!=NULL)
				{
					if((isIpv6==0 && mcastFlowEntry->ipVersion==IP_VERSION4) ||(isIpv6==1 &&mcastFlowEntry->ipVersion==IP_VERSION6) )
					{
						if(memcmp(mcastFlowEntry->groupAddr,statisticInfo->groupAddr,sizeof(mcastFlowEntry->groupAddr))==0 )
						{
							for(i=0;i<IGMP_MAX_DEV_NUM;i++)
							{
								if(devcfg && mcastFlowEntry->multicastFwdInfo.egressDevInfo[i].devIfIdx!=_devifid)
									continue;

								memcpy(statisticInfo->sourceAddr,mcastFlowEntry->serverAddr,sizeof(statisticInfo->sourceAddr));
								statisticInfo->vlanId = mcastFlowEntry->ingressCvlan==FAIL?0:mcastFlowEntry->ingressCvlan;
								statisticInfo->svlanId = mcastFlowEntry->ingressSvlan==FAIL?0:mcastFlowEntry->ingressSvlan;
								goto GET_SUCCESS;
							}
						}
					}
					mcastFlowEntry=mcastFlowEntry->next;
				}
			}
		}
	}


GET_SUCCESS:

	*getIdx=hitGroupAndDevIdx;

	if(*getIdx !=FAIL)
	{

		if(isIpv6)
		{
			IGMP_API("DIP:%pI6 SIP:%pI6 curClient=%pI6(ExitstSec:%d) Vlan:%d  SVlan:%d",
				statisticInfo->groupAddr,statisticInfo->sourceAddr,statisticInfo->curClient,statisticInfo->curClientExistTimesSec,statisticInfo->vlanId,statisticInfo->svlanId);
		}
		else
		{
			IGMP_API("DIP:%pI4h SIP:%pI4h curClient=%pI4h(ExitstSec:%d) Vlan:%d SVlan:%d",
				statisticInfo->groupAddr,statisticInfo->sourceAddr,statisticInfo->curClient,statisticInfo->curClientExistTimesSec,statisticInfo->vlanId,statisticInfo->svlanId);
		}
		return SUCCESS;

	}
	else
	{
		//IGMP_API("no more entry hit here");
		return FAILED;
	}
		
}

