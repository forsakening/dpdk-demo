//create @20170510 
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <arpa/inet.h>
#include "ip_filter.h"

//ƽ��ÿ��hashͰ�µ���ȣ����ڼ���Ͱ��Ŀ
#define IP_FILTER_HASH_DEPTH_SMALL 1     //���� < 4096
#define IP_FILTER_HASH_DEPTH_MEDIUM 5    //���� < 50000
#define IP_FILTER_HASH_DEPTH_LARGE 24    //���� > 50000

//��ȡĳ��ip��ַ��hash���� num-hash���Ͱ��Ŀ
#define IP_FILTER_HASHIDX(ipv4, num) (((ipv4) ^ (ipv4 >> 4)) % (num))

//FILTERģ���ӡ����
#define IP_FILTER_LOG_DEBUG(fmt,...) printf("[IP_FILTER][DEBUG][%s:%d]"fmt, __FUNCTION__, __LINE__, ##__VA_ARGS__);
#define IP_FILTER_LOG_ERROR(fmt,...) printf("[IP_FILTER][ERROR][%s:%d]"fmt, __FUNCTION__, __LINE__, ##__VA_ARGS__);

//linux x86 ԭ����
#define ATOMIC_BOOL_COM_SWAP(ptr, old_val, new_val) __sync_bool_compare_and_swap((ptr), (old_val), (new_val))

///////////////////////////////////////////////////////////////////////////////////
// �ڲ�hash����
// filter��Ŀǰ��hash�����ƥ��
///////////////////////////////////////////////////////////////////////////////////
//hash����ÿ���ڵ�����ݽṹ��Ŀǰ��˫���������
typedef struct _hash_node_s
{
	int32_t ipv4Addr;
	struct _hash_node_s* next;
	struct _hash_node_s* pre;
}HASH_NODE_S;

//hashͰ���ݽṹ
typedef struct
{
    HASH_NODE_S*   buck_head;   //Ͱͷָ��
    uint32_t       hcur_node;   //��ǰͰ�µĽڵ���
    uint32_t       buck_lock;   //Ͱ��
}HASH_BUCK_S;

//hash���ڲ�ʹ�û��λ��棬��̬�����ͷ� 
typedef struct
{
	uint32_t     nodeCnt;       //���λ����нڵ���Ŀ
	uint32_t     nodeSize;      //���λ�����ÿ���ڵ��С
	uint32_t     head;          //���λ���ͷ
	uint32_t     tail;          //���λ���β��
	uint32_t     lock;          //���λ����ڲ���
	void**       nodeArray;     //���λ���������ƽṹ
	void*        nodeBuf;		//���λ��滺����
}FIFO_CTRL;

//hash�����ݽṹ
typedef struct
{
    uint32_t     nodeNum;       //�ڵ�����
	uint32_t     buckNum;       //��ϣ��Ͱ��
	uint32_t     hcur_node;     //��ǰ�ڵ�����
	FIFO_CTRL*   fifoCtrl;    //���ݻ���
	HASH_BUCK_S* buck;          //Ͱ�����ָ��
}IP_FILTER_HASH_TABLE;

//���ζ���ʵ��
//����һ�黷�λ���
//nodeCnt - ���λ���ش�С
//nodeSize - ���λ�����е���Ԫ�صĴ�С
//ctrl - ���λ�����ƽṹ
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

	//�����ڴ�ָ����array��
	int i = 0;
	for (; i < nodeCnt; i++)
	{
		array[i] = nodeBuf + i * nodeSize;
	}

	//��ʼ��fifo ctrl�����ṹ
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

//֧�ֲ����Ļ��ζ��������ͷŽӿ�
//�μ�DPDK:rte_ring.h��__rte_ring_mp_do_enqueue �ӿڵ�
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
			return -2; // �������

        if (ATOMIC_BOOL_COM_SWAP(&(ctrl->head), tmp, tmp_head)) //CAS�Ƚϣ��൱����
            break;
    }

	*node = ctrl->nodeArray[tmp]; //���ڴ���л�ȡһ��Ԫ�ػ���
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
	        return -2;  //���ζ�������

		if (ATOMIC_BOOL_COM_SWAP(&(ctrl->tail), tmp_tail, tmp))
            break;
	}

	ctrl->nodeArray[tmp] = node;  //�������ڴ����	
	return 0;
}

//Ϊһ��hash���ĳ��Ͱ����
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

//�ͷ�һ��hash���ĳ��Ͱ��
void ip_filter_hash_buck_unlock(int32_t hashIdx, IP_FILTER_HASH_TABLE* hash_table)
{
	hash_table->buck[hashIdx].buck_lock = 0;	
	return ;
}

int ip_filter_hash_init(uint32_t nodeCnt, IP_FILTER_HASH_TABLE* hash_table)
{
    //���ݳ�ʼ���Ľڵ���Ŀ����̬������Ҫ��hashͰ��Ŀ
    uint32_t buckNum = 0;
	if (nodeCnt <= 4096)
		buckNum = nodeCnt / IP_FILTER_HASH_DEPTH_SMALL;
	else if (nodeCnt > 4096 && nodeCnt <= 50000)
	    buckNum = nodeCnt / IP_FILTER_HASH_DEPTH_MEDIUM;
	else
		buckNum = nodeCnt / IP_FILTER_HASH_DEPTH_LARGE;

    //����buck���黺��
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

	//����hash�ڵ㻺��
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

	//��ʼ��hash�ڵ㻺��
	if (0 != ip_filter_fifo_alloc_buf(nodeCnt, sizeof(HASH_NODE_S), fifoCtrl))
	{
		IP_FILTER_LOG_ERROR("Alloc FIFO Ctrl Buf Error, nodeCnt:%u, size:%d! \n", nodeCnt, (int)sizeof(HASH_NODE_S));
		return -1;
	}
	else
	{
		IP_FILTER_LOG_DEBUG("Alloc FIFO Ctrl Buf Ok, nodeCnt:%u, size:%d! \n", nodeCnt, (int)sizeof(HASH_NODE_S));
	}

	//�ɹ�
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

    //��Ͱ
    ip_filter_hash_buck_lock(hashIdx, hash_table);
	
	//�ȱ����µ�ǰhashͰ���Ƿ��д���ӵĽڵ�
	HASH_NODE_S* head = hash_table->buck[hashIdx].buck_head;
	HASH_NODE_S* tmp = head;
	while (NULL != tmp)
	{
		if (tmp->ipv4Addr == ipv4Addr)
		{
            //�ͷ�Ͱ��
			ip_filter_hash_buck_unlock(hashIdx, hash_table);
			return 1; //hash�����Ѿ����ڸýڵ�
		}

		tmp = tmp->next;
	}

	//�����ڱ��У������Ͱ��ͷ�ڵ�
	//����һ��hash�ڵ��ڴ�
	HASH_NODE_S* addNode = NULL;
	int ret = ip_filter_fifo_get_buf((void**)&addNode, hash_table->fifoCtrl);
	if (0 != ret || NULL == addNode)
	{
		IP_FILTER_LOG_ERROR("Get Buf from FIFO Err, ipv4:%d, ret=%d !\n", ipv4Addr, ret);
		//�ͷ�Ͱ��
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

	//�ͷ�Ͱ��
	ip_filter_hash_buck_unlock(hashIdx, hash_table);
	return 0;  //��ӳɹ�
}

int ip_filter_hash_del(int32_t ipv4Addr, IP_FILTER_HASH_TABLE* hash_table)
{
	if (NULL == hash_table)
	{
		IP_FILTER_LOG_ERROR("Para Error, hash_table NULL! \n");
		return -1;
	}

	//����hash����
	int32_t hashIdx = IP_FILTER_HASHIDX(ipv4Addr, hash_table->buckNum);

	//��Ͱ
	ip_filter_hash_buck_lock(hashIdx, hash_table);
	
	//�ȱ����µ�ǰhashͰ���Ƿ��д���ӵĽڵ�
	HASH_NODE_S* tmp = hash_table->buck[hashIdx].buck_head;
	while (NULL != tmp)
	{
	    //�ҵ��ýڵ�
		if (tmp->ipv4Addr == ipv4Addr)
			break;

		tmp = tmp->next;
	}

    if (NULL == tmp)
   	{
   	    //�ͷ�Ͱ��
	    ip_filter_hash_buck_unlock(hashIdx, hash_table);
        IP_FILTER_LOG_ERROR("ipv4 %d not in filter table!\n", ipv4Addr);
		return -2; //��ɾ���Ľڵ㲻�ڱ��У��쳣!!
	}

	//���ڱ��У�����ɾ��
    HASH_NODE_S* pre = tmp->pre;
	HASH_NODE_S* next = tmp->next;
	if (NULL != pre)
		pre->next = next;
	else
	{
		//ɾ�����Ǳ�ͷ
		hash_table->buck[hashIdx].buck_head = next;
	}

	if (NULL != next)
		next->pre = pre;

    //�ͷ�����ڵ���ڴ�
    ip_filter_fifo_ret_buf(tmp, hash_table->fifoCtrl);

	//�ͷ�Ͱ��
	ip_filter_hash_buck_unlock(hashIdx, hash_table);

	return 0; //ɾ���ɹ�
}

HASH_NODE_S* ip_filter_hash_find(int32_t ipv4Addr, IP_FILTER_HASH_TABLE* hash_table)
{
	if (NULL == hash_table)
	{
		IP_FILTER_LOG_ERROR("Para Error, hash_table NULL! \n");
		return NULL;
	}

	//����hash����
	int32_t hashIdx = IP_FILTER_HASHIDX(ipv4Addr, hash_table->buckNum);

	//��Ͱ
	ip_filter_hash_buck_lock(hashIdx, hash_table);
	
	//�ȱ����µ�ǰhashͰ���Ƿ��д���ӵĽڵ�
	HASH_NODE_S* tmp = hash_table->buck[hashIdx].buck_head;
	while (NULL != tmp)
	{
		//�ҵ��ýڵ�
		if (tmp->ipv4Addr == ipv4Addr)
			break;

		tmp = tmp->next;
	}

	//�ͷ�Ͱ��
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
* �������ƣ�ip_filter_init
* ������������ʼ��ip filter ��
* ���룺
*      ip_cnt��  filter������ɵ�ip��Ŀ
* �����
*      filter_table: ��ʼ���ɹ���filter��
* ����ֵ���ɹ�����IP_FILTER_OK��ʧ�ܷ���IP_FILTER_ERR��
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
* �������ƣ�ip_filter_destroy
* ��������������ip filter ��
* ���룺
*      filter_table: ��ʼ���ɹ���filter��
* �����
*      ��
* ����ֵ���ɹ�����IP_FILTER_OK��ʧ�ܷ���IP_FILTER_ERR��
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
* �������ƣ�ip_filter_add
* ������������filter��������ip��ַ
* ���룺
*      ipv4_addr��  ����ӵ�ip��ַ
*      filter_table: �ѳ�ʼ���ɹ���filter��
* �����
*      ��
* ����ֵ���ɹ�����IP_FILTER_OK��ʧ�ܷ���IP_FILTER_ERR��
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
		//fix-me ? ��δ����Ѵ���
	}

	filter_table->currCnt++;
	return IP_FILTER_OK;
}


/*********************************************************************************
* �������ƣ�ip_filter_del
* ������������filter����ɾ��ip��ַ
* ���룺
*      ipv4_addr��  ��ɾ����ip��ַ
*      filter_table: �ѳ�ʼ���ɹ���filter��
* �����
*      ��
* ����ֵ��ɾ���ɹ�����IP_FILTER_OK��ɾ��ʧ�ܷ���IP_FILTER_ERR��
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
* �������ƣ�ip_filter_match
* ������������filter����ƥ��ip��ַ
* ���룺
*      ipv4_addr��  ��ƥ���ip��ַ
*      filter_table: �ѳ�ʼ���ɹ���filter��
* �����
*      ��
* ����ֵ��ƥ��ɹ�����IP_FILTER_OK��ƥ��ʧ�ܷ���IP_FILTER_ERR��
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


//��ȡԭʼ�����е�Ŀ��ip��Ŀǰֻ�����׼��̫֡������װ���ݲ�����
// dmac - smac - 0800 - iphead - ipdata
#define IP_FILTER_INVALID_IPV4 0x0
#define IP_FILTER_MIN_PKT_LEN  34 // 12 + 2 + 20 ��С���ĳ���
int32_t pkt_dip_get(uint8_t* pkt, uint16_t len)
{
	if (len < IP_FILTER_MIN_PKT_LEN)
		return IP_FILTER_INVALID_IPV4;

	//ethernet head check
    int16_t* ethType = (int16_t*)(pkt + 12);
	if (ntohs(*ethType) != 0x0800)  //ȷ����ipv4����
		return IP_FILTER_INVALID_IPV4;
	
	//ip head
	uint8_t ipFirstByte = *(pkt + 14);
	if (0b0100 != ipFirstByte >> 4) //ȷ����ipv4����
		return IP_FILTER_INVALID_IPV4;

	uint8_t ipHeadLen = ipFirstByte && 0x0f;
	if (14 + ipHeadLen > len)
		return IP_FILTER_INVALID_IPV4;

	//��ȡĿ��IP
	int32_t* pdip = (int32_t *)(pkt + 30);
	return ntohl(*pdip);
}

/*********************************************************************************
* �������ƣ�pkt_ip_match
* ������������һ��ԭʼ���Ľ���ip��ַ
* ���룺
*      pkt��  ����ָ��
*      len��  ���ĳ���
*      filter_table��  �ѳ�ʼ���ɹ���filter��
* �����
*      ��
* ����ֵ���ɹ�����IP_FILTER_OK��ʧ�ܷ���IP_FILTER_ERR��
*********************************************************************************/
IP_FILTER_RET pkt_ip_match(uint8_t* pkt, uint16_t len, int32_t* ipv4, IP_FILTER_TABLE* filter_table)
{
	//��ȡĿ��IP
    int32_t dip = pkt_dip_get(pkt, len);
	if (IP_FILTER_INVALID_IPV4 == dip)
	{
		//���Ľ�������
		//fix-me ? 
		//�Ƿ���Ҫ��־�����������ܳ��ִ�������
		//�Ƿ���Ҫ����
		return IP_FILTER_ERR;
	}
	
	//��Ŀ��IP����ƥ��
    if (IP_FILTER_OK != ip_filter_match(dip, filter_table))
		return IP_FILTER_ERR;

	*ipv4 = dip;
	return IP_FILTER_OK;
}


