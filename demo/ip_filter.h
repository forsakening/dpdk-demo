#ifndef __IP_FILTER_H__
#define __IP_FILTER_H__

#include <stdint.h>

typedef enum
{
    IP_FILTER_OK = 0,     //返回成功
	IP_FILTER_ERR = -1,   //返回失败
}IP_FILTER_RET;

typedef struct
{
	uint32_t capacity;    //可容纳的ip数目
	uint32_t currCnt;     //当前表中ip数目
	void*    hashTable;   //指向filter表中hash表
}IP_FILTER_TABLE;

/*********************************************************************************
* 函数名称：ip_filter_init
* 功能描述：初始化ip filter 表
* 输入：
*      ip_cnt：  filter表可容纳的ip数目
* 输出：
*      filter_table: 初始化成功的filter表
* 返回值：成功返回IP_FILTER_OK，失败返回IP_FILTER_ERR。
*********************************************************************************/
IP_FILTER_RET ip_filter_init(uint32_t ip_cnt, IP_FILTER_TABLE* filter_table);


/*********************************************************************************
* 函数名称：ip_filter_destroy
* 功能描述：销毁ip filter 表
* 输入：
*      filter_table: 初始化成功的filter表
* 输出：
*      无
* 返回值：成功返回IP_FILTER_OK，失败返回IP_FILTER_ERR。
*********************************************************************************/
IP_FILTER_RET ip_filter_destroy(IP_FILTER_TABLE* filter_table);


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
IP_FILTER_RET ip_filter_add(int32_t ipv4_addr, IP_FILTER_TABLE* filter_table);


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
IP_FILTER_RET ip_filter_del(int32_t ipv4_addr, IP_FILTER_TABLE* filter_table);


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
IP_FILTER_RET ip_filter_match(int32_t ipv4_addr, IP_FILTER_TABLE* filter_table);


/*********************************************************************************
* 函数名称：pkt_ip_match
* 功能描述：对一个原始报文进行ip地址,支持多线程调用
* 输入：
*      pkt：  报文指针
*      len：  报文长度
*      filter_table：  已初始化成功的filter表
* 输出：
*      无
* 返回值：成功返回IP_FILTER_OK，失败返回IP_FILTER_ERR。
*********************************************************************************/
IP_FILTER_RET pkt_ip_match(uint8_t* pkt, uint16_t len, int32_t* ipv4, IP_FILTER_TABLE* filter_table);

#endif
