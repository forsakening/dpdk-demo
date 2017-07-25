#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include <inttypes.h>
#include <sys/types.h>
#include <sys/queue.h>
#include <netinet/in.h>
#include <setjmp.h>
#include <stdarg.h>
#include <ctype.h>
#include <errno.h>
#include <getopt.h>
#include <signal.h>
#include <stdbool.h>

#include <rte_common.h>
#include <rte_log.h>
#include <rte_malloc.h>
#include <rte_memory.h>
#include <rte_memcpy.h>
#include <rte_memzone.h>
#include <rte_eal.h>
#include <rte_per_lcore.h>
#include <rte_launch.h>
#include <rte_atomic.h>
#include <rte_cycles.h>
#include <rte_prefetch.h>
#include <rte_lcore.h>
#include <rte_per_lcore.h>
#include <rte_branch_prediction.h>
#include <rte_interrupts.h>
#include <rte_pci.h>
#include <rte_random.h>
#include <rte_debug.h>
#include <rte_ether.h>
#include <rte_ethdev.h>
#include <rte_mempool.h>
#include <rte_mbuf.h>
#include <sys/time.h>
#include <unistd.h>

#include "dpdk_driver.h"


#define DPDK_R_OK 0
#define DPDK_R_ERR -1

#define DPDK_INIT_TIMEOUT 60 //60s

#define MAX_PKT_BURST 32

#define MEMPOOL_CACHE_SIZE 256

#define RTE_TEST_RX_DESC_DEFAULT 128
#define RTE_TEST_TX_DESC_DEFAULT 512

#define DPDK_ETHER_LEN 1600

#define BURST_TX_DRAIN_US 100 /* TX drain every ~100us */

#define CHECK_INTERVAL 100 /* 100ms */
#define MAX_CHECK_TIME 90 /* 9s (90 * 100ms) in total */
#define SEND_TIMES_OUT 100 /*100次*/

#define ALLOC_PORT_IN 0xff      /*动态申请报文缓存的入端口*/
#define ALLOC_DEFAULT_CORE 0    /*动态申请报文缓存的处理核心*/

/*报文转发丢弃标志*/
enum DPDK_PKT_SEND_FLAG
{
    DPDK_ACCEPT,        /*发送标志*/
    DPDK_DROP,          /*丢弃标志*/
};


/*驱动初始化状态标志*/
enum DPDK_INIT_FLAG
{
    DPDK_IN_INIT,        /*初始化中*/
    DPDK_INIT_OK,        /*初始化成功*/
    DPDK_INIT_ERROR,     /*初始化失败*/
};

struct lcore_port_conf {
	uint8_t n_rx_port;
	uint8_t rx_port_list[DPDK_MAX_PORT_NUM];
} __rte_cache_aligned;


static uint8_t rss_sym_key[40] = {
	0x6D, 0x5A, 0x6D, 0x5A, 0x6D, 0x5A, 0x6D, 0x5A,
	0x6D, 0x5A, 0x6D, 0x5A, 0x6D, 0x5A, 0x6D, 0x5A,
	0x6D, 0x5A, 0x6D, 0x5A, 0x6D, 0x5A, 0x6D, 0x5A,
	0x6D, 0x5A, 0x6D, 0x5A, 0x6D, 0x5A, 0x6D, 0x5A,
	0x6D, 0x5A, 0x6D, 0x5A, 0x6D, 0x5A, 0x6D, 0x5A,
};

static struct rte_eth_conf port_conf = {
	.rxmode = {
		.mq_mode = ETH_MQ_RX_RSS,
		.max_rx_pkt_len = ETHER_MAX_LEN,
		.split_hdr_size = 0,
		.header_split   = 0, /**< Header Split disabled */
		.hw_ip_checksum = 0, /**< IP checksum offload enabled */
		.hw_vlan_filter = 0, /**< VLAN filtering disabled */
		.jumbo_frame    = 0, /**< Jumbo Frame Support disabled */
		.hw_strip_crc   = 0, /**< CRC stripped by hardware */
	},
	.rx_adv_conf = {
		.rss_conf = {
			.rss_key = rss_sym_key,
            .rss_key_len = 40,			
            .rss_hf = ETH_RSS_IP | ETH_RSS_UDP |
                ETH_RSS_TCP | ETH_RSS_SCTP | ETH_RSS_TUNNEL,
		},
	},
	.txmode = {
		.mq_mode = ETH_MQ_TX_NONE,
	},
};

static uint32_t enabled_core_mask = 0;

static uint16_t nb_rxd = RTE_TEST_RX_DESC_DEFAULT;
static uint16_t nb_txd = RTE_TEST_TX_DESC_DEFAULT;

static uint8_t dpdk_user_core_id_map[DPDK_MAX_CORE_NUM] = {0};

static uint8_t user_dpdk_port_id_map[DPDK_MAX_PORT_NUM] = {0};
static uint8_t dpdk_user_port_id_map[DPDK_MAX_PORT_NUM] = {0};

struct rte_mempool * pktmbuf_pool[DPDK_MAX_CORE_NUM] = {NULL};

static struct lcore_port_conf lcore_port_conf[DPDK_MAX_CORE_NUM] = {{0}};

static uint8_t dpdk_init_res_flag = DPDK_IN_INIT;

struct rte_ring  *pkt_recv_ring[DPDK_MAX_APP_THREAD_NUM]={NULL};
struct rte_ring  *pkt_send_ring[DPDK_MAX_CORE_NUM][DPDK_MAX_APP_THREAD_NUM]={{NULL}};

static struct rte_eth_dev_tx_buffer *tx_buffer[DPDK_MAX_CORE_NUM][DPDK_MAX_PORT_NUM] = {{NULL}};

static struct dpdk_init_para dpdk_init_cfg = {0};

static struct ether_addr g_ports_eth_addr[DPDK_MAX_PORT_NUM] = {{{0}}};

static struct dpdk_app_thread_statistics  app_thread_statistics[DPDK_MAX_APP_THREAD_NUM] = {{0}};







static int dpdk_config_validate(struct dpdk_init_para *init_para);

static void check_all_ports_link_status(void);

static int dpdk_env_setup(void);

static int dpdk_core_setup(void);

static int dpdk_port_setup(void);

static int dpdk_ring_setup(void);

static void dpdk_eth_tx_buffer_flush(uint8_t port_id, uint8_t queue_id,
		struct rte_eth_dev_tx_buffer *buffer);
		
static inline void __attribute__((always_inline))
dpdk_eth_tx_buffer(uint8_t port_id, uint8_t queue_id,
		struct rte_eth_dev_tx_buffer *buffer, struct rte_mbuf *tx_pkt);
		
static inline void dpdk_simple_forward(struct rte_mbuf *m, uint8_t port_id, uint8_t que_id);

static void fwd_main_loop(void);

static int fwd_launch_one_lcore(__attribute__((unused)) void *dummy);

static void* dpdk_init_thread(void *arg);




/*
*初始化参数合法性校验
*/
static int dpdk_config_validate(struct dpdk_init_para *init_para)
{
    int i = 0;
    int j = 0;
    
    if(NULL == init_para)
    {
        printf("init_para can't be NULL!\n");
        return DPDK_R_ERR;
    }

    if(DPDK_MAX_CORE_NUM < init_para->core_num || 0 == init_para->core_num
        || DPDK_MAX_PORT_NUM < init_para->port_num|| 0 == init_para->port_num
        || DPDK_MAX_APP_THREAD_NUM < init_para->thread_num || 0 == init_para->thread_num
        || 0 == init_para->cache_pkt_num)
    {
        printf("init_para core_num=%u, port_num=%u, thread_num=%u, or cache_pkt_num=%"PRIu64" illegal!\n", 
            init_para->core_num, init_para->port_num, init_para->thread_num,init_para->cache_pkt_num);
        return DPDK_R_ERR;
    }

    for(i = 0; i < init_para->core_num; i++)
    {
        if(DPDK_MAX_CORE_NUM <= init_para->core_arr[i])
        {
            printf("init_para core_arr[%d]=%d illegal!\n",i,init_para->core_arr[i]);
            return DPDK_R_ERR;
        }
    }


    /*
    *核心个数必须小于等于端口个数
    */
    if(init_para->core_num > init_para->port_num)
    {
        printf("core_num %d cant not be more than port_num %d!\n",
            init_para->core_num, init_para->port_num);
        return DPDK_R_ERR;
    }

    /*
    *cpu核心重复配置校验
    */
    for(i = 0; i < init_para->core_num; i++)
    {
        for(j = i+1; j < init_para->core_num; j++)
        {
            if(init_para->core_arr[i] == init_para->core_arr[j])
            {
                printf("core %d and core %d have the same physical core id %u\n",
                    i, j, init_para->core_arr[i]);
                return DPDK_R_ERR;
            }
        }    
    }

    /*
    *端口重复配置校验
    */
    for(i = 0; i < init_para->port_num; i++)
    {
        for(j = i+1; j < init_para->port_num; j++)
        {
            if(0 == memcmp(init_para->port_arr[i], init_para->port_arr[j], 6))
            {
                printf("port %d and port %d have the same port mac %02x:%02x:%02x:%02x:%02x:%02x\n",
                    i,j,init_para->port_arr[i][0],init_para->port_arr[i][1],init_para->port_arr[i][2],
                    init_para->port_arr[i][3],init_para->port_arr[i][4],init_para->port_arr[i][5]);
                return DPDK_R_ERR;
            }
        }    
    }

    return DPDK_R_OK;    
}




/* Check the link status of all ports in up to 9s, and print them finally */
static void check_all_ports_link_status(void)
{
	uint8_t user_port_id, count, all_ports_up, print_flag = 0;
	struct rte_eth_link link;

	printf("\nChecking link status");
	fflush(stdout);
	for (count = 0; count <= MAX_CHECK_TIME; count++) 
	{
		all_ports_up = 1;
		for (user_port_id = 0; user_port_id < dpdk_init_cfg.port_num; user_port_id++) 
		{				
			memset(&link, 0, sizeof(link));
			rte_eth_link_get_nowait(user_dpdk_port_id_map[user_port_id], &link);
			/* print link status if flag set */
			if (print_flag == 1) 
			{
				if (link.link_status)
					printf("Port %d Link Up - speed %u "
						"Mbps - %s\n", (uint8_t)user_port_id,
						(unsigned)link.link_speed,
				(link.link_duplex == ETH_LINK_FULL_DUPLEX) ?
					("full-duplex") : ("half-duplex\n"));
				else
					printf("Port %d Link Down\n",
						(uint8_t)user_port_id);
				continue;
			}
			/* clear all_ports_up flag if any link down */
			if (link.link_status == ETH_LINK_DOWN) 
			{
				all_ports_up = 0;
				break;
			}
		}
		/* after finally printing all link status, get out */
		if (print_flag == 1)
			break;

		if (all_ports_up == 0) 
		{
			printf(".");
			fflush(stdout);
			rte_delay_ms(CHECK_INTERVAL);
		}

		/* set the print_flag if all ports up or timeout */
		if (all_ports_up == 1 || count == (MAX_CHECK_TIME - 1)) 
		{
			print_flag = 1;
			printf("done\n");
		}
	}
}








/*
*初始化dpdk环境参数
*/
static int dpdk_env_setup(void)
{
    int      i = 0;    
    int      j = 0;
    int      ret = 0;
    uint8_t  dpdk_core_id;
    uint32_t nb_ports = 0;
    char     core_mask_str[64] = {0};
    struct ether_addr mac = {{0}};


    /*
    *根据cpu核心信息数组计算用于初始化dpdk的核心掩码；
    *建立核物理编号和实际使用序号之间的映射
    */
    for(i = 0; i < dpdk_init_cfg.core_num; i++)
    {
        dpdk_core_id = dpdk_init_cfg.core_arr[i];
        dpdk_user_core_id_map[dpdk_core_id] = i;
        enabled_core_mask |= 1 << dpdk_core_id;
    }
    snprintf(core_mask_str, 64, "0x%x", enabled_core_mask);
    printf("init core mask:%s\n",core_mask_str);

    /*
    *初始化dpdk环境参数
    */
    int    argc = 3;  
    char **argv = (char **) malloc((argc+1)*sizeof(char*));
    if(NULL == argv)
        return DPDK_R_ERR;
        
    argv[0] = strdup("dpdk_driver"); 
    argv[1] = strdup("-c");
    argv[2] = strdup(core_mask_str);
    argv[3] = NULL;

    ret = rte_eal_init(argc, argv);
    if (ret < 0)
    {
        printf("Invalid EAL arguments\n");
        return DPDK_R_ERR;
    }


    /*
    *端口mac信息数组配置的mac地址校验，检查dpdk环境中是否配置了相应端口
    */
    nb_ports = rte_eth_dev_count();
    for(i = 0; i < dpdk_init_cfg.port_num; i++)
    {
        for(j = 0; j < nb_ports; j++)
        {
            rte_eth_macaddr_get(j,&mac);
            if(0 == memcmp(mac.addr_bytes, dpdk_init_cfg.port_arr[i], 6))
            {
                /*
                *建立用户端口编号与dpdk端口编号之间的双向映射关系
                */
                user_dpdk_port_id_map[i] = j;
                dpdk_user_port_id_map[j] = i;
                break;
            }
        }
        
        if(j >= nb_ports)
        {
            printf("port %d[mac: %02x:%02x:%02x:%02x:%02x:%02x] not exist or has not bind to dpdk!\n",
                i,dpdk_init_cfg.port_arr[i][0],dpdk_init_cfg.port_arr[i][1],dpdk_init_cfg.port_arr[i][2],
                dpdk_init_cfg.port_arr[i][3],dpdk_init_cfg.port_arr[i][4],dpdk_init_cfg.port_arr[i][5]);
            return DPDK_R_ERR;
        }
    }

    return DPDK_R_OK;
}


/*
*分配和核心相关的资源
*/
static int dpdk_core_setup(void)
{
    int index = 0;
    int user_port_id = 0;
    int dpdk_port_id = 0;
    int user_core_id = 0;
    int dpdk_core_id = 0;
	int rx_port_per_core = 0;
    char mbuf_pool_name[32] = {0};
	struct lcore_port_conf *pconf = NULL;


    /*
    *内存池按照核心进行分配，一个核心线程操作一块内存池，额外分配一块内存池给用户
    *动态申请使用
    */
    for(index = 0; index < dpdk_init_cfg.core_num + 1; index++)
    {
        snprintf(mbuf_pool_name, 32, "mbuf_pool_%d",index);
    	pktmbuf_pool[index] = rte_pktmbuf_pool_create(mbuf_pool_name, 
    	    dpdk_init_cfg.cache_pkt_num/(dpdk_init_cfg.core_num + 1),
    		MEMPOOL_CACHE_SIZE, 0, RTE_MBUF_DEFAULT_BUF_SIZE,
    		rte_socket_id());
    		
    	if (pktmbuf_pool[index] == NULL)
    	{
    		printf( "Cannot init mbuf pool\n");
    		return DPDK_R_ERR;
        }
    }


    /*
    *初始化各核心所需要维护的端口列表
    */
    if(dpdk_init_cfg.port_num % dpdk_init_cfg.core_num)
    {
        rx_port_per_core = dpdk_init_cfg.port_num / dpdk_init_cfg.core_num + 1;
    }
    else
    {
        rx_port_per_core = dpdk_init_cfg.port_num / dpdk_init_cfg.core_num;
    }

	for (user_port_id = 0; user_port_id < dpdk_init_cfg.port_num; user_port_id++) 
	{
		while (rte_lcore_is_enabled(dpdk_core_id) == 0 ||
		       lcore_port_conf[dpdk_core_id].n_rx_port ==
		       rx_port_per_core) 
		{
			dpdk_core_id++;
			if (dpdk_core_id >= DPDK_MAX_CORE_NUM)
			{
				printf("Not enough cores\n");
				return DPDK_R_ERR;
			}
		}

		if (pconf != &lcore_port_conf[dpdk_core_id])
			/* Assigned a new logical core in the loop above. */
			pconf = &lcore_port_conf[dpdk_core_id];
			
        dpdk_port_id = user_dpdk_port_id_map[user_port_id];
		pconf->rx_port_list[pconf->n_rx_port] = dpdk_port_id;
		pconf->n_rx_port++;

		user_core_id = dpdk_user_core_id_map[dpdk_core_id];
		printf("Lcore %u: RX port %d\n", user_core_id, user_port_id);
	}

	return DPDK_R_OK;

}



/*
*初始化端口相关资源及配置
*/
static int dpdk_port_setup(void)
{
    int i = 0;
    int j = 0;
    int ret = 0;
    int dpdk_core_id = 0;
    int user_core_id = 0;
    int user_port_id = 0;
    int dpdk_port_id = 0;
    char tx_buffer_name[64] = {0};
    
    /*
    *初始化各个端口
    */
    for (user_port_id = 0; user_port_id < dpdk_init_cfg.port_num; user_port_id++) 
    {
        printf("Initializing port %d...\n", user_port_id);
        
        dpdk_port_id = user_dpdk_port_id_map[user_port_id];

        /*
        *端口启用硬件收队列个数等于上层应用线程个数，启用硬件发送队列个数等于启用核心个数
        */
        ret = rte_eth_dev_configure(dpdk_port_id, dpdk_init_cfg.thread_num, 
            dpdk_init_cfg.core_num, &port_conf);
        if (ret < 0)
            printf("Cannot configure device: err=%d, port=%d\n", ret, user_port_id);

        rte_eth_macaddr_get(dpdk_port_id,&g_ports_eth_addr[user_port_id]);

        /*
        *查找端口所属核心
        */
        user_core_id = -1;
        for(i=0; i < DPDK_MAX_CORE_NUM; i++)
        {
    		if ((enabled_core_mask & (1 << i)) == 0)
    			continue;        
            for(j=0;j<lcore_port_conf[i].n_rx_port;j++)
            {
                if(dpdk_port_id == lcore_port_conf[i].rx_port_list[j])
                {
                    dpdk_core_id = i;
                    user_core_id = dpdk_user_core_id_map[dpdk_core_id];
                    printf("port %d is processed by core %d\n",user_port_id, user_core_id);
                }
            }       
        }

        if(0 > user_core_id)
        {
            printf("port %d find process core failed!\n",user_port_id); 
            return DPDK_R_ERR;
        }


        /*
        *初始化端口的各个接收队列
        */
        for(i = 0; i < dpdk_init_cfg.thread_num; i++)
        {
            ret = rte_eth_rx_queue_setup(dpdk_port_id, i, nb_rxd,
                             rte_eth_dev_socket_id(dpdk_port_id),
                             NULL,
                             pktmbuf_pool[user_core_id]);
            if (ret < 0)
            {
                printf("rte_eth_rx_queue_setup:err=%d, port=%d queid=%d\n",
                      ret, user_port_id, i);
                return DPDK_R_ERR;
            }
			else
				printf("DPDK-Port:%d using core id:%d ...\n", dpdk_port_id, user_core_id);
        }

        /*
        *初始化端口的各个发送队列
        */
        for(i = 0; i < dpdk_init_cfg.core_num; i++)
        {
            ret = rte_eth_tx_queue_setup(dpdk_port_id, i, nb_txd,
                    rte_eth_dev_socket_id(dpdk_port_id),
                    NULL);
            if (ret < 0)
            {
                printf("rte_eth_tx_queue_setup:err=%d, port=%d queid=%d\n",
                    ret, user_port_id, i);
            }

            /*
            *初始化端口各队列的发送缓存
            */
            snprintf(tx_buffer_name, 64, "tx_buffer_%d_%d",i,user_port_id);
    		tx_buffer[i][user_port_id] = rte_zmalloc_socket(tx_buffer_name,
    				RTE_ETH_TX_BUFFER_SIZE(MAX_PKT_BURST), 0,
    				rte_eth_dev_socket_id(user_port_id));
    				
    		if (tx_buffer[i][user_port_id] == NULL)
    			printf("Cannot allocate buffer for tx on core %d port %d\n",
    					i, user_port_id);

    		rte_eth_tx_buffer_init(tx_buffer[i][user_port_id], MAX_PKT_BURST);
            
        }


       
        /* 开启端口 */
        ret = rte_eth_dev_start(dpdk_port_id);
        if (ret < 0)
        {
            printf("rte_eth_dev_start:err=%d, port=%u\n",
                  ret, user_port_id);
            return DPDK_R_ERR;
        }

        printf("done: \n");

        /* 设置混杂模式 */
        rte_eth_promiscuous_enable(dpdk_port_id);

        printf("Port %d, MAC address: %02X:%02X:%02X:%02X:%02X:%02X\n\n",
                user_port_id,
                g_ports_eth_addr[user_port_id].addr_bytes[0],
                g_ports_eth_addr[user_port_id].addr_bytes[1],
                g_ports_eth_addr[user_port_id].addr_bytes[2],
                g_ports_eth_addr[user_port_id].addr_bytes[3],
                g_ports_eth_addr[user_port_id].addr_bytes[4],
                g_ports_eth_addr[user_port_id].addr_bytes[5]);

    }


    /* 各端口链接状态检测 */
	check_all_ports_link_status();

	return DPDK_R_OK;

}


/*
*初始化队列资源及配置
*/
static int dpdk_ring_setup(void)
{
    int i = 0;
    int j = 0;
    unsigned ring_item_count = 0;
    unsigned ring_cache_max_num = 0;

    char recv_ring_name[64] = {0};
    char send_ring_name[64] = {0};


    /*
    *根据分配的缓存池大小计算ring的大小
    *保证ring中足以存放所有报文
    */
    ring_cache_max_num = dpdk_init_cfg.cache_pkt_num/(dpdk_init_cfg.core_num+1) *
        dpdk_init_cfg.core_num;

    for(i = 0; i < 32; i++)
    {
        if((1 << i) > ring_cache_max_num)
        {
            ring_item_count = (1 << i);
            break;
        }
    }

    if(32 <= i)
    {
        printf("init_para cache_pkt_num=%"PRIu64" illegal!\n",dpdk_init_cfg.cache_pkt_num);
           return DPDK_R_ERR;
    }


    /*
    *创建收包队列,一个用户收包线程对应一个收包队列
    */
    for(i=0; i<dpdk_init_cfg.thread_num; i++)
    {
       snprintf(recv_ring_name, 64, "%s_%d","pkt_recv_ring",i);
       pkt_recv_ring[i] = rte_ring_create(recv_ring_name, ring_item_count, SOCKET_ID_ANY, 0);
       if(NULL == pkt_recv_ring[i])
       {
           printf("rte_ring_create pkt_recv_ring_%d err\n",i);
           return DPDK_R_ERR;
       }
    }


    /*
    *创建发包队列，各个核心拥有自己的一组发包队列
    */
    for(i = 0; i < dpdk_init_cfg.core_num; i++)
    {
        /*
        *创建发包队列，一个用户发包线程对应各核心发包队列组中的一个发包队列，
        *各核心将处理各自发包队列组中的各个队列
        */
       for(j = 0; j < dpdk_init_cfg.thread_num; j++)
       {
           snprintf(send_ring_name, 64, "%s_%d_%d","pkt_send_ring", i, j);
           pkt_send_ring[i][j] = rte_ring_create(send_ring_name, ring_item_count, SOCKET_ID_ANY, 0);
           if(NULL == pkt_send_ring[i][j])
           {
               printf("rte_ring_create pkt_send_ring_%d_%d err\n",i,j);
               return DPDK_R_ERR;
           }
       }
    }

    return DPDK_R_OK;

}

static void dpdk_eth_tx_buffer_flush(uint8_t port_id, uint8_t queue_id,
		struct rte_eth_dev_tx_buffer *buffer)
{
	uint16_t sent;
	uint16_t i;
	uint16_t to_send = buffer->length;
	//uint16_t tx_cnt = 0;

	if (to_send == 0)
		return;

	sent = rte_eth_tx_burst(port_id, queue_id, buffer->pkts, to_send);
	buffer->length = 0;
	
    struct rte_mbuf **cur_pkts_tx = buffer->pkts;
    
    while (unlikely(sent < to_send))
    {
#if 0
        if(unlikely(0 == sent))
        {   
            tx_cnt++;
            if(unlikely(SEND_TIMES_OUT < tx_cnt))
            {
                for(i = 0; i < to_send; i++)
                {
                    rte_pktmbuf_free(cur_pkts_tx[i]);
                }
                break;
            }
        }
#endif        
        to_send = to_send - sent;
        cur_pkts_tx = cur_pkts_tx + sent;

        sent = rte_eth_tx_burst(port_id, queue_id, cur_pkts_tx, to_send);
    }
}

static inline void __attribute__((always_inline))
dpdk_eth_tx_buffer(uint8_t port_id, uint8_t queue_id,
		struct rte_eth_dev_tx_buffer *buffer, struct rte_mbuf *tx_pkt)
{       
	buffer->pkts[buffer->length++] = tx_pkt;
	if (buffer->length < buffer->size)
		return;

	return dpdk_eth_tx_buffer_flush(port_id, queue_id, buffer);
}

static inline void dpdk_simple_forward(struct rte_mbuf *m, uint8_t port_id, uint8_t que_id)
{
	struct rte_eth_dev_tx_buffer *buffer;
	uint8_t dpdk_port_id;


	buffer = tx_buffer[que_id][port_id];
    dpdk_port_id = user_dpdk_port_id_map[port_id];
	dpdk_eth_tx_buffer(dpdk_port_id, que_id, buffer, m);
}

/* main processing loop */
static void fwd_main_loop(void)
{
    int i = 0;
    int j = 0;
    uint8_t que_id = 0;
	uint8_t dpdk_core_id = 0;
	uint8_t user_core_id = 0;
    uint8_t dpdk_port_id = 0;
    uint8_t user_port_id = 0;
    uint16_t ret = 0;
    uint16_t nb_rx = 0;
	uint64_t prev_tsc = 0; 
	uint64_t diff_tsc = 0; 
	uint64_t cur_tsc = 0;
    uint64_t drain_tsc = 0;
    struct timeval tv = {0};
	struct lcore_port_conf *pconf = NULL;
	struct rte_eth_dev_tx_buffer *buffer = NULL;
	struct rte_mbuf *mbuf = NULL;
	struct rte_mbuf *pkts_burst[MAX_PKT_BURST] = {NULL};
	struct dpdk_pkt_info pkt_info = {{0}};
	struct dpdk_pkt_info *pkts_info_burst[MAX_PKT_BURST] = {NULL};
	struct dpdk_pkt_info **cur_pkt_info_burst = NULL;
	
    drain_tsc = (rte_get_tsc_hz() + US_PER_S - 1) / US_PER_S * BURST_TX_DRAIN_US;
	dpdk_core_id = rte_lcore_id();
	pkt_info.core_id = user_core_id = dpdk_user_core_id_map[dpdk_core_id];
	pconf = &lcore_port_conf[dpdk_core_id];

	if (pconf->n_rx_port == 0) 
	{
		printf("lcore %u has nothing to do\n", user_core_id);
		return;
	}

	printf("entering main loop on lcore %u\n", user_core_id);

	for (i = 0; i < pconf->n_rx_port; i++) 
	{
		dpdk_port_id = pconf->rx_port_list[i];
		user_port_id = dpdk_user_port_id_map[dpdk_port_id];
		printf(" -- lcoreid=%u portid=%u\n", user_core_id, user_port_id);
	}

	while (1) 
	{

		/*
		 * TX burst queue drain
		 */
		//cur_tsc = rte_rdtsc();
		//diff_tsc = cur_tsc - prev_tsc;
		//if (unlikely(diff_tsc > drain_tsc)) 
		//{
		//	for (i = 0; i < dpdk_init_cfg.port_num; i++) 
		//	{   
		//		buffer = tx_buffer[user_core_id][i];
		//	    dpdk_port_id = user_dpdk_port_id_map[i];
        //        dpdk_eth_tx_buffer_flush(dpdk_port_id, user_core_id, buffer);
		//	}
		//	prev_tsc = cur_tsc;
		//}
		

        /*
         * recv and send pkts
         */
		gettimeofday(&tv, NULL);
        for(que_id = 0; que_id < dpdk_init_cfg.thread_num; que_id++)
        {
            /*
             * recv pkts per que
             */
            for (i = 0; i < pconf->n_rx_port; i++) 
            {
                /*
                 * recv pkts for each port the core servicing
                 */
                dpdk_port_id = pconf->rx_port_list[i];
                pkt_info.port_in = dpdk_user_port_id_map[dpdk_port_id];
                
        		nb_rx = rte_eth_rx_burst(dpdk_port_id, que_id, pkts_burst, MAX_PKT_BURST);

                if(0 < nb_rx)
                {
            		for (j = 0; j < nb_rx; j++) 
            		{
            			pkt_info.pmbuf      = pkts_burst[j];
            			pkt_info.pkt_data   = rte_pktmbuf_mtod((struct rte_mbuf*)pkt_info.pmbuf, uint8_t *);
            			pkt_info.pkt_len    = rte_pktmbuf_pkt_len((struct rte_mbuf*)pkt_info.pmbuf);
            			pkt_info.drop_flag  = DPDK_ACCEPT;
            			pkt_info.time_stamp = tv;
            			rte_memcpy(pkt_info.pkt_data + DPDK_ETHER_LEN, &pkt_info, sizeof(struct dpdk_pkt_info));
            			pkts_info_burst[j] = (struct dpdk_pkt_info *)(pkt_info.pkt_data + DPDK_ETHER_LEN);
            	    }

                    ret = rte_ring_enqueue_burst(pkt_recv_ring[que_id], (void **)pkts_info_burst, nb_rx);
                    if(unlikely(ret < nb_rx))
                    {
                        cur_pkt_info_burst = pkts_info_burst;
                        while(unlikely(ret < nb_rx))
                        {
                            cur_pkt_info_burst = cur_pkt_info_burst + ret;
                            nb_rx = nb_rx- ret;
                            ret = rte_ring_enqueue_burst(pkt_recv_ring[que_id], (void **)cur_pkt_info_burst, nb_rx);
                        }
                    }
                }
            }
            
            /*
             * send pkts per que
             */
            //ret = rte_ring_dequeue_burst(pkt_send_ring[user_core_id][que_id], (void **)pkts_info_burst, MAX_PKT_BURST);
            //for(j = 0; j < ret; j++)
            //{
            //    mbuf = (struct rte_mbuf *)(pkts_info_burst[j]->pmbuf);
            //    rte_pktmbuf_pkt_len(mbuf)  = pkts_info_burst[j]->pkt_len;
            //    rte_pktmbuf_data_len(mbuf) = pkts_info_burst[j]->pkt_len;
                
            //    if(DPDK_ACCEPT == pkts_info_burst[j]->drop_flag)
            //    {
            //        user_port_id = pkts_info_burst[j]->port_out;
            //        dpdk_simple_forward(mbuf, user_port_id, user_core_id);
            //    }
            //    else
            //    {
            //        rte_pktmbuf_free(mbuf);
            //    }
            //}
        }
	}
}

static int fwd_launch_one_lcore(__attribute__((unused)) void *dummy)
{
	fwd_main_loop();
	return DPDK_R_OK;
}


static void* dpdk_init_thread(void *arg)
{
    if(DPDK_R_OK != dpdk_env_setup())
    {
        goto error_process ;
    }

    if(DPDK_R_OK != dpdk_core_setup())
    {
        goto error_process ;
    }

    if(DPDK_R_OK != dpdk_port_setup())
    {
        goto error_process ;
    }
    if(DPDK_R_OK != dpdk_ring_setup())
    {
        goto error_process ;
    }

    dpdk_init_res_flag = DPDK_INIT_OK;
    
	rte_eal_mp_remote_launch(fwd_launch_one_lcore, NULL, CALL_MASTER);

    return NULL;

    error_process:

    dpdk_init_res_flag = DPDK_INIT_ERROR;
    
    return NULL;
}


int dpdk_nic_init(struct dpdk_init_para *init_para)
{
    pthread_t init_thead_id = 0;
    uint32_t  tm_count = 0;


    if(NULL == init_para)
    {
        printf("func %-20s input para illegal!\n", __FUNCTION__);
        return DPDK_R_ERR;
    }

    if(DPDK_R_OK != dpdk_config_validate(init_para))
    {
        printf("dpdk init config validate failed!\n");
        return DPDK_R_ERR;
    }

    memcpy(&dpdk_init_cfg, init_para, sizeof(struct dpdk_init_para));

    if(DPDK_R_OK != pthread_create(&init_thead_id, NULL, dpdk_init_thread, NULL))
        return DPDK_R_ERR;   

    while(DPDK_IN_INIT == dpdk_init_res_flag)
    {
        if(DPDK_INIT_TIMEOUT < tm_count) 
        {
            printf("init dpdk timeout err.\n"); 
        		return DPDK_R_ERR;
        }
        
        tm_count++;
        sleep(1);

    }

    if(DPDK_INIT_OK == dpdk_init_res_flag)
        return DPDK_R_OK;
    else
        return DPDK_R_ERR;
}


int dpdk_recv_pkt(int thread_id,  struct dpdk_pkt_info **recv_pkt)
{
    if(dpdk_init_cfg.thread_num <= thread_id || NULL == recv_pkt)
    {
        printf("func %-20s input para illegal!\n", __FUNCTION__);
        return DPDK_R_ERR;
    }

    if(DPDK_R_OK != rte_ring_dequeue(pkt_recv_ring[thread_id], (void**)recv_pkt))
        return DPDK_R_ERR;

    app_thread_statistics[thread_id].rcv_packets++;
    return DPDK_R_OK;
}

void dpdk_send_pkt(int thread_id, int port_id, struct dpdk_pkt_info *send_pkt)
{
    if(dpdk_init_cfg.thread_num <= thread_id || NULL == send_pkt
        || dpdk_init_cfg.port_num <= port_id)
    {
        printf("func %-20s input para illegal!\n", __FUNCTION__);
        return;
    }
        
    send_pkt->port_out = port_id;
	struct rte_mbuf *mbuf = (struct rte_mbuf *)(send_pkt->pmbuf);
    rte_pktmbuf_pkt_len(mbuf)  = send_pkt->pkt_len;
    rte_pktmbuf_data_len(mbuf) = send_pkt->pkt_len;

	//send at now
	uint32_t ret = 0;
	do
	{
		ret = rte_eth_tx_burst(port_id, send_pkt->core_id, &mbuf, 1);
	}while (1 != ret);
	
	//while(DPDK_R_OK != rte_ring_enqueue(pkt_send_ring[send_pkt->core_id][thread_id],send_pkt))
    //{
    //    printf("pkt_send_ring is full.\n");
    //}
	
    app_thread_statistics[thread_id].snd_packets++;
    
    return;
}



void dpdk_drop_pkt(int thread_id, struct dpdk_pkt_info *ppkt)
{
    if(dpdk_init_cfg.thread_num <= thread_id || NULL == ppkt)
    {
        printf("func %-20s input para illegal!\n", __FUNCTION__);
        return;
    }

    ppkt->drop_flag = DPDK_DROP;
	if (NULL == ppkt->pmbuf)
		return;
	rte_pktmbuf_free(ppkt->pmbuf);
	//while(DPDK_R_OK != rte_ring_enqueue(pkt_send_ring[ppkt->core_id][thread_id],ppkt))
    //{
    //    printf("pkt_send_ring is full.\n");
    //}
    
    app_thread_statistics[thread_id].drp_packets++;
    
    return ;
}


int dpdk_get_sendbuf(int thread_id, struct dpdk_pkt_info **ppkt)
{
    struct rte_mbuf* mbuf = NULL;
    struct dpdk_pkt_info *pkt_tmp = NULL;

    if(NULL == ppkt)
    {
        printf("func %-20s input para illegal!\n", __FUNCTION__);
        return DPDK_R_ERR;
    }

    mbuf = rte_pktmbuf_alloc(pktmbuf_pool[dpdk_init_cfg.core_num]);

    if(NULL == mbuf)
    {
        app_thread_statistics[thread_id].get_nobuf++;
        printf("not enough mbuf memory!\n");
        return DPDK_R_ERR;
    }
    
    *ppkt = (struct dpdk_pkt_info *)(rte_pktmbuf_mtod(mbuf, uint8_t*) + DPDK_ETHER_LEN);
    pkt_tmp = *ppkt;

    pkt_tmp->pkt_data = rte_pktmbuf_mtod(mbuf, uint8_t*);
    pkt_tmp->pkt_len = DPDK_ETHER_LEN;
    pkt_tmp->port_in = ALLOC_PORT_IN;
    pkt_tmp->core_id = ALLOC_DEFAULT_CORE;
    pkt_tmp->drop_flag = DPDK_ACCEPT;
    pkt_tmp->pmbuf = mbuf;
    gettimeofday(&(pkt_tmp->time_stamp), NULL);

    app_thread_statistics[thread_id].get_buffers++;

    return DPDK_R_OK;
}

void dpdk_get_nic_port_statistics(int port_id, struct dpdk_nic_port_statistics *nic_port_stats)
{
    int dpdk_port_id = 0;
    struct rte_eth_stats stats = {0};

    if(dpdk_init_cfg.port_num <= port_id || NULL == nic_port_stats)
    {
        printf("func %-20s input para illegal!\n", __FUNCTION__);
        return;
    }

    dpdk_port_id = user_dpdk_port_id_map[port_id];
    rte_eth_stats_get(dpdk_port_id, &stats);

    nic_port_stats->rx_packets = stats.ipackets;
    nic_port_stats->rx_bytes = stats.ibytes;
    nic_port_stats->tx_packets = stats.opackets;
    nic_port_stats->tx_bytes = stats.obytes;
    
    nic_port_stats->imissed_packets = stats.imissed;
    nic_port_stats->ierrors_packets = stats.ierrors;
    nic_port_stats->oerrors_packets = stats.oerrors;
    nic_port_stats->rx_nombuf = stats.rx_nombuf;    
    ;
}


void dpdk_get_app_thread_statistics(int thread_id, struct dpdk_app_thread_statistics *app_thread_stats)
{
    if(dpdk_init_cfg.thread_num <= thread_id || NULL == app_thread_stats)
    {
        printf("func %-20s input para illegal!\n", __FUNCTION__);
        return;
    }

    memcpy(app_thread_stats, app_thread_statistics + thread_id, 
        sizeof(struct dpdk_app_thread_statistics));
    
}

void dpdk_print_memory(void)
{
	struct rte_mempool* pmempool = NULL;
	int i = 0;

	printf("==================Memory Stat Start====================\n");
	//收包的使用情况，每个核心申请一块收包内存池
	printf("Each Core Memory Pool:\n");
    for (; i < dpdk_init_cfg.core_num; i++)
   	{
		pmempool = pktmbuf_pool[i];
		if (NULL == pmempool)
		{
			printf("[ERROR]CoreID:%d has no mpoll poniter!\n", i);
			continue;
		}
		
		printf("buf[%s]: count %d, size %d, available %u, alloc %u\n", 
		        pmempool->name,
		        pmempool->size,
		        pmempool->elt_size,
		        rte_mempool_count(pmempool),
		        rte_mempool_free_count(pmempool));
	}

	//动态内存的使用情况
	printf("Dynamic Memory Pool:\n");
	pmempool = pktmbuf_pool[dpdk_init_cfg.core_num];
	if (NULL == pmempool)
	{
		printf("[ERROR]CoreID:%d has no mpoll poniter!\n", dpdk_init_cfg.core_num);
		return;
	}
	
	printf("buf[%s]: count %d, size %d, available %u, alloc %u\n", 
	        pmempool->name,
	        pmempool->size,
	        pmempool->elt_size,
	        rte_mempool_count(pmempool),
	        rte_mempool_free_count(pmempool));
	printf("==================Memory Stat End====================\n");
	
	return;
}

