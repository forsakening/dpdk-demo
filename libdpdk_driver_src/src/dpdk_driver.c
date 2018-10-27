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

#ifdef DPDK_NIC_TAP
#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <inttypes.h>
#include <string.h>
#include <sys/queue.h>
#include <stdarg.h>
#include <errno.h>
#include <getopt.h>
#include <netinet/in.h>
#include <linux/if.h>
#include <linux/if_tun.h>
#include <fcntl.h>
#include <sys/ioctl.h>
#include <unistd.h>
#include <signal.h>
#endif

#define DPDK_R_OK 0
#define DPDK_R_ERR -1

#define DPDK_INIT_TIMEOUT 60 //60s

#define MAX_PKT_BURST 32

#define MEMPOOL_CACHE_SIZE 32

#define RTE_TEST_RX_DESC_DEFAULT 512
#define RTE_TEST_TX_DESC_DEFAULT 512

#define DPDK_ETHER_LEN 1600

#define ALLOC_PORT_IN 0xff      /*动态申请报文缓存的入端口*/
#define ALLOC_DEFAULT_CORE 0    /*动态申请报文缓存的处理核心*/

#define CHECK_INTERVAL 100 /* 100ms */
#define MAX_CHECK_TIME 90 /* 9s (90 * 100ms) in total */

#define RX_ONLY 0x1 
#define TX_ONLY 0x2
#define RT_BOTH 0x3

#define LCORE_USED 1

#define TX_RING_SIZE 10000

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

struct port_for_lcore {
	uint8_t user_port_id;
	uint8_t n_que_num;
	uint8_t que_array[DPDK_MAX_QUE_NUM];
} ;

struct lcore_port_conf {
	uint8_t used_flag;
	uint8_t n_rx_port;
	uint8_t n_tx_port;
	struct port_for_lcore rx_port_info[DPDK_MAX_PORT_NUM];
	struct port_for_lcore tx_port_info[DPDK_MAX_PORT_NUM];
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

static uint64_t enabled_core_mask = 0;

static uint16_t nb_rxd = RTE_TEST_RX_DESC_DEFAULT;
static uint16_t nb_txd = RTE_TEST_TX_DESC_DEFAULT;

static uint8_t user_dpdk_port_id_map[DPDK_MAX_PORT_NUM] = {0};
static uint8_t dpdk_user_port_id_map[DPDK_MAX_PORT_NUM] = {0};

static struct lcore_port_conf lcore_port_conf[DPDK_MAX_CORE_NUM] = {{0}};

static uint8_t dpdk_init_res_flag = DPDK_IN_INIT;

static struct dpdk_init_para dpdk_init_cfg = {0};

static struct ether_addr g_ports_eth_addr[DPDK_MAX_PORT_NUM] = {{{0}}};

static struct dpdk_app_thread_statistics  app_thread_statistics[DPDK_MAX_PORT_NUM][DPDK_MAX_APP_THREAD_NUM] = {{0}};

static struct dpdk_port_conf dpdk_port_info[DPDK_MAX_PORT_NUM] = {{0}};

static void*  mpool_send_buf = NULL;

static uint32_t mpool_send_buf_num = 100000;
static char dpdk_brodcast_mac[6] ={0xff, 0xff, 0xff, 0xff, 0xff, 0xff};
#ifdef DPDK_NIC_TAP
static uint64_t dpdk_tap_rx[DPDK_MAX_PORT_NUM] = {0};
static uint64_t dpdk_tap_tx[DPDK_MAX_PORT_NUM] = {0};
static uint64_t dpdk_tap_rx_drop[DPDK_MAX_PORT_NUM] = {0};
static uint64_t dpdk_tap_tx_drop[DPDK_MAX_PORT_NUM] = {0};
static int dpdk_tap_port_id[DPDK_MAX_PORT_NUM] = {0};
#endif

///////////////////////////////////////////////////////////////////////////////////////////////////
static void check_all_ports_link_status(void);

static int dpdk_env_setup(void);

static int dpdk_core_setup(void);

static int dpdk_port_setup(void);

static void fwd_main_loop(void);

static int fwd_launch_one_lcore(__attribute__((unused)) void *dummy);

static void* dpdk_init_thread(void *arg);
#ifdef DPDK_NIC_TAP
typedef struct
{
    int  port_id;
    int  tap_fd;
    char tap_mac[6];
    char tap_name[32];
}DPDK_TAP_INFO;
static DPDK_TAP_INFO dpdk_tap_info[DPDK_MAX_PORT_NUM] = {{0}};
static int tap_create(int port_id)
{
	struct ifreq ifr;
	int fd, ret;
	fd = open("/dev/net/tun", O_RDWR);
	if (fd < 0)
		return -1;
    dpdk_tap_info[port_id].port_id = port_id;
    memcpy(dpdk_tap_info[port_id].tap_mac, dpdk_port_info[port_id].mac_addr, 6);
    sprintf(dpdk_tap_info[port_id].tap_name, "dpdk%d", port_id);
	memset(&ifr, 0, sizeof(ifr));
	ifr.ifr_flags = IFF_TAP | IFF_NO_PI;
	snprintf(ifr.ifr_name, IFNAMSIZ, "%s", dpdk_tap_info[port_id].tap_name);
	ret = ioctl(fd, TUNSETIFF, (void *) &ifr);
	if (ret < 0) {
		close(fd);
		return -1;
	}
    ifr.ifr_ifru.ifru_hwaddr.sa_family = 1;    
    ifr.ifr_ifru.ifru_hwaddr.sa_data[0] = dpdk_tap_info[port_id].tap_mac[0];  
    ifr.ifr_ifru.ifru_hwaddr.sa_data[1] = dpdk_tap_info[port_id].tap_mac[1];  
    ifr.ifr_ifru.ifru_hwaddr.sa_data[2] = dpdk_tap_info[port_id].tap_mac[2];  
    ifr.ifr_ifru.ifru_hwaddr.sa_data[3] = dpdk_tap_info[port_id].tap_mac[3];  
    ifr.ifr_ifru.ifru_hwaddr.sa_data[4] = dpdk_tap_info[port_id].tap_mac[4];  
    ifr.ifr_ifru.ifru_hwaddr.sa_data[5] = dpdk_tap_info[port_id].tap_mac[5];  
    ret = ioctl(fd, SIOCSIFHWADDR, (void *) &ifr);
    if (ret < 0) {
		close(fd);
		return -1;
	}
    dpdk_tap_info[port_id].tap_fd = fd;
	return 0;
}
static int dpdk_tap_create(void)
{
    int i = 0;
    for (; i < dpdk_init_cfg.port_num; i++)
    {
        if (dpdk_port_info[i].tap_pkt_mode == TAP_MODE_NO)
        {
            printf("PortID:%d not need to create tap .\n", i);
            continue;
        }
        if (0 != tap_create(i))
        {
            printf("PortID:%d create tap Failed \n", i);
            return -1;
        }
        else
            printf("PortID:%d create tap ok,tap fd:%d name:%s \n", i, dpdk_tap_info[i].tap_fd, dpdk_tap_info[i].tap_name);
    }
    return 0;
}
static void* dpdk_tap_rx_thread(void *arg)
{   
    printf("Entering Thread %s ...\n" ,__FUNCTION__);
    int i,j,ret,tap_fd;
    struct rte_mbuf* m = NULL;
    struct dpdk_pkt_info *ppkt = NULL;
    int tx_que_num = 0;
    while (1)
    {
        for (i = 0; i < dpdk_init_cfg.port_num; i++)
        {  
            tap_fd = dpdk_tap_info[i].tap_fd;
            for (j = 0; j < dpdk_port_info[i].rx_que_num; j++)
            {
                if (0 == rte_ring_sc_dequeue(dpdk_port_info[i].tap_ring[j], (void**)(&m)))
                {
    				ret = write(tap_fd, rte_pktmbuf_mtod(m, void*), rte_pktmbuf_data_len(m));
    				rte_pktmbuf_free(m);
    				if (unlikely(ret < 0))
    					dpdk_tap_rx_drop[i]++;
    				else
    					dpdk_tap_rx[i]++;
                }
                else
                    usleep(1);
            }
        }
    }
}
static void* dpdk_tap_tx_thread(void *arg)
{   
    int ret,tap_fd, tx_que_num;
    struct rte_mbuf* m = NULL;
    struct dpdk_pkt_info *ppkt = NULL;
    int port_id = *((int*)arg);
    printf("Entering Thread %s, portid:%d ...\n" ,__FUNCTION__, port_id);
    tap_fd = dpdk_tap_info[port_id].tap_fd;
    tx_que_num = dpdk_port_info[port_id].tx_que_num;
    while (1)
    {
        if (DPDK_R_OK != dpdk_get_sendbuf(&ppkt))
        {
            dpdk_tap_tx_drop[port_id]++;
            sleep(1);
            continue;
        }
        m = ppkt->pmbuf;
        ret = read(tap_fd, rte_pktmbuf_mtod(m, void *), DPDK_ETHER_LEN);
        if (unlikely(ret < 0)) {
            dpdk_tap_tx_drop[port_id]++;
            dpdk_drop_pkt(ppkt);
            sleep(1);
			continue;
		}
        m->nb_segs = 1;
		m->next = NULL;
		m->pkt_len = (uint16_t)ret;
		m->data_len = (uint16_t)ret;
		ret = rte_eth_tx_burst(port_id, tx_que_num, &m, 1);
		if (unlikely(ret < 1)) {
			rte_pktmbuf_free(m);
			dpdk_tap_tx_drop[port_id]++;
		}
		else 
			dpdk_tap_tx[port_id]++;
    }
}
#endif

////////////////////////////////////////////////////////////////////////////////////////////////////

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

    for(i = 0; i < dpdk_init_cfg.core_num; i++)
    {
		dpdk_core_id = dpdk_init_cfg.core_arr[i];
	    enabled_core_mask |=  (((uint64_t)1) << dpdk_core_id);
		printf("Get a coreid:%d , current mask:%llx \n", dpdk_core_id, enabled_core_mask);        
    }
    snprintf(core_mask_str, 64, "0x%llx", enabled_core_mask);
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
            if(0 == memcmp(mac.addr_bytes, dpdk_port_info[i].mac_addr, 6))
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
                i,dpdk_port_info[i].mac_addr[0],dpdk_port_info[i].mac_addr[1],dpdk_port_info[i].mac_addr[2],
                dpdk_port_info[i].mac_addr[3],dpdk_port_info[i].mac_addr[4],dpdk_port_info[i].mac_addr[5]);
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
	int i,j,k;
    int user_port_id = 0;
    int dpdk_port_id = 0;
    int user_core_id = 0;
    int dpdk_core_id = 0;
	uint32_t cache_pkt = 0;
    char mbuf_pool_name[32] = {0};
	char recv_ring_name[64] = {0};
	char send_ring_name[64] = {0};
	struct lcore_port_conf *pconf = NULL;

    for(i = 0; i < dpdk_init_cfg.port_num; i++)
    {
		if (TX_ONLY == dpdk_port_info[i].mode)
		{
			printf("PortID:%d is tx only mode, still have to rx memory setup...\n", i);
		}
	
    	for (j = 0; j < dpdk_port_info[i].rx_que_num; j++)
    	{
    		//申请mbuf内存
			snprintf(mbuf_pool_name, 32, "m_port%d_que%d",i, j);
			cache_pkt = dpdk_port_info[i].cache_pkt_num;
			dpdk_port_info[i].mbuf_pool[j] = \
				rte_pktmbuf_pool_create(mbuf_pool_name,             \
										cache_pkt,                  \
										MEMPOOL_CACHE_SIZE,         \
										0,                          \
										RTE_MBUF_DEFAULT_BUF_SIZE,  \
    									rte_socket_id());

			if (NULL == dpdk_port_info[i].mbuf_pool[j])
			{
				printf( "Cannot init mbuf pool:%s,size:%u\n", mbuf_pool_name, cache_pkt);
    			return DPDK_R_ERR;
			}

			//申请ring
			uint32_t ring_size = rte_align32pow2(cache_pkt);
			snprintf(recv_ring_name, 64, "r_port%d_que%d",i, j);
			dpdk_port_info[i].r_ring[j] = rte_ring_create(recv_ring_name, ring_size, SOCKET_ID_ANY, 0);
			if (NULL == dpdk_port_info[i].r_ring[j])
			{
				printf("Cannot init ring:%s,size:%u\n", recv_ring_name, ring_size);
				return DPDK_R_ERR;
			}
        #ifdef DPDK_NIC_TAP
            char tap_ring_name[64] = {0};
            uint32_t tap_ring_size = 4096;
			snprintf(tap_ring_name, 64, "tap_port%d_que%d",i, j);
			dpdk_port_info[i].tap_ring[j] = rte_ring_create(tap_ring_name, tap_ring_size, SOCKET_ID_ANY, 0);
			if (NULL == dpdk_port_info[i].tap_ring[j])
			{
				printf("Cannot init ring:%s,size:%u\n", tap_ring_name, tap_ring_size);
				return DPDK_R_ERR;
			}
        #endif
		}

		//tx
		for (j = 0; j < dpdk_port_info[i].tx_que_num; j++)
		{
			//申请ring
			uint32_t ring_size = rte_align32pow2(TX_RING_SIZE);
			snprintf(send_ring_name, 64, "t_port%d_que%d",i, j);
			dpdk_port_info[i].t_ring[j] = rte_ring_create(send_ring_name, ring_size, SOCKET_ID_ANY, 0);
			if (NULL == dpdk_port_info[i].t_ring[j])
			{
				printf("Cannot init ring:%s,size:%u\n", send_ring_name, ring_size);
				return DPDK_R_ERR;
			}
		}
    }

	//额外申请一块用于主动发送的内存
	mpool_send_buf = rte_pktmbuf_pool_create("send_buf",            \
										mpool_send_buf_num,         \
										MEMPOOL_CACHE_SIZE,         \
										0,                          \
										RTE_MBUF_DEFAULT_BUF_SIZE,  \
    									rte_socket_id());
	if (NULL == mpool_send_buf)
	{
		printf( "Cannot init send buf pool,size:%d\n", mpool_send_buf_num);
    	return DPDK_R_ERR;
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
    int rx_que_num = 0;
	int tx_que_num = 0;
	
    /*
    *初始化各个端口
    */
    for (user_port_id = 0; user_port_id < dpdk_init_cfg.port_num; user_port_id++) 
    {
        printf("Initializing port %d...\n", user_port_id);
		dpdk_port_id = user_dpdk_port_id_map[user_port_id];
		rx_que_num = dpdk_port_info[user_port_id].rx_que_num;
		tx_que_num = dpdk_port_info[user_port_id].tx_que_num;
		
    #ifdef DPDK_NIC_TAP
        tx_que_num += 1;
    #endif
        ret = rte_eth_dev_configure(dpdk_port_id, rx_que_num, tx_que_num, &port_conf);
        if (ret < 0)
        {
            printf("Cannot configure device: err=%d, port=%d\n", ret, user_port_id);
			return DPDK_R_ERR;
		}
		
        rte_eth_macaddr_get(dpdk_port_id,&g_ports_eth_addr[user_port_id]);

        /*
        *初始化端口的各个接收队列
        */
        for(i = 0; i < rx_que_num; i++)
        {
			if (TX_ONLY == dpdk_port_info[user_port_id].mode)
			{
				printf("PortID:%d is tx only mode, still have to rx que setup...\n", i);
			}
		
            ret = rte_eth_rx_queue_setup(dpdk_port_id, i, nb_rxd,
                             rte_eth_dev_socket_id(dpdk_port_id),
                             NULL,
                             dpdk_port_info[user_port_id].mbuf_pool[i]);
            if (ret < 0)
            {
                printf("rte_eth_rx_queue_setup:err=%d, port=%d queid=%d\n",
                      ret, user_port_id, i);
                return DPDK_R_ERR;
            }
			else
				printf("DPDK-Port:%d Que id:%d Rx init success ...\n", dpdk_port_id, i);
        }

        /*
        *初始化端口的各个发送队列
        */
        for(i = 0; i < tx_que_num; i++)
        {
			if (RX_ONLY == dpdk_port_info[user_port_id].mode)
			{
				printf("PortID:%d is rx only mode, still have to setup tx que...\n", i);
			} 
		
            ret = rte_eth_tx_queue_setup(dpdk_port_id, i, nb_txd,
                    rte_eth_dev_socket_id(dpdk_port_id),
                    NULL);
            if (ret < 0)
            {
                printf("rte_eth_tx_queue_setup:err=%d, port=%d queid=%d\n",
                    ret, user_port_id, i);
            }
			else
				printf("DPDK-Port:%d Que id:%d Tx init success ...\n", dpdk_port_id, i);
        }
       
        /* 开启端口 */
        ret = rte_eth_dev_start(dpdk_port_id);
        if (ret < 0)
        {
            printf("rte_eth_dev_start:err=%d, port=%u\n",
                  ret, user_port_id);
			
            return DPDK_R_ERR;
        }

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

/* main processing loop */
static void fwd_main_loop(void)
{
    int i = 0;
    int j = 0;
	int k = 0;
    uint8_t que_id = 0;
	uint8_t dpdk_core_id = 0;
	uint8_t user_core_id = 0;
    uint8_t dpdk_port_id = 0;
    uint8_t user_port_id = 0;
    uint16_t ret = 0;
    uint16_t nb_real, nb_rx = 0;
	uint16_t nb_tx = 0;
    struct timeval tv = {0};
	struct lcore_port_conf *pconf = NULL;
	struct rte_mbuf *mbuf = NULL;
	struct rte_mbuf *pkts_burst[MAX_PKT_BURST] = {NULL};
	struct dpdk_pkt_info pkt_info = {{0}};
	struct dpdk_pkt_info *pkts_info_burst[MAX_PKT_BURST] = {NULL};
	struct dpdk_pkt_info **cur_pkt_info_burst = NULL;
	
	dpdk_core_id = rte_lcore_id();
	pkt_info.core_id = dpdk_core_id;
	pconf = &lcore_port_conf[dpdk_core_id];
	if (pconf->n_rx_port == 0 && pconf->n_tx_port == 0) 
	{
		printf("lcore %u has nothing to do\n", dpdk_core_id);
		return;
	}

	printf("entering main loop on lcore %u\n", dpdk_core_id);

	while (1) 
	{	
        /*
         * recv pkts
         */
        for (i = 0; i < pconf->n_rx_port; i++) 
        {
            /*
             * recv pkts for each port the core servicing
             */
			gettimeofday(&tv, NULL);
			
			user_port_id = pconf->rx_port_info[i].user_port_id;
            dpdk_port_id = dpdk_user_port_id_map[user_port_id];
            pkt_info.port_in = user_port_id;

			for (j = 0; j < pconf->rx_port_info[i].n_que_num; j++)
			{
				que_id = pconf->rx_port_info[i].que_array[j];
				nb_rx = rte_eth_rx_burst(dpdk_port_id, que_id, pkts_burst, MAX_PKT_BURST);
                if(0 < nb_rx)
                {
                #ifdef DPDK_NIC_TAP
                    if (dpdk_port_info[user_port_id].tap_pkt_mode == TAP_MODE_ALL)
                    {
                        nb_real = rte_ring_mp_enqueue_burst(dpdk_port_info[user_port_id].tap_ring[que_id], 
                                                            (void **)pkts_burst, nb_rx, NULL);
                        if (unlikely(nb_real < nb_rx))
                        {
                            for (k = nb_real; k < nb_rx; k++)
                                rte_pktmbuf_free(pkts_burst[k]);
                        }
                        continue;
                    }
                    else if (dpdk_port_info[user_port_id].tap_pkt_mode == TAP_MODE_MAC)
                    {
                        struct rte_mbuf *tmp_pkts_burst[MAX_PKT_BURST];
                        uint16_t tmp_nb_rx = nb_rx;
                        uint16_t non_tap_cnt = 0;
                        rte_memcpy(&tmp_pkts_burst, &pkts_burst, nb_rx * sizeof(pkts_burst[0]));
                        for (k = 0; k < tmp_nb_rx; k++)
                        {
                            char *dst_mac = rte_pktmbuf_mtod((struct rte_mbuf*)tmp_pkts_burst[k], uint8_t *);
                            if ((0 == memcmp(dst_mac, dpdk_port_info[user_port_id].mac_addr, 6)) || 
                                (0 == memcmp(dst_mac, dpdk_brodcast_mac, 6)))
                            {
                                if( 0 != rte_ring_mp_enqueue(dpdk_port_info[user_port_id].tap_ring[que_id], tmp_pkts_burst[k]))
                                    rte_pktmbuf_free(tmp_pkts_burst[k]);
                                nb_rx--;
                                continue;
                            }
                            else
                            {
                                pkts_burst[non_tap_cnt++] = tmp_pkts_burst[k];
                            }
                        }
                        if (0 >= nb_rx)
                            continue;
                    }
                #endif
            		for (k = 0; k < nb_rx; k++) 
            		{
            			pkt_info.pmbuf      = pkts_burst[k];
            			pkt_info.pkt_data   = rte_pktmbuf_mtod((struct rte_mbuf*)pkt_info.pmbuf, uint8_t *);
            			pkt_info.pkt_len    = rte_pktmbuf_pkt_len((struct rte_mbuf*)pkt_info.pmbuf);
            			pkt_info.drop_flag  = DPDK_ACCEPT;
            			pkt_info.time_stamp = tv;
            			rte_memcpy(pkt_info.pkt_data + DPDK_ETHER_LEN, &pkt_info, sizeof(struct dpdk_pkt_info));
            			pkts_info_burst[k] = (struct dpdk_pkt_info *)(pkt_info.pkt_data + DPDK_ETHER_LEN);
            	    }

					void* ring = dpdk_port_info[user_port_id].r_ring[que_id];
                    ret = rte_ring_enqueue_burst(ring, (void **)pkts_info_burst, nb_rx, NULL);
                    if(unlikely(ret < nb_rx))
                    {
                        cur_pkt_info_burst = pkts_info_burst;
                        while(unlikely(ret < nb_rx))
                        {
                            cur_pkt_info_burst = cur_pkt_info_burst + ret;
                            nb_rx = nb_rx- ret;
                            ret = rte_ring_enqueue_burst(ring, (void **)cur_pkt_info_burst, nb_rx, NULL);
                        }
                    }
                }
			}
    	}

		for (i = 0; i < pconf->n_tx_port; i++) 
        {
			user_port_id = pconf->tx_port_info[i].user_port_id;
            dpdk_port_id = dpdk_user_port_id_map[user_port_id];

			for (j = 0; j < pconf->tx_port_info[i].n_que_num; j++)
			{
				que_id = pconf->tx_port_info[i].que_array[j];
				void* ring = dpdk_port_info[user_port_id].t_ring[que_id];
				nb_tx = rte_ring_dequeue_burst(ring, (void **)pkts_burst, MAX_PKT_BURST, NULL);

				//for debug
				//int debug = 0;
				//struct rte_mbuf *d_mbuf;
				//for (; debug < nb_tx; debug++)
				//{
				//	d_mbuf = pkts_burst[debug];
				//	printf("Send A pkt,len:%d, mbuf:0x%x from ring:0x%x...\n", rte_pktmbuf_pkt_len(d_mbuf), d_mbuf, ring);
				//}
				
				uint16_t to_send = nb_tx;
                uint16_t sent = 0;
				struct rte_mbuf **cur_pkts_tx = pkts_burst;
				while (sent < to_send)
				{
					to_send = to_send - sent;
					cur_pkts_tx = cur_pkts_tx + sent;
					sent = rte_eth_tx_burst(dpdk_port_id, que_id, cur_pkts_tx, to_send);
				}
			}
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

#ifdef DPDK_NIC_TAP
    if (0 != dpdk_tap_create())
    {
        printf("TAP Create Module Error!\n");
        goto error_process ;
    }
    pthread_t thread_id;
    if(DPDK_R_OK != pthread_create(&thread_id, NULL, dpdk_tap_rx_thread, NULL))
    {
        printf("Create dpdk_tap_rx_thread Error!\n");
        goto error_process ;
    }
    int i = 0;
    for (; i < dpdk_init_cfg.port_num; i++)
    {
        dpdk_tap_port_id[i] = i;
        if(DPDK_R_OK != pthread_create(&thread_id, NULL, dpdk_tap_tx_thread, &dpdk_tap_port_id[i]))
        {
            printf("Create dpdk_tap_tx_thread %d Error!\n", i);
            goto error_process ;
        }
    }
#endif
    dpdk_init_res_flag = DPDK_INIT_OK;
    
	rte_eal_mp_remote_launch(fwd_launch_one_lcore, NULL, CALL_MASTER);

    return NULL;

    error_process:

    dpdk_init_res_flag = DPDK_INIT_ERROR;
    
    return NULL;
}
//Mac,Mode,RxQue,TxQue,CacheNum,RxCoreArray,TxCoreArray   
//Just Like:  01:02:03:04:05:06,3,16,16,10000,[3 4],[5 6]
int dpdk_parse_one_line(char* line, struct dpdk_port_conf* port_conf)
{
	if (NULL == line || NULL == port_conf)
		return DPDK_R_ERR;

	char mac[32] = {0};
	int  mode = 0;
	int  rx_que = 0;
	int  tx_que = 0;
	int  cache_num = 0;
	char rx_core_arr[64] = {0};
	char tx_core_arr[64] = {0};
    int  tap_mode = 0;

	int split_num = 0;
	char *split = NULL;
	split = strtok(line, ",");
	if (NULL == split)
	{
		printf("Format Error!\n");
		return DPDK_R_ERR;
	}
	else
	{
		split_num++;
		memcpy(mac, split, strlen(split));
	}

	while (split = strtok(NULL, ","))
	{
		split_num++;
		if (2 == split_num)
		{
			mode = atoi(split);
		}
		else if (3 == split_num)
		{
			rx_que = atoi(split);
		}
		else if (4 == split_num)
		{
			tx_que = atoi(split);
		}
		else if (5 == split_num)
		{
			cache_num = atoi(split);
		}
		else if (6 == split_num)
		{
			memcpy(rx_core_arr, split, strlen(split));
		}
		else if (7 == split_num)
		{
			memcpy(tx_core_arr, split, strlen(split));
		}
        else if (8 == split_num)
        {
            tap_mode = atoi(split);
		}
	}
	

	//mac
	int mac1,mac2,mac3,mac4,mac5,mac6;
	if	(6 != sscanf(mac, "%02x:%02x:%02x:%02x:%02x:%02x", &mac1,&mac2,&mac3,&mac4,&mac5,&mac6))
	{
		printf("Formact parse error!\n");
		return DPDK_R_ERR;
	}
	else
	{
		port_conf->mac_addr[0] = (uint8_t)mac1;
		port_conf->mac_addr[1] = (uint8_t)mac2;
		port_conf->mac_addr[2] = (uint8_t)mac3;
		port_conf->mac_addr[3] = (uint8_t)mac4;
		port_conf->mac_addr[4] = (uint8_t)mac5;
		port_conf->mac_addr[5] = (uint8_t)mac6;
	}
	
	//mode
	if (RX_ONLY > mode || RT_BOTH < mode)
	{
		printf("Mode:%d Error!\n", mode);
		return DPDK_R_ERR;
	}
	else
	{
		port_conf->mode = mode;
	}

	//cache_num
	port_conf->cache_pkt_num = cache_num;
	
	//rx_que
	if (TX_ONLY == mode)
	{
		port_conf->rx_que_num = 1;
		port_conf->cache_pkt_num = (2*RTE_TEST_RX_DESC_DEFAULT);
	}
	else
	{
		if (rx_que > DPDK_MAX_QUE_NUM || rx_que == 0)
		{
			printf("Rx Que:%d Error !\n", rx_que);
			return DPDK_R_ERR;
		}

		port_conf->rx_que_num = rx_que;
	}

	//tx_que
	if (RX_ONLY == mode)
	{
		port_conf->tx_que_num = 1;
	}
	else
	{
		if (tx_que > DPDK_MAX_QUE_NUM || tx_que == 0)
		{
			printf("Tx Que:%d Error !\n", tx_que);
			return DPDK_R_ERR;
		}

		port_conf->tx_que_num = tx_que;
	}

	//rx_core_array
	char* rx_core_start = rx_core_arr; 
	char* r_tmp = NULL;
	while (r_tmp = strstr(rx_core_start, "["))
	{
		rx_core_start = (r_tmp+1);
	}
	
	if (rx_core_arr == rx_core_start)
	{
		printf("RX Core Array not find [ ...\n");
		return DPDK_R_ERR;
	}

	char* rx_core_end = strstr(rx_core_arr, "]");
	if (NULL == rx_core_end)
	{
		printf("RX Core Array not find ] ...\n");
		return DPDK_R_ERR;
	}

	if (rx_core_end <= rx_core_start)
	{
		printf("RX Core Array Format Error! \n");
		return DPDK_R_ERR;
	}

	*rx_core_end = '\0';

	int rx_core_num = 0;
	char* r_p = NULL;
	r_p = strtok(rx_core_start, " ");
	if (NULL != r_p)
	{
		port_conf->rx_core_arr[rx_core_num] = atoi(r_p);
		rx_core_num++;
	}
	
	while (r_p = strtok(NULL, " "))
	{
		port_conf->rx_core_arr[rx_core_num] = atoi(r_p);
		rx_core_num++;	
	}

	port_conf->rx_core_num = rx_core_num;

	//tx core arr
	char* tx_core_start = tx_core_arr; 
	char* t_tmp = NULL;
	while (t_tmp = strstr(tx_core_start, "["))
	{
		tx_core_start = (t_tmp+1);
	}
	
	if (tx_core_arr == tx_core_start)
	{
		printf("TX Core Array not find [ ...\n");
		return DPDK_R_ERR;
	}

	char* tx_core_end = strstr(tx_core_arr, "]");
	if (NULL == tx_core_end)
	{
		printf("TX Core Array not find ] ...\n");
		return DPDK_R_ERR;
	}

	if (tx_core_end <= tx_core_start)
	{
		printf("TX Core Array Format Error! \n");
		return DPDK_R_ERR;
	}

	*tx_core_end = '\0';

	int tx_core_num = 0;
	char* t_p = NULL;
	t_p = strtok(tx_core_start, " ");
	if (NULL != t_p)
	{
		port_conf->tx_core_arr[tx_core_num] = atoi(t_p);
		tx_core_num++;
	}
	
	while (t_p = strtok(NULL, " "))
	{
		port_conf->tx_core_arr[tx_core_num] = atoi(t_p);
		tx_core_num++;	
	}

	port_conf->tx_core_num = tx_core_num;

    if (TAP_MODE_NO > tap_mode || TAP_MODE_ALL < tap_mode)
    {
        printf("Tap Mode:%d Error!\n", tap_mode);
		return DPDK_R_ERR;        
    }
    if (TAP_MODE_NO == tap_mode)
    {
        port_conf->tap_pkt_mode = tap_mode;
    }
    else
    {
        if (mode != RX_ONLY && mode != RT_BOTH)
        {
            printf("Tap Mode Enable, port mode [%d] must include rx mode!\n", mode);
            return DPDK_R_ERR;
        }
        port_conf->tap_pkt_mode = tap_mode;
    }
	return DPDK_R_OK;
}

int dpdk_setup_config(void)
{
	int i = 0;
	for (; i < dpdk_init_cfg.port_num; i++)
	{
		//上层的处理队列，一定要是核数的整数倍
		if (0 != dpdk_port_info[i].rx_que_num % dpdk_port_info[i].rx_core_num)
		{
			printf("PortID:%d, RxQueNum:%d, CoreNum:%d Error, not mod right!\n", i, dpdk_port_info[i].rx_que_num, dpdk_port_info[i].rx_core_num);
			return DPDK_R_ERR;
		}

		if (0 != dpdk_port_info[i].tx_que_num % dpdk_port_info[i].tx_core_num)
		{
			printf("PortID:%d, TxQueNum:%d, CoreNum:%d Error, not mod right!\n", i, dpdk_port_info[i].tx_que_num, dpdk_port_info[i].tx_core_num);
			return DPDK_R_ERR;
		}

		//rx
		int num = dpdk_port_info[i].rx_que_num / dpdk_port_info[i].rx_core_num;
		int core_id,port_idx;
		int j = 0;
		for (; j < dpdk_port_info[i].rx_core_num; j++)
		{
			if (TX_ONLY == dpdk_port_info[i].mode)
				break;
		
			core_id = dpdk_port_info[i].rx_core_arr[j];
			port_idx = lcore_port_conf[core_id].n_rx_port;
			struct port_for_lcore *port_conf = &(lcore_port_conf[core_id].rx_port_info[port_idx]);
			port_conf->n_que_num = num;
			port_conf->user_port_id = i;
			
			int k = 0;
			for (; k < num; k++)
			{
				port_conf->que_array[k] = j*num + k;
			}
			
			lcore_port_conf[core_id].n_rx_port++;
			lcore_port_conf[core_id].used_flag = LCORE_USED;
		}

		//tx
		num = dpdk_port_info[i].tx_que_num / dpdk_port_info[i].tx_core_num;
		for (j = 0; j < dpdk_port_info[i].tx_core_num; j++)
		{
			if (RX_ONLY == dpdk_port_info[i].mode)
				break;
			
			core_id = dpdk_port_info[i].tx_core_arr[j];
			port_idx = lcore_port_conf[core_id].n_tx_port;
			struct port_for_lcore *port_conf = &(lcore_port_conf[core_id].tx_port_info[port_idx]);
			port_conf->n_que_num = num;
			port_conf->user_port_id = i;
			
			int k = 0;
			for (; k < num; k++)
			{
				port_conf->que_array[k] = j*num + k;
			}
			
			lcore_port_conf[core_id].n_tx_port++;
			lcore_port_conf[core_id].used_flag = LCORE_USED;
		}
	}

	int core_idx;
	for (i = 0; i < DPDK_MAX_CORE_NUM; i++)
	{
		if (lcore_port_conf[i].used_flag == LCORE_USED)
		{
			core_idx = dpdk_init_cfg.core_num;
			dpdk_init_cfg.core_arr[core_idx] = i;
			dpdk_init_cfg.core_num++;
		}
	}

	return DPDK_R_OK;
}

int dpdk_init_config(const char* file_path)
{
	if (NULL == file_path)
		return DPDK_R_ERR;

	FILE* cfg = fopen(file_path, "rb");
	if (NULL == cfg)
		return DPDK_R_ERR;

	//初始化配置
	char lines[128] = {0};
	int  valid_lines = 0;
	while (fgets(lines, sizeof(lines), cfg))
	{
		printf("Get One Cfg ==> %s \n", lines);
		if (DPDK_R_OK != dpdk_parse_one_line(lines, &dpdk_port_info[valid_lines]))
		{
			return DPDK_R_ERR;
		}

		valid_lines++;
		memset(lines,0,sizeof(lines));
	}

	fclose(cfg);
	cfg = NULL;

	//setup配置
	dpdk_init_cfg.port_num = valid_lines;
	if (DPDK_R_OK != dpdk_setup_config())
	{
		return DPDK_R_ERR;
	}

	//打印配置
	printf("=====================Config Info=====================\n");
	int i,j,k;

	for (i = 0; i < dpdk_init_cfg.port_num; i++)
	{
		printf("PortID:%d \n", i);
		printf("    Mode:%d \n", dpdk_port_info[i].mode);
		printf("    RxQueNum:%d \n", dpdk_port_info[i].rx_que_num);
		printf("    TxQueNum:%d \n", dpdk_port_info[i].tx_que_num);
		printf("    RxCacheNum:%d \n", dpdk_port_info[i].cache_pkt_num);
		printf("    RXCoreNum:%d \n", dpdk_port_info[i].rx_core_num);
        printf("    TapMode:%d \n", dpdk_port_info[i].tap_pkt_mode);
		for (j = 0; j < dpdk_port_info[i].rx_core_num; j++)
		{
			printf("        CoreID:%d \n", dpdk_port_info[i].rx_core_arr[j]);
		}
		printf("    TXCoreNum:%d \n", dpdk_port_info[i].tx_core_num);
		for (j = 0; j < dpdk_port_info[i].tx_core_num; j++)
		{
			printf("        CoreID:%d \n", dpdk_port_info[i].tx_core_arr[j]);
		}
	}
	
	//输出core进行处理的端口及其队列
	for (i = 0; i < dpdk_init_cfg.core_num; i++)
	{
		int core_id = dpdk_init_cfg.core_arr[i];
		printf("CoreID:%d\n", core_id);
		for (j = 0; j < lcore_port_conf[core_id].n_rx_port; j++)
		{
			printf("==== RXPortID:%d\n", lcore_port_conf[core_id].rx_port_info[j].user_port_id);
			for (k = 0; k < lcore_port_conf[core_id].rx_port_info[j].n_que_num; k++)
			{
				printf("    ==== RXQueID:%d\n", lcore_port_conf[core_id].rx_port_info[j].que_array[k]);
			}
		}
		for (j = 0; j < lcore_port_conf[core_id].n_tx_port; j++)
		{
			printf("==== TXPortID:%d\n", lcore_port_conf[core_id].tx_port_info[j].user_port_id);
			for (k = 0; k < lcore_port_conf[core_id].tx_port_info[j].n_que_num; k++)
			{
				printf("    ==== TXQueID:%d\n", lcore_port_conf[core_id].tx_port_info[j].que_array[k]);
			}
		}
	}
	printf("=====================================================\n\n");

	return DPDK_R_OK;
}

int dpdk_nic_init(const char* file_path, struct dpdk_init_para* init_para)
{
    pthread_t init_thead_id = 0;
    uint32_t  tm_count = 0;

	if (NULL == init_para)
	{
		printf("Func:%s para error!\n", __FUNCTION__);
		return DPDK_R_ERR;
	}

    //从配置文件中读取信息
    if (DPDK_R_OK != dpdk_init_config(file_path))
   	{
   		printf("Init DPDK config file error:%s!\n", file_path);
		return DPDK_R_ERR;
	}

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

    return DPDK_R_OK;
    if(DPDK_INIT_OK == dpdk_init_res_flag)
    {
    	memcpy(init_para, &dpdk_init_cfg, sizeof(dpdk_init_cfg));
        return DPDK_R_OK;
    }
	else
        return DPDK_R_ERR;
}


int dpdk_recv_pkt(int port_id, int que_id,  struct dpdk_pkt_info **recv_pkt)
{
    if(dpdk_port_info[port_id].rx_que_num <= que_id || NULL == recv_pkt)
    {
        printf("func %-20s input para illegal, que_id:%d, recv_pkt:%p!\n", __FUNCTION__, que_id, recv_pkt);
        return DPDK_R_ERR;
    }

    if(DPDK_R_OK != rte_ring_dequeue(dpdk_port_info[port_id].r_ring[que_id], (void**)recv_pkt))
        return DPDK_R_ERR;

    app_thread_statistics[port_id][que_id].rcv_packets++;
	//printf("%s:Get A pkt,len:%d, mbuf:0x%x ...\n", __FUNCTION__, (*recv_pkt)->pkt_len, (*recv_pkt)->pmbuf);
    return DPDK_R_OK;
}

int dpdk_send_pkt(int port_id, int que_id, struct dpdk_pkt_info *send_pkt)
{
    if(dpdk_port_info[port_id].tx_que_num <= que_id || NULL == send_pkt)
    {
        printf("func %-20s input para illegal, que_id:%d, recv_pkt:%p!\n", __FUNCTION__, que_id, send_pkt);
        return;
    }

	struct rte_mbuf *mbuf = (struct rte_mbuf *)(send_pkt->pmbuf);
    rte_pktmbuf_pkt_len(mbuf)  = send_pkt->pkt_len;
    rte_pktmbuf_data_len(mbuf) = send_pkt->pkt_len;
		
	void* ring = dpdk_port_info[port_id].t_ring[que_id];
    if (0 != rte_ring_enqueue(ring, mbuf))
    {
        return DPDK_R_ERR;
    }

	//printf("%s:Send A pkt,len:%d, mbuf:0x%x to ring:0x%x ...\n", __FUNCTION__,send_pkt->pkt_len, mbuf, ring);
	//dpdk_print_memory();
	
    app_thread_statistics[port_id][que_id].snd_packets++;
    
    return DPDK_R_OK;
}



void dpdk_drop_pkt(struct dpdk_pkt_info *ppkt)
{
    if(NULL == ppkt)
    {
        printf("func %-20s input para illegal!\n", __FUNCTION__);
        return;
    }

    ppkt->drop_flag = DPDK_DROP;
	if (NULL == ppkt->pmbuf)
		return;
	rte_pktmbuf_free(ppkt->pmbuf);
    
    //app_thread_statistics[thread_id].drp_packets++;
    
    return ;
}


int dpdk_get_sendbuf(struct dpdk_pkt_info **ppkt)
{
    struct rte_mbuf* mbuf = NULL;
    struct dpdk_pkt_info *pkt_tmp = NULL;

    if(NULL == ppkt)
    {
        printf("func %-20s input para illegal!\n", __FUNCTION__);
        return DPDK_R_ERR;
    }

    mbuf = rte_pktmbuf_alloc(mpool_send_buf);

    if(NULL == mbuf)
    {
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

#ifdef DPDK_NIC_TAP
    nic_port_stats->tap_rx = dpdk_tap_rx[port_id];
    nic_port_stats->tap_tx = dpdk_tap_tx[port_id];
    nic_port_stats->tap_rx_drop = dpdk_tap_rx_drop[port_id];
    nic_port_stats->tap_tx_drop = dpdk_tap_tx_drop[port_id];
#endif
	return;
}


void dpdk_get_app_thread_statistics(int thread_id, struct dpdk_app_thread_statistics *app_thread_stats)
{
    return;
}

void dpdk_print_memory(void)
{
	struct rte_mempool* pmempool = NULL;
	struct rte_ring* pring = NULL; 
	int i = 0;
	int j = 0;

	printf("==================Memory Stat Start====================\n");
	//收包的使用情况，每个核心申请一块收包内存池
	printf("Each Core Memory Pool:\n");
    for (; i < dpdk_init_cfg.port_num; i++)
   	{
   		for (j = 0; j < dpdk_port_info[i].rx_que_num; j++)
   		{
			pmempool = dpdk_port_info[i].mbuf_pool[j];
			if (NULL == pmempool)
			{
				printf("[ERROR]PortID:%d-QueID:%d has no mpool poniter!\n", i, j);
				continue;
			}
			
			printf("buf[%s]: count %d, size %d, available %u, alloc %u\n", 
			        pmempool->name,
			        pmempool->size,
			        pmempool->elt_size,
			        rte_mempool_avail_count(pmempool),
			        rte_mempool_in_use_count(pmempool));

			pring = dpdk_port_info[i].r_ring[j];
			if (NULL == pring)
			{
				printf("[ERROR]PortID:%d-QueID:%d has no r_ring poniter!\n", i, j);
				continue;
			}

			printf("ring[%s]: count %d, used %u\n", 
			        pring->name,
			        pring->size,
			        rte_ring_count(pring));
        #ifdef DPDK_NIC_TAP
            pring = dpdk_port_info[i].tap_ring[j];
			if (NULL == pring)
			{
				printf("[ERROR]PortID:%d-QueID:%d has no tap_ring poniter!\n", i, j);
				continue;
			}
			printf("ring[%s]: count %d, used %u\n", 
			        pring->name,
			        pring->size,
			        rte_ring_count(pring));
        #endif
		}

		for (j = 0; j < dpdk_port_info[i].tx_que_num; j++)
   		{
			pring = dpdk_port_info[i].t_ring[j];
			if (NULL == pring)
			{
				printf("[ERROR]PortID:%d-QueID:%d has no r_ring poniter!\n", i, j);
				continue;
			}

			printf("ring[%s]: count %d, used %u\n", 
			        pring->name,
			        pring->size,
			        rte_ring_count(pring));
		}
	}

	//动态内存的使用情况
	printf("Dynamic Memory Pool:\n");
	pmempool = mpool_send_buf;
	if (NULL == pmempool)
	{
		printf("[ERROR]Mpoll Send Buf poniter Error!\n");
		return;
	}
	
	printf("buf[%s]: count %d, size %d, available %u, alloc %u\n", 
	        pmempool->name,
	        pmempool->size,
	        pmempool->elt_size,
	        rte_mempool_avail_count(pmempool),
	        rte_mempool_in_use_count(pmempool));
	printf("==================Memory Stat End====================\n");
	
	return;
}

