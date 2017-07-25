
#define _GNU_SOURCE
#include <sched.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <pthread.h>

//#include "i_public.h"
#include "dpdk_driver.h"

#define APP_THREAD_NUM 3


/* Print out statistics on packets dropped */
void print_stats(struct dpdk_init_para *init_para)
{
	unsigned portid;

	unsigned thread_id;


	const char clr[] = { 27, '[', '2', 'J', '\0' };
	const char topLeft[] = { 27, '[', '1', ';', '1', 'H','\0' };

	struct dpdk_nic_port_statistics nic_stats;
	struct dpdk_app_thread_statistics app_stats;

		/* Clear screen and move to top left */
	printf("%s%s", clr, topLeft);

	printf("\nPort statistics ====================================");

	for (portid = 0; portid < init_para->port_num; portid++) {
		/* skip disabled ports */

		printf("\nStatistics for port %u ------------------------------\n",portid);
		dpdk_get_nic_port_statistics(portid, &nic_stats);

		printf("RX-packets: %-12llu RX-bytes:  %-12llu\n", nic_stats.rx_packets,nic_stats.rx_bytes);
        printf("RX-error  : %-12llu Reason:[CRC or BadLen Error By Packet]\n",  nic_stats.ierrors_packets);
	    printf("RX-nombuf : %-12llu Reason:[Mempool Not Enough, Not Equal To Packet Num]\n", nic_stats.rx_nombuf);
        printf("RX-missed : %-12llu Reason:[Fwd Thread Been Scheduled or Enque Nic Que Error Happen]\n", nic_stats.imissed_packets);
	    printf("TX-packets: %-12llu TX-bytes:  %-12llu TX-errors: %-12llu\n",
		   nic_stats.tx_packets, nic_stats.tx_bytes, nic_stats.oerrors_packets);
	}

	printf("\nthread statistics ====================================");

	for (thread_id = 0; thread_id < init_para->thread_num; thread_id++) {
		printf("\nStatistics for thread %u ------------------------------\n",thread_id);
		dpdk_get_app_thread_statistics(thread_id, &app_stats);

		printf("rcv-packets: %-12llu snd-packets:  %-12llu drop-packets:  %-12llu\n", 
		    app_stats.rcv_packets,app_stats.snd_packets,app_stats.drp_packets);
		printf("get-buffers: %-12llu get-nobufs:  %-12llu\n", app_stats.get_buffers,app_stats.get_nobuf);
	}

}



void *worker_process(void *arg)
{
    struct dpdk_pkt_info *ppkt;
    struct dpdk_pkt_info *ppkt2;
    int thread_id = *(unsigned *)arg;
    int ret;
    cpu_set_t mask;
    int bindnum;

    CPU_ZERO(&mask);
    bindnum = thread_id + 4;
    CPU_SET(bindnum, &mask);
    if (pthread_setaffinity_np(pthread_self(), sizeof(mask), &mask) < 0) {
        printf("set thread affinity failed\n");
    }	
    while(1)
    {
        ret = dpdk_recv_pkt(thread_id, &ppkt);
        if(0 == ret)
        {
            #if 1
            if(0 == dpdk_get_sendbuf(thread_id, &ppkt2))
            {
                memcpy(ppkt2->pkt_data, ppkt->pkt_data, ppkt->pkt_len);
                ppkt2->pkt_len = ppkt->pkt_len;
                dpdk_send_pkt(thread_id,1,ppkt2);
            }

            #endif
            /*
            *所有从捕包接口取出的报文要么选择发送，要么选择释放，不做
            *处理会导致缓存池耗尽
            */
            dpdk_send_pkt(thread_id,1,ppkt);
        }
    }
    return NULL;
}

int main()
{
    int ret;
    struct dpdk_init_para init_para;
    init_para.cache_pkt_num = 655360;
    init_para.thread_num = APP_THREAD_NUM;
    init_para.core_num = 1;
    init_para.port_num = 2;
    init_para.core_arr[0] = 1;
    init_para.core_arr[1] = 2;
    init_para.core_arr[2] = 3;
    init_para.port_arr[0][0] = 0x00;
    init_para.port_arr[0][1] = 0x1b;
    init_para.port_arr[0][2] = 0x21;
    init_para.port_arr[0][3] = 0x99;
    init_para.port_arr[0][4] = 0x2b;
    init_para.port_arr[0][5] = 0xf4;

    init_para.port_arr[1][0] = 0x00;
    init_para.port_arr[1][1] = 0x1b;
    init_para.port_arr[1][2] = 0x21;
    init_para.port_arr[1][3] = 0x99;
    init_para.port_arr[1][4] = 0x2b;
    init_para.port_arr[1][5] = 0xf5;

    init_para.port_arr[2][0] = 0xec;
    init_para.port_arr[2][1] = 0x9e;
    init_para.port_arr[2][2] = 0xcd;
    init_para.port_arr[2][3] = 0x1a;
    init_para.port_arr[2][4] = 0xbb;
    init_para.port_arr[2][5] = 0x82;
    
    unsigned thread_id;
    time_t t_last,t_now;
    t_last = 0;
    pthread_t tid;
    unsigned arg[APP_THREAD_NUM];
        
    ret = dpdk_nic_init(&init_para);
    if(0 != ret)
    {
        printf("init error!\n");
        return -1;
    }
    
    for(thread_id = 0;thread_id < APP_THREAD_NUM; thread_id++)
    {
        arg[thread_id] = thread_id;
        ret = pthread_create(&tid, NULL, worker_process, &arg[thread_id]);
        if(0 != ret)
        {
            printf("worker thread %u create failed!\n",thread_id);
            return -1;
        }            
    }    
    
    while(1)
    {

        time(&t_now);
        if(10 < t_now - t_last)
        {
            t_last = t_now;
            print_stats(&init_para);
        }
        
    }
	
	return 0;
}


