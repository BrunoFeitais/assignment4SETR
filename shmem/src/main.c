/*
 * Paulo Pedreiras, 2022/02
 * Zephyr: Simple thread creation example (2)
 * 
 * One of the tasks is periodc, the other two are activated via a semaphore. Data communicated via sharem memory 
 *
 * Base documentation:
 *      https://developer.nordicsemi.com/nRF_Connect_SDK/doc/latest/zephyr/reference/kernel/index.html
 * 
 */

#include <zephyr.h>
#include <device.h>
#include <drivers/gpio.h>
#include <drivers/pwm.h>
#include <sys/printk.h>
#include <sys/__assert.h>
#include <string.h>
#include <timing/timing.h>
#include <stdlib.h>
#include <stdio.h>
#include <devicetree.h>

/* Refer to dts file */
#define GPIO0_NID DT_NODELABEL(gpio0)
#define PWM0_NID DT_NODELABEL(pwm0)
#define BOARDLED1 0x0d /* Pin at which LED1 is connected.  Addressing is direct (i.e., pin number) */

/* Size of stack area used by each thread (can be thread specific, if necessary)*/
#define STACK_SIZE 1024

/* Thread scheduling priority */
#define thread_A_prio 1
#define thread_B_prio 1
#define thread_C_prio 1

/* Therad periodicity (in ms)*/
#define thread_A_period 3000

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

/* Global vars (shared memory between tasks A/B and B/C, resp) */
int DadosAB[] = {0, 0, 0, 0, 0, 0, 0, 0, 0, 0};
int DadosBC = 0;
int ab = 100;
int bc = 200;

/* Semaphores for task synch */
struct k_sem sem_ab;
struct k_sem sem_bc;

/* Thread code prototypes */
void thread_A_code(void *argA, void *argB, void *argC);
void thread_B_code(void *argA, void *argB, void *argC);
void thread_C_code(void *argA, void *argB, void *argC);


/* Main function */
void main(void) {

    const struct device *pwm0_dev;          /* Pointer to PWM device structure */
    int pwm0_channel  = 13;                 /* Ouput pin associated to pwm channel. See DTS for pwm channel - output pin association */ 
    unsigned int pwmPeriod_us = 1000;       /* PWM period in us */
    int ret = 0;
    
    pwm0_dev = device_get_binding(DT_LABEL(PWM0_NID));
    if (pwm0_dev == NULL) {
      printk("Error: PWM device %s is not ready\n", pwm0_dev->name);
      return;
    }
    else  {
      printk("PWM device %s is ready\n", pwm0_dev->name);            
    }

    /* Welcome message */
    printf("\n\r Illustration of the use of shmem + semaphores\n\r");
    
    /* Create and init semaphores */
    k_sem_init(&sem_ab, 0, 1);
    k_sem_init(&sem_bc, 0, 1);
    
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
    
    printk("Thread A init (periodic)\n");

    /* Compute next release instant */
    release_time = k_uptime_get() + thread_A_period;

    /* Thread loop */
    while(1) {
        
        /* Do the workload */          
        printk("\n\nThread A instance %ld released at time: %lld (ms). \n", ++nact, k_uptime_get());  
        
        for(int i = 0; i < 10; i++){
          // DadosAB[i] = GUARGAR ULTIMOS 10 VALORES DO POTENCIOMETRO  
        }
        
        k_sem_give(&sem_ab); 

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
    /* Other variables */
    long int nact = 0;
    int avg = 0;
    int cnt = 0;
    int avgmax = 0;
    int avgmin = 0;


    printk("Thread B init (sporadic, waits on a semaphore by task A)\n");
    while(1) {
        k_sem_take(&sem_ab,  K_FOREVER);
        printk("Thread B instance %ld released at time: %lld (ms). \n", ++nact, k_uptime_get());
        
        for(int i = 0; i < 10; i++){
          avg += DadosAB[i];
        }

        avgmax = avg + avg*0.1;
        avgmin = avg - avg*0.1;

        for(int i = 0; i < 10; i++){
          if(DadosAB[i] < avgmax || DadosAB[i] > avgmin) {
            DadosBC += DadosAB[i];
            cnt++;
          }
        }

        DadosBC = DadosBC/cnt;
        
        printk("Thread B set BC value to: %d \n", DadosBC);
        k_sem_give(&sem_bc);
    }
}

void thread_C_code(void *argA , void *argB, void *argC)
{
    /* Other variables */
    long int nact = 0;
    int ret = 0;

    printk("Thread C init (sporadic, waits on a semaphore by task A)\n");
    while(1) {
        k_sem_take(&sem_bc, K_FOREVER);
        printk("Thread C instance %5ld released at time: %lld (ms). \n", ++nact, k_uptime_get());          
        printk("Task C read BC value: %d\n", DadosBC); 

        ret = pwm_pin_set_usec(pwm0_dev, pwm0_channel, pwmPeriod_us,(unsigned int)((pwmPeriod_us*DadosBC)/100), PWM_POLARITY_NORMAL);
        if (ret) {
          printk("Error %d: failed to set pulse width\n", ret);
          return;
        }      
    }
}

