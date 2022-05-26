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






/*ADC definitions and includes*/
#include <hal/nrf_saadc.h>
#define ADC_NID DT_NODELABEL(adc) 
#define ADC_RESOLUTION 10
#define ADC_GAIN ADC_GAIN_1_4
#define ADC_REFERENCE ADC_REF_VDD_1_4
#define ADC_ACQUISITION_TIME ADC_ACQ_TIME(ADC_ACQ_TIME_MICROSECONDS, 40)
#define ADC_CHANNEL_ID 1 

/* This is the actual nRF ANx input to use. Note that a channel can be assigned to any ANx. In fact a channel can */
/*    be assigned to two ANx, when differential reading is set (one ANx for the positive signal and the other one for the negative signal) */  
/* Note also that the configuration of differnt channels is completely independent (gain, resolution, ref voltage, ...) */
#define ADC_CHANNEL_INPUT NRF_SAADC_INPUT_AIN1  






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






/* ADC channel configuration */
static const struct adc_channel_cfg my_channel_cfg = {
	.gain = ADC_GAIN,
	.reference = ADC_REFERENCE,
	.acquisition_time = ADC_ACQUISITION_TIME,
	.channel_id = ADC_CHANNEL_ID,
	.input_positive = ADC_CHANNEL_INPUT
};

/* Global vars */
struct k_timer my_timer;
const struct device *adc_dev = NULL;
static uint16_t adc_sample_buffer[BUFFER_SIZE];
  





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






/* Takes one sample */
static int adc_sample(void)
{
	int ret;
	const struct adc_sequence sequence = {
		.channels = BIT(ADC_CHANNEL_ID),
		.buffer = adc_sample_buffer,
		.buffer_size = sizeof(adc_sample_buffer),
		.resolution = ADC_RESOLUTION,
	};

	if (adc_dev == NULL) {
            printk("adc_sample(): error, must bind to adc first \n\r");
            return -1;
	}

	ret = adc_read(adc_dev, &sequence);
	if (ret) {
            printk("adc_read() failed with code %d\n", ret);
	}	

	return ret;
}







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





    /* ADC setup: bind and initialize */
    adc_dev = device_get_binding(DT_LABEL(ADC_NID));
	if (!adc_dev) {
        printk("ADC device_get_binding() failed\n");
    } 
    err = adc_channel_setup(adc_dev, &my_channel_cfg);
    if (err) {
        printk("adc_channel_setup() failed with error code %d\n", err);
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

        for(int i = 0; i < 10; i++){
          DadosAB[i] = adc_sample(); 
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

    while(1) {
        k_sem_take(&sem_ab,  K_FOREVER);

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
        
        k_sem_give(&sem_bc);
    }
}

void thread_C_code(void *argA , void *argB, void *argC)
{
    /* Other variables */
    long int nact = 0;
    int ret = 0;

    while(1) {
        k_sem_take(&sem_bc, K_FOREVER);

        ret = pwm_pin_set_usec(pwm0_dev, pwm0_channel, pwmPeriod_us,(unsigned int)((pwmPeriod_us*DadosBC)/100), PWM_POLARITY_NORMAL);
        if (ret) {
          printk("Error %d: failed to set pulse width\n", ret);
          return;
        }      
    }
}

