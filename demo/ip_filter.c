//create @20170510 
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <arpa/inet.h>
#include "ip_filter.h"

//平均每个hash桶下的深度，用于计算桶数目
#define IP_FILTER_HASH_DEPTH_SMALL 1     //总数 < 4096
#define IP_FILTER_HASH_DEPTH_MEDIUM 5    //总数 < 50000
#define IP_FILTER_HASH_DEPTH_LARGE 24    //总数 > 50000

//获取某个ip地址的hash索引 num-hash表的桶数目
#define IP_FILTER_HASHIDX(ipv4, num) (((ipv4) ^ (ipv4 >> 4)) % (num))

//FILTER模块打印函数
#define IP_FILTER_LOG_DEBUG(fmt,...) printf("[IP_FILTER][DEBUG][%s:%d]"fmt, __FUNCTION__, __LINE__, ##__VA_ARGS__);
#define IP_FILTER_LOG_ERROR(fmt,...) printf("[IP_FILTER][ERROR][%s:%d]"fmt, __FUNCTION__, __LINE__, ##__VA_ARGS__);

//linux x86 原子锁
#define ATOMIC_BOOL_COM_SWAP(ptr, old_val, new_val) __sync_bool_compare_and_swap((ptr), (old_val), (new_val))

///////////////////////////////////////////////////////////////////////////////////
// 内部hash表定义
// filter表目前以hash表进行匹配
///////////////////////////////////////////////////////////////////////////////////
//hash表中每个节点的数据结构，目前以双向链表关联
typedef struct _hash_node_s
{
	int32_t ipv4Addr;
	struct _hash_node_s* next;
	struct _hash_node_s* pre;
}HASH_NODE_S;

//hash桶数据结构
typedef struct
{
    HASH_NODE_S*   buck_head;   //桶头指针
    uint32_t       hcur_node;   //当前桶下的节点数
    uint32_t       buck_lock;   //桶锁
}HASH_BUCK_S;

//hash表内部使用环形缓存，动态申请释放 
typedef struct
{
	uint32_t     nodeCnt;       //环形缓存中节点数目
	uint32_t     nodeSize;      //环形缓存中每个节点大小
	uint32_t     head;          //环形缓存头
	uint32_t     tail;          //环形缓存尾部
	uint32_t     lock;          //环形缓存内部锁
	void**       nodeArray;     //环形缓存数组控制结构
	void*        nodeBuf;		//环形缓存缓存区
}FIFO_CTRL;

//hash表数据结构
typedef struct
{
    uint32_t     nodeNum;       //节点总数
	uint32_t     buckNum;       //哈希表桶数
	uint32_t     hcur_node;     //当前节点数量
	FIFO_CTRL*   fifoCtrl;    //数据缓存
	HASH_BUCK_S* buck;          //桶数组的指针
}IP_FILTER_HASH_TABLE;

//环形队列实现
//申请一块环形缓存
//nodeCnt - 环形缓存池大小
//nodeSize - 环形缓存池中单个元素的大小
//ctrl - 环形缓存控制结构
int ip_filter_fifo_alloc_buf(uint32_t nodeCnt, uint32_t nodeSize, FIFO_CTRL* ctrl)
{
	if (NULL == ctrl)
	{
		IP_FILTER_LOG_ERROR("Para Error, ctrl NULL! \n");
		return -1;
	}

	void* nodeBuf = (void *)malloc(nodeCnt * nodeSize);
	if (NULL == nodeBuf)
	{
		IP_FILTER_LOG_ERROR("Malloc Mem Error, nodeCnt:%d, nodeSize:%d ! \n", nodeCnt, nodeSize);
		return -1;
	}

	void** array = (void **)malloc(nodeCnt * sizeof(void *));
	if (NULL == array)
	{
		IP_FILTER_LOG_ERROR("Malloc array Error, nodeCnt:%d! \n", nodeCnt);
		return -1;
	}

	//分配内存指针至array中
	int i = 0;
	for (; i < nodeCnt; i++)
	{
		array[i] = nodeBuf + i * nodeSize;
	}

	//初始化fifo ctrl各个结构
	ctrl->head = 0;
	ctrl->tail = 0;
	ctrl->lock = 0;
	ctrl->nodeCnt = nodeCnt;
	ctrl->nodeSize = nodeSize;
	ctrl->nodeArray = array;
	ctrl->nodeBuf = nodeBuf;

	return 0;
}

int ip_filter_fifo_free_buf(FIFO_CTRL* ctrl)
{
	if (NULL == ctrl)
	{
		IP_FILTER_LOG_ERROR("Para Error, ctrl NULL! \n");
		return -1;
	}

	if (NULL != ctrl->nodeArray)
		free(ctrl->nodeArray);

	if (NULL != ctrl->nodeBuf)
		free(ctrl->nodeBuf);

	return 0;
}

void ip_filter_fifo_lock(FIFO_CTRL* ctrl)
{
	while(1)
	{
		if (ATOMIC_BOOL_COM_SWAP(&(ctrl->lock), 0, 1))
			break;
	}

	return ;
}

void ip_filter_fifo_unlock(FIFO_CTRL* ctrl)
{
	ctrl->lock = 0;
	return ;
}

//支持并发的环形队列申请释放接口
//参见DPDK:rte_ring.h中__rte_ring_mp_do_enqueue 接口等
int ip_filter_fifo_get_buf(void** node, FIFO_CTRL* ctrl)
{
	if (NULL == node || NULL == ctrl)
	{
		IP_FILTER_LOG_ERROR("Para Error, ctrl:%p, node:%p ! \n", ctrl, node);
		return -1;
	}

	int tmp, tmp_head;
	*node = NULL;
	
    while(1)
    {
        tmp = ctrl->head;        
        tmp_head = tmp+1;
        if (tmp_head >= ctrl->nodeCnt)
            tmp_head = 0;

		if (tmp_head == ctrl->tail)
			return -2; // 分配完毕

        if (ATOMIC_BOOL_COM_SWAP(&(ctrl->head), tmp, tmp_head)) //CAS比较，相当于锁
            break;
    }

	*node = ctrl->nodeArray[tmp]; //从内存池中获取一个元素缓存
	return 0;
}

int ip_filter_fifo_ret_buf(void* node, FIFO_CTRL* ctrl)
{
	if (NULL == node || NULL == ctrl)
	{
		IP_FILTER_LOG_ERROR("Para Error, ctrl:%p, node:%p ! \n", ctrl, node);
		return -1;
	}

	int tmp, tmp_tail;
	while (1)
	{
		tmp_tail = ctrl->tail;
		tmp = ctrl->tail + 1;
	    if (tmp > ctrl->nodeCnt)
	        tmp = 0;
		
	    if (tmp == ctrl->head)
	        return -2;  //环形队列已满

		if (ATOMIC_BOOL_COM_SWAP(&(ctrl->tail), tmp_tail, tmp))
            break;
	}

	ctrl->nodeArray[tmp] = node;  //返回至内存池中	
	return 0;
}

//为一个hash表的某个桶加锁
void ip_filter_hash_buck_lock(uint32_t hashIdx, IP_FILTER_HASH_TABLE* hash_table)
{
	uint32_t* buck_lock = &(hash_table->buck[hashIdx].buck_lock);
	while(1)
	{
		if (ATOMIC_BOOL_COM_SWAP(buck_lock, 0, 1))
			break;
	}

	return ;
}

//释放一个hash表的某个桶锁
void ip_filter_hash_buck_unlock(int32_t hashIdx, IP_FILTER_HASH_TABLE* hash_table)
{
	hash_table->buck[hashIdx].buck_lock = 0;	
	return ;
}

int ip_filter_hash_init(uint32_t nodeCnt, IP_FILTER_HASH_TABLE* hash_table)
{
    //根据初始化的节点数目，动态计算需要的hash桶数目
    uint32_t buckNum = 0;
	if (nodeCnt <= 4096)
		buckNum = nodeCnt / IP_FILTER_HASH_DEPTH_SMALL;
	else if (nodeCnt > 4096 && nodeCnt <= 50000)
	    buckNum = nodeCnt / IP_FILTER_HASH_DEPTH_MEDIUM;
	else
		buckNum = nodeCnt / IP_FILTER_HASH_DEPTH_LARGE;

    //申请buck数组缓存
    void* buckBuf = (void*)malloc(buckNum * sizeof(HASH_BUCK_S));
    if (NULL == buckBuf)
   	{
   	    IP_FILTER_LOG_ERROR("Malloc Buck Error,nodeCnt:%d, buckNum:%d! \n", nodeCnt, buckNum);
		return -1;
	}
	else
	{
		IP_FILTER_LOG_DEBUG("Malloc Buck Ok,nodeCnt:%d, buckNum:%d! \n", nodeCnt, buckNum);
		memset(buckBuf, 0, buckNum * sizeof(HASH_BUCK_S));
	}

	//申请hash节点缓存
	FIFO_CTRL* fifoCtrl = (FIFO_CTRL*)malloc(sizeof(FIFO_CTRL));
	if (NULL == fifoCtrl)
	{
   	    IP_FILTER_LOG_ERROR("Malloc FIFO Ctrl Error ! \n");
		return -1;
	}
	else
	{
		IP_FILTER_LOG_DEBUG("Malloc FIFO Ctrl Ok ! \n");
		memset(fifoCtrl, 0, sizeof(FIFO_CTRL));
	}

	//初始化hash节点缓存
	if (0 != ip_filter_fifo_alloc_buf(nodeCnt, sizeof(HASH_NODE_S), fifoCtrl))
	{
		IP_FILTER_LOG_ERROR("Alloc FIFO Ctrl Buf Error, nodeCnt:%u, size:%d! \n", nodeCnt, (int)sizeof(HASH_NODE_S));
		return -1;
	}
	else
	{
		IP_FILTER_LOG_DEBUG("Alloc FIFO Ctrl Buf Ok, nodeCnt:%u, size:%d! \n", nodeCnt, (int)sizeof(HASH_NODE_S));
	}

	//成功
	hash_table->buckNum = buckNum;
    hash_table->nodeNum = nodeCnt;
	hash_table->hcur_node = 0;
	hash_table->fifoCtrl = fifoCtrl;
	hash_table->buck = buckBuf;

	return 0;
}

int ip_filter_hash_add(int32_t ipv4Addr, IP_FILTER_HASH_TABLE* hash_table)
{
    if (NULL == hash_table)
   	{
   	    IP_FILTER_LOG_ERROR("Para Error, hash_table NULL! \n");
		return -1;
	}

	uint32_t hashIdx = IP_FILTER_HASHIDX(ipv4Addr, hash_table->buckNum);

    //锁桶
    ip_filter_hash_buck_lock(hashIdx, hash_table);
	
	//先遍历下当前hash桶下是否有待添加的节点
	HASH_NODE_S* head = hash_table->buck[hashIdx].buck_head;
	HASH_NODE_S* tmp = head;
	while (NULL != tmp)
	{
		if (tmp->ipv4Addr == ipv4Addr)
		{
            //释放桶锁
			ip_filter_hash_buck_unlock(hashIdx, hash_table);
			return 1; //hash表中已经存在该节点
		}

		tmp = tmp->next;
	}

	//不存在表中，添加至桶的头节点
	//申请一个hash节点内存
	HASH_NODE_S* addNode = NULL;
	int ret = ip_filter_fifo_get_buf((void**)&addNode, hash_table->fifoCtrl);
	if (0 != ret || NULL == addNode)
	{
		IP_FILTER_LOG_ERROR("Get Buf from FIFO Err, ipv4:%d, ret=%d !\n", ipv4Addr, ret);
		//释放桶锁
		ip_filter_hash_buck_unlock(hashIdx, hash_table);
		return -1;
	}

	addNode->ipv4Addr = ipv4Addr;
	addNode->pre = NULL;
	addNode->next = head;
	if (NULL != head)
	{
		head->pre = addNode;
		
	}
	
    hash_table->buck[hashIdx].buck_head = addNode;

	//释放桶锁
	ip_filter_hash_buck_unlock(hashIdx, hash_table);
	return 0;  //添加成功
}

int ip_filter_hash_del(int32_t ipv4Addr, IP_FILTER_HASH_TABLE* hash_table)
{
	if (NULL == hash_table)
	{
		IP_FILTER_LOG_ERROR("Para Error, hash_table NULL! \n");
		return -1;
	}

	//计算hash索引
	int32_t hashIdx = IP_FILTER_HASHIDX(ipv4Addr, hash_table->buckNum);

	//锁桶
	ip_filter_hash_buck_lock(hashIdx, hash_table);
	
	//先遍历下当前hash桶下是否有待添加的节点
	HASH_NODE_S* tmp = hash_table->buck[hashIdx].buck_head;
	while (NULL != tmp)
	{
	    //找到该节点
		if (tmp->ipv4Addr == ipv4Addr)
			break;

		tmp = tmp->next;
	}

    if (NULL == tmp)
   	{
   	    //释放桶锁
	    ip_filter_hash_buck_unlock(hashIdx, hash_table);
        IP_FILTER_LOG_ERROR("ipv4 %d not in filter table!\n", ipv4Addr);
		return -2; //待删除的节点不在表中，异常!!
	}

	//存在表中，进行删除
    HASH_NODE_S* pre = tmp->pre;
	HASH_NODE_S* next = tmp->next;
	if (NULL != pre)
		pre->next = next;
	else
	{
		//删除的是表头
		hash_table->buck[hashIdx].buck_head = next;
	}

	if (NULL != next)
		next->pre = pre;

    //释放这个节点的内存
    ip_filter_fifo_ret_buf(tmp, hash_table->fifoCtrl);

	//释放桶锁
	ip_filter_hash_buck_unlock(hashIdx, hash_table);

	return 0; //删除成功
}

HASH_NODE_S* ip_filter_hash_find(int32_t ipv4Addr, IP_FILTER_HASH_TABLE* hash_table)
{
	if (NULL == hash_table)
	{
		IP_FILTER_LOG_ERROR("Para Error, hash_table NULL! \n");
		return NULL;
	}

	//计算hash索引
	int32_t hashIdx = IP_FILTER_HASHIDX(ipv4Addr, hash_table->buckNum);

	//锁桶
	ip_filter_hash_buck_lock(hashIdx, hash_table);
	
	//先遍历下当前hash桶下是否有待添加的节点
	HASH_NODE_S* tmp = hash_table->buck[hashIdx].buck_head;
	while (NULL != tmp)
	{
		//找到该节点
		if (tmp->ipv4Addr == ipv4Addr)
			break;

		tmp = tmp->next;
	}

	//释放桶锁
	ip_filter_hash_buck_unlock(hashIdx, hash_table);
	return tmp;
}


int ip_filter_hash_destroy(IP_FILTER_HASH_TABLE* hash_table)
{
	if (NULL == hash_table)
	{
		IP_FILTER_LOG_ERROR("Para Error, hash_table NULL! \n");
		return -1;
	}

	if (NULL != hash_table->buck)
	{
		free(hash_table->buck);
		hash_table->buck = NULL;
	}

	if (NULL != hash_table->fifoCtrl)
	{
		ip_filter_fifo_free_buf(hash_table->fifoCtrl);
		free(hash_table->fifoCtrl);
		hash_table->fifoCtrl = NULL;
	}

	return 0;
}

///////////////////////////////////////////////////////////////////////////////////

/*********************************************************************************
* 函数名称：ip_filter_init
* 功能描述：初始化ip filter 表
* 输入：
*      ip_cnt：  filter表可容纳的ip数目
* 输出：
*      filter_table: 初始化成功的filter表
* 返回值：成功返回IP_FILTER_OK，失败返回IP_FILTER_ERR。
*********************************************************************************/
IP_FILTER_RET ip_filter_init(uint32_t ip_cnt, IP_FILTER_TABLE* filter_table)
{
	if (NULL == filter_table)
	{
		IP_FILTER_LOG_ERROR("Para Error, filter_table NULL! \n");
		return IP_FILTER_ERR;
	}

	IP_FILTER_HASH_TABLE* hash_table = (IP_FILTER_HASH_TABLE *)malloc(sizeof(IP_FILTER_HASH_TABLE));
	memset(hash_table, 0, sizeof(IP_FILTER_HASH_TABLE));
	if (0 != ip_filter_hash_init(ip_cnt, hash_table))
	{
		IP_FILTER_LOG_ERROR("ip_filter_hash_init ERROR, nodeCnt:%d! \n", ip_cnt);
		free(hash_table);
		return IP_FILTER_ERR;
	}

	filter_table->capacity = ip_cnt;
	filter_table->currCnt = 0;
	filter_table->hashTable = hash_table;

	IP_FILTER_LOG_DEBUG("ip_filter_hash_init OK, nodeCnt:%d! \n", ip_cnt);
	return IP_FILTER_OK;
}

/*********************************************************************************
* 函数名称：ip_filter_destroy
* 功能描述：销毁ip filter 表
* 输入：
*      filter_table: 初始化成功的filter表
* 输出：
*      无
* 返回值：成功返回IP_FILTER_OK，失败返回IP_FILTER_ERR。
*********************************************************************************/
IP_FILTER_RET ip_filter_destroy(IP_FILTER_TABLE* filter_table)
{
	if (NULL == filter_table)
	{
		IP_FILTER_LOG_ERROR("Para Error, filter_table NULL! \n");
		return IP_FILTER_ERR;
	}

	if (NULL != filter_table->hashTable)
	{
		ip_filter_hash_destroy(filter_table->hashTable);
		free(filter_table->hashTable);
		filter_table->hashTable= NULL;
	}

	return IP_FILTER_OK;
}


/*********************************************************************************
* 函数名称：ip_filter_add
* 功能描述：向filter表中增加ip地址
* 输入：
*      ipv4_addr：  待添加的ip地址
*      filter_table: 已初始化成功的filter表
* 输出：
*      无
* 返回值：成功返回IP_FILTER_OK，失败返回IP_FILTER_ERR。
*********************************************************************************/
IP_FILTER_RET ip_filter_add(int32_t ipv4_addr, IP_FILTER_TABLE* filter_table)
{
	if (NULL == filter_table)
	{
		IP_FILTER_LOG_ERROR("Para Error, filter_table NULL! \n");
		return IP_FILTER_ERR;
	}

	int ret = ip_filter_hash_add(ipv4_addr, filter_table->hashTable);
	if (-1 == ret)
	{
		return IP_FILTER_ERR;
	}
	else if (1 == ret)
	{
		//fix-me ? 如何处理，已存在
	}

	filter_table->currCnt++;
	return IP_FILTER_OK;
}


/*********************************************************************************
* 函数名称：ip_filter_del
* 功能描述：向filter表中删除ip地址
* 输入：
*      ipv4_addr：  待删除的ip地址
*      filter_table: 已初始化成功的filter表
* 输出：
*      无
* 返回值：删除成功返回IP_FILTER_OK，删除失败返回IP_FILTER_ERR。
*********************************************************************************/
IP_FILTER_RET ip_filter_del(int32_t ipv4_addr, IP_FILTER_TABLE* filter_table)
{
	if (NULL == filter_table)
	{
		IP_FILTER_LOG_ERROR("Para Error, filter_table NULL! \n");
		return IP_FILTER_ERR;
	}

	int ret = ip_filter_hash_del(ipv4_addr, filter_table->hashTable);
	if (0 != ret)
	{
		return IP_FILTER_ERR;
	}

	filter_table->currCnt--;
	return IP_FILTER_OK;
}


/*********************************************************************************
* 函数名称：ip_filter_match
* 功能描述：向filter表中匹配ip地址
* 输入：
*      ipv4_addr：  待匹配的ip地址
*      filter_table: 已初始化成功的filter表
* 输出：
*      无
* 返回值：匹配成功返回IP_FILTER_OK，匹配失败返回IP_FILTER_ERR。
*********************************************************************************/
IP_FILTER_RET ip_filter_match(int32_t ipv4_addr, IP_FILTER_TABLE* filter_table)
{
	if (NULL == filter_table)
	{
		IP_FILTER_LOG_ERROR("Para Error, filter_table NULL! \n");
		return IP_FILTER_ERR;
	}

	if (NULL == ip_filter_hash_find(ipv4_addr, filter_table->hashTable))
	{
		return IP_FILTER_ERR;
	}

	return IP_FILTER_OK;
}


//获取原始报文中的目的ip，目前只处理标准以太帧，带封装的暂不处理
// dmac - smac - 0800 - iphead - ipdata
#define IP_FILTER_INVALID_IPV4 0x0
#define IP_FILTER_MIN_PKT_LEN  34 // 12 + 2 + 20 最小报文长度
int32_t pkt_dip_get(uint8_t* pkt, uint16_t len)
{
	if (len < IP_FILTER_MIN_PKT_LEN)
		return IP_FILTER_INVALID_IPV4;

	//ethernet head check
    int16_t* ethType = (int16_t*)(pkt + 12);
	if (ntohs(*ethType) != 0x0800)  //确保是ipv4类型
		return IP_FILTER_INVALID_IPV4;
	
	//ip head
	uint8_t ipFirstByte = *(pkt + 14);
	if (0b0100 != ipFirstByte >> 4) //确保是ipv4类型
		return IP_FILTER_INVALID_IPV4;

	uint8_t ipHeadLen = ipFirstByte && 0x0f;
	if (14 + ipHeadLen > len)
		return IP_FILTER_INVALID_IPV4;

	//获取目的IP
	int32_t* pdip = (int32_t *)(pkt + 30);
	return ntohl(*pdip);
}

/*********************************************************************************
* 函数名称：pkt_ip_match
* 功能描述：对一个原始报文进行ip地址
* 输入：
*      pkt：  报文指针
*      len：  报文长度
*      filter_table：  已初始化成功的filter表
* 输出：
*      无
* 返回值：成功返回IP_FILTER_OK，失败返回IP_FILTER_ERR。
*********************************************************************************/
IP_FILTER_RET pkt_ip_match(uint8_t* pkt, uint16_t len, int32_t* ipv4, IP_FILTER_TABLE* filter_table)
{
	//获取目的IP
    int32_t dip = pkt_dip_get(pkt, len);
	if (IP_FILTER_INVALID_IPV4 == dip)
	{
		//报文解析出错
		//fix-me ? 
		//是否需要日志输出，这里可能出现大量错误
		//是否需要上送
		return IP_FILTER_ERR;
	}
	
	//对目的IP进行匹配
    if (IP_FILTER_OK != ip_filter_match(dip, filter_table))
		return IP_FILTER_ERR;

	*ipv4 = dip;
	return IP_FILTER_OK;
}


