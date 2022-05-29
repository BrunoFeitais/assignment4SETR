/*
 * Bruno Feitais, 05/2022
 * 
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
#include <drivers/adc.h>

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

#define BUFFER_SIZE 1

/* Other defines */
#define TIMER_INTERVAL_MSEC 1000 /* Interval between ADC samples */

/* ADC channel configuration */
static const struct adc_channel_cfg my_channel_cfg = {
	.gain = ADC_GAIN,
	.reference = ADC_REFERENCE,
	.acquisition_time = ADC_ACQUISITION_TIME,
	.channel_id = ADC_CHANNEL_ID,
	.input_positive = ADC_CHANNEL_INPUT
};

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
#define thread_A_period 300

/* Global vars */
struct k_timer my_timer;
const struct device *adc_dev = NULL;
static uint16_t adc_sample_buffer[BUFFER_SIZE];

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
void thread_A_code(void *, void *, void *);
void thread_B_code(void *, void *, void *);
void thread_C_code(void *, void *, void *);


/* Main function */
void main(void) {

    int err = 0;

    adc_dev = device_get_binding(DT_LABEL(ADC_NID));
	if (!adc_dev) {
        printk("ADC device_get_binding() failed\n");
    } 
    err = adc_channel_setup(adc_dev, &my_channel_cfg);
    if (err) {
        printk("adc_channel_setup() failed with error code %d\n", err);
    }

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

    thread_C_tid = k_thread_create(&thread_C_data, thread_C_stack,
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
    int err = 0;
    int i = 0;
    int cnt = 0;
    long int nact = 0;
    struct data_item_t data_ab[10];

    /* Compute next release instant */
    release_time = k_uptime_get() + thread_A_period;
    
    /* Thread loop */
    while(1) {
        err=adc_sample();
        if(err) {
          printk("adc_sample() failed with error code %d\n\r",err);
        }
        else {
          if(adc_sample_buffer[0] > 1023) {
              printk("adc reading out of range\n\r");
          }
        }
        data_ab->data = adc_sample_buffer[0];
        /* Wait for next release instant */ 
        k_fifo_put(&fifo_ab, &data_ab);  

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
    int i= 0;
    struct data_item_t *data_ab;
    struct data_item_t data_bc;
    int valores[10] = {0,0,0,0,0,0,0,0,0,0};

    while(1) {
        data_ab = k_fifo_get(&fifo_ab, K_FOREVER);
        valores[i] = data_ab->data;
        printk("%d, ", valores[i]); 
        i++;

        int avg = 0;
        int cnt = 0;
        int avgmax = 0;
        int avgmin = 0;
        int sum = 0;

        if(i > 9){
          for(i = 0; i < 10; i++){
            avg += valores[i];
          }
          avg = avg/10;

          avgmax = avg + avg*0.1;
          avgmin = avg - avg*0.1;

          for(i = 0; i < 10; i++){
            if(valores[i] < avgmax || valores[i] > avgmin) {
              sum += valores[i];
              cnt++;
            }
          }
          i = 0;

          data_bc.data = sum/cnt;

          k_fifo_put(&fifo_bc, &data_bc);
          printk("\nValor calculado: %d \n", data_bc.data);
        }           
    }
}

void thread_C_code(void *argA , void *argB, void *argC)
{
    /* Local variables */
    long int nact = 0;
    struct data_item_t *data_bc;

    while(1) {
        data_bc = k_fifo_get(&fifo_bc, K_FOREVER);          
        printk("Valor final: %d\n\n\n",data_bc->data);
  }
}