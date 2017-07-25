#ifndef __IP_FILTER_H__
#define __IP_FILTER_H__

#include <stdint.h>

typedef enum
{
    IP_FILTER_OK = 0,     //���سɹ�
	IP_FILTER_ERR = -1,   //����ʧ��
}IP_FILTER_RET;

typedef struct
{
	uint32_t capacity;    //�����ɵ�ip��Ŀ
	uint32_t currCnt;     //��ǰ����ip��Ŀ
	void*    hashTable;   //ָ��filter����hash��
}IP_FILTER_TABLE;

/*********************************************************************************
* �������ƣ�ip_filter_init
* ������������ʼ��ip filter ��
* ���룺
*      ip_cnt��  filter������ɵ�ip��Ŀ
* �����
*      filter_table: ��ʼ���ɹ���filter��
* ����ֵ���ɹ�����IP_FILTER_OK��ʧ�ܷ���IP_FILTER_ERR��
*********************************************************************************/
IP_FILTER_RET ip_filter_init(uint32_t ip_cnt, IP_FILTER_TABLE* filter_table);


/*********************************************************************************
* �������ƣ�ip_filter_destroy
* ��������������ip filter ��
* ���룺
*      filter_table: ��ʼ���ɹ���filter��
* �����
*      ��
* ����ֵ���ɹ�����IP_FILTER_OK��ʧ�ܷ���IP_FILTER_ERR��
*********************************************************************************/
IP_FILTER_RET ip_filter_destroy(IP_FILTER_TABLE* filter_table);


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
IP_FILTER_RET ip_filter_add(int32_t ipv4_addr, IP_FILTER_TABLE* filter_table);


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
IP_FILTER_RET ip_filter_del(int32_t ipv4_addr, IP_FILTER_TABLE* filter_table);


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
IP_FILTER_RET ip_filter_match(int32_t ipv4_addr, IP_FILTER_TABLE* filter_table);


/*********************************************************************************
* �������ƣ�pkt_ip_match
* ������������һ��ԭʼ���Ľ���ip��ַ,֧�ֶ��̵߳���
* ���룺
*      pkt��  ����ָ��
*      len��  ���ĳ���
*      filter_table��  �ѳ�ʼ���ɹ���filter��
* �����
*      ��
* ����ֵ���ɹ�����IP_FILTER_OK��ʧ�ܷ���IP_FILTER_ERR��
*********************************************************************************/
IP_FILTER_RET pkt_ip_match(uint8_t* pkt, uint16_t len, int32_t* ipv4, IP_FILTER_TABLE* filter_table);

#endif
