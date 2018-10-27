
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



        printf("TAP rx    : %-12llu rx_drop :  %-12llu \n", nic_stats.tap_rx, nic_stats.tap_rx_drop);
        printf("TAP tx    : %-12llu tx_drop :  %-12llu \n", nic_stats.tap_tx, nic_stats.tap_tx_drop);
	}

    dpdk_print_memory();
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
    bindnum = thread_id * 2 + 2;
    CPU_SET(bindnum, &mask);
    if (pthread_setaffinity_np(pthread_self(), sizeof(mask), &mask) < 0) {
        printf("set thread affinity failed\n");
    }	
    while(1)
    {
        ret = dpdk_recv_pkt(0, thread_id, &ppkt);
        if(0 == ret)
        {

            /*
            *所有从捕包接口取出的报文要么选择发送，要么选择释放，不做
            *处理会导致缓存池耗尽
            */
            dpdk_drop_pkt(ppkt);
        }
    }
    return NULL;
}

int main()
{
    int ret;
    struct dpdk_init_para init_para;
    init_para.core_num = 3;
    init_para.port_num = 2;


    
    unsigned thread_id;
    time_t t_last,t_now;
    t_last = 0;
    pthread_t tid;
    unsigned arg[APP_THREAD_NUM];
        
    ret = dpdk_nic_init("./nic.conf", &init_para);
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
        if(1 < t_now - t_last)
        {
            t_last = t_now;
            print_stats(&init_para);
        }
        
    }
	
	return 0;
}


