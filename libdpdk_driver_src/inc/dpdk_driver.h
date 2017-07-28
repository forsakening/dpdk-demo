#ifndef _DPDK_DRIVER_H_
#define _DPDK_DRIVER_H_
#include <stdint.h>

#define DPDK_MAX_PORT_NUM 32           /*dpdk收发接口支持的最大端口数*/
#define DPDK_MAX_CORE_NUM 64           /*dpdk收发接口可启用的最大cpu核数*/
#define DPDK_MAX_APP_THREAD_NUM 64     /*上层应用可启用的最大报文处理线程数*/
#define DPDK_MAX_QUE_NUM  32           /*每个网卡可以使用的最大队列数目*/


#define __dpdk_cache_aligned __attribute__((__aligned__(64)))



/* 网卡端口状态统计 */
struct dpdk_nic_port_statistics {
	uint64_t rx_packets;        /*网卡端口收包个数*/
	uint64_t rx_bytes;          /*网卡端口收包字节数*/
	uint64_t tx_packets;        /*网卡端口发包个数*/
	uint64_t tx_bytes;          /*网卡端口发包字节数*/
	uint64_t imissed_packets;   /*网卡端口由硬件队列满导致的丢包个数*/
	uint64_t ierrors_packets;   /*网卡端口接受错误包个数*/
	uint64_t oerrors_packets;   /*网卡端口发送错误包个数*/
	uint64_t rx_nombuf;         /*网卡端口报文缓存分配失败次数*/
} __dpdk_cache_aligned;


/* 业务线程收发包状态统计 */
struct dpdk_app_thread_statistics {
	uint64_t rcv_packets;        /*业务线程收包个数*/
	uint64_t snd_packets;        /*业务线程发包个数*/
	uint64_t drp_packets;        /*业务线程丢弃个数*/
	uint64_t get_buffers;        /*业务线程申请报文缓存成功次数*/
	uint64_t get_nobuf;          /*业务线程申请报文缓存失败次数*/
} __dpdk_cache_aligned;



/*dpdk收发接口初始化信息结构*/
struct dpdk_init_para{
    uint8_t  port_num;                                  /*启用端口个数*/
	uint8_t  core_num;                                  /*启用CPU个数*/
	uint8_t  core_arr[DPDK_MAX_CORE_NUM];               /*cpu核心信息数组*/
}__dpdk_cache_aligned;


struct dpdk_port_conf{
	uint8_t  mode;									
	uint8_t  mac_addr[6];								/*端口mac信息*/
	uint8_t  rx_que_num;								/*端口使用的收队列数目，和其对应的上层应用线程数目相等*/
    uint8_t  tx_que_num;                                /*端口使用的发队列数目，和其对应的上层应用线程数目相等*/	
	uint8_t  rx_core_num;								/*端口使用几个cpu核心进行收包*/
	uint8_t  rx_core_arr[DPDK_MAX_CORE_NUM];			/*cpu核心信息数组*/
	uint8_t  tx_core_num;								/*端口使用几个cpu核心进行发包*/
	uint8_t  tx_core_arr[DPDK_MAX_CORE_NUM];			/*cpu核心信息数组*/
	uint32_t cache_pkt_num;								/*端口可以缓存报文的个数*/
	void*	 mbuf_pool[DPDK_MAX_QUE_NUM];				/*用于保存其收包内存池*/
	void*    r_ring[DPDK_MAX_QUE_NUM];					/*用于将收到的报文MBUF指针存在ring中，各个队列一个*/
	void*    t_ring[DPDK_MAX_QUE_NUM];					/*发包队列*/
}__dpdk_cache_aligned;

/*报文描述信息结构*/
struct dpdk_pkt_info {
    struct timeval time_stamp;              /*时间戳*/
    uint8_t       *pkt_data;		        /*mac层指针*/
    uint16_t       pkt_len;                 /*长度*/
    uint8_t        port_in;                 /*入端口*/
    uint8_t        port_out;                /*出端口*/
    uint8_t        core_id;                 /*处理核心*/
    uint8_t        drop_flag;               /*丢弃标志*/
    void           *pmbuf;                  /*mbuf指针*/
}__dpdk_cache_aligned;


/*********************************************************************************
* 函数名称：dpdk_nic_init
* 功能描述：dpdk收发包接口初始化，分配所需资源。
* 输入：
*      init_para：收发包接口初始化参数。
* 输出：无
* 返回值：成功返回0，失败返回-1。
*********************************************************************************/
int dpdk_nic_init(const char* file_path, struct dpdk_init_para* init_para);

/*********************************************************************************
* 函数名称：dpdk_recv_pkt
* 功能描述：收包接口。
* 输入：
*      thread_id：  线程序号。
* 输出：
*      recv_pkt： 报文描述信息指针。
* 返回值：成功返回0，失败返回-1。
*********************************************************************************/
int	dpdk_recv_pkt(int port_id, int que_id,  struct dpdk_pkt_info **recv_pkt);


/*********************************************************************************
* 函数名称：dpdk_send_pkt
* 功能描述：发包接口。
* 输入：
*      thread_id：  线程序号。
*	   port_id:    发送端口。
*      send_pkt： 报文描述信息指针数。
* 输出：无
* 返回值：无
*********************************************************************************/
int dpdk_send_pkt(int port_id, int que_id, struct dpdk_pkt_info *send_pkt);


/*********************************************************************************
* 函数名称：dpdk_drop_pkt
* 功能描述：释放报文。
* 输入：
*      thread_id：  线程序号。
*	   ppkt： 报文描述信息指针。
* 输出：无
* 返回值：无
*********************************************************************************/
void dpdk_drop_pkt(struct dpdk_pkt_info *ppkt);

/*********************************************************************************
* 函数名称：dpdk_get_sendbuf
* 功能描述：获取报文缓存接口。
* 输入：
*      thread_id：  线程序号。
* 输出：
*      ppkt： 报文描述信息指针。
* 返回值：成功返回0，失败返回-1。
*********************************************************************************/
int dpdk_get_sendbuf(struct dpdk_pkt_info **ppkt);


/*********************************************************************************
* 函数名称：dpdk_get_nic_port_statistics
* 功能描述：获取网卡的指定端口的报文统计
* 输入：    port_id： 端口号
* 输出：    nic_port_stats: 统计值结构体
* 返回值：  无
*********************************************************************************/
void dpdk_get_nic_port_statistics(int port_id, struct dpdk_nic_port_statistics *nic_port_stats);


/*********************************************************************************
* 函数名称：dpdk_app_thread_statistics
* 功能描述：获取指定业务线程的报文统计
*           thread_id：  线程序号。
* 输出：    app_thread_stats: 统计值结构体
* 返回值：  无
*********************************************************************************/
//暂未实现
void dpdk_get_app_thread_statistics(int thread_id, struct dpdk_app_thread_statistics *app_thread_stats);


/*********************************************************************************
* 函数名称：dpdk_print_memory
* 功能描述：统计当前内存池的使用情况
* 输出：    无
* 返回值：  无
*********************************************************************************/
void dpdk_print_memory(void);

#endif 
/* _DPDK_DRIVER_H_ */

