/*
 * Paulo Pedreiras, 2022/02
 * Zephyr: Simple thread creation example (3)
 * 
 * One of the tasks is periodc, the other two synchronzie via a fifo 
 * 
 * Base documentation:
 *      https://developer.nordicsemi.com/nRF_Connect_SDK/doc/latest/zephyr/reference/kernel/index.html
 * 
 */


#include <zephyr.h>
#include <device.h>
#include <drivers/gpio.h>
#include <sys/printk.h>
#include <sys/__assert.h>
#include <string.h>

/* Size of stack area used by each thread (can be thread specific, if necessary)*/
#define STACK_SIZE 1024

/* Thread scheduling priority */
#define thread_A_prio 1
#define thread_B_prio 1
#define thread_C_prio 1

/* Therad periodicity (in ms)*/
#define thread_A_period 1000


/* Create thread stack space */
K_THREAD_STACK_DEFINE(thread_A_stack, STACK_SIZE);
K_THREAD_STACK_DEFINE(thread_B_stack, STACK_SIZE);
K_THREAD_STACK_DEFINE(thread_C_stack, STACK_SIZE);
  
/* Create variables for thread data */
struct k_thread thread_A_data;
struct k_thread thread_B_data;
struct k_thread thread_C_data;

/* Create task IDs */
k_tid_t thread_A_tid;
k_tid_t thread_B_tid;
k_tid_t thread_C_tid;

/* Create fifos*/
struct k_fifo fifo_ab;
struct k_fifo fifo_bc;

/* Create fifo data structure and variables */
struct data_item_t {
    void *fifo_reserved;    /* 1st word reserved for use by FIFO */
    uint16_t data;          /* Actual data */
};

/* Thread code prototypes */
void thread_A_code(void *, void *, void *);
void thread_B_code(void *, void *, void *);
void thread_C_code(void *, void *, void *);


/* Main function */
void main(void) {

    /* Welcome message */
     printk("\n\r IPC via FIFO example \n\r");
    
    /* Create/Init fifos */
    k_fifo_init(&fifo_ab);
    k_fifo_init(&fifo_bc);
        
    /* Create tasks */
    thread_A_tid = k_thread_create(&thread_A_data, thread_A_stack,
        K_THREAD_STACK_SIZEOF(thread_A_stack), thread_A_code,
        NULL, NULL, NULL, thread_A_prio, 0, K_NO_WAIT);

    thread_B_tid = k_thread_create(&thread_B_data, thread_B_stack,
        K_THREAD_STACK_SIZEOF(thread_B_stack), thread_B_code,
        NULL, NULL, NULL, thread_B_prio, 0, K_NO_WAIT);

    thread_B_tid = k_thread_create(&thread_C_data, thread_C_stack,
        K_THREAD_STACK_SIZEOF(thread_C_stack), thread_C_code,
        NULL, NULL, NULL, thread_C_prio, 0, K_NO_WAIT);

    
    return;

} 

/* Thread code implementation */
void thread_A_code(void *argA , void *argB, void *argC)
{
    /* Timing variables to control task periodicity */
    int64_t fin_time=0, release_time=0;

    /* Other variables */
    long int nact = 0;
    struct data_item_t data_ab;
    
    printk("Thread A init (periodic)\n");

    /* Compute next release instant */
    release_time = k_uptime_get() + thread_A_period;
    
    /* Thread loop */
    while(1) {
        
        /* Do the workload */          
        printk("\n\nThread A instance %ld released at time: %lld (ms). \n",++nact, k_uptime_get());  
                
        data_ab.data = nact + 100;
        k_fifo_put(&fifo_ab, &data_ab);
        printk("Thread A data in fifo_ab: %d\n",data_ab.data);  
       
        /* Wait for next release instant */ 
        fin_time = k_uptime_get();
        if( fin_time < release_time) {
            k_msleep(release_time - fin_time);
            release_time += thread_A_period;

        }
    }

}

void thread_B_code(void *argA , void *argB, void *argC)
{
    /* Local variables */
    long int nact = 0;
    struct data_item_t *data_ab;
    struct data_item_t data_bc;

    printk("Thread B init (sporadic, waits on a semaphore by task A)\n");
    while(1) {
        
        data_ab = k_fifo_get(&fifo_ab, K_FOREVER);
    
        printk("Thread B instance %ld released at time: %lld (ms). \n",++nact, k_uptime_get());  
        printk("Task B read fifo ab value: %d\n",data_ab->data);
        
        data_bc.data = nact + 200;

        k_fifo_put(&fifo_bc, &data_bc);
        printk("Thread B set fifo bc value to: %d \n",data_bc.data);     
               
  }
}

void thread_C_code(void *argA , void *argB, void *argC)
{
    /* Local variables */
    long int nact = 0;
    struct data_item_t *data_bc;

    printk("Thread C init (sporadic, waits on a semaphore by task A)\n");
    while(1) {
        data_bc = k_fifo_get(&fifo_bc, K_FOREVER);
        printk("Thread C instance %5ld released at time: %lld (ms). \n",++nact, k_uptime_get());          
        printk("Task C read bc value: %d\n",data_bc->data);
            
  }
}

