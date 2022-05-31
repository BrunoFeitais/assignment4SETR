/** @main main.c
 * @brief This program implements cooperative tasks in Zephyr. 
 *
 * 
 * It does a basic processing of an analog signal using FIFO.
 *
 * @author Bruno Feitais
 * @date 2022/05
 * @bug There are no bugs
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

/** ADC definitions and includes */
#include <hal/nrf_saadc.h>
/** ADC definitions and includes */
#define ADC_NID DT_NODELABEL(adc) 
/** ADC definitions and includes */
#define ADC_RESOLUTION 10
/** ADC definitions and includes */
#define ADC_GAIN ADC_GAIN_1_4
/** ADC definitions and includes */
#define ADC_REFERENCE ADC_REF_VDD_1_4
/** ADC definitions and includes */
#define ADC_ACQUISITION_TIME ADC_ACQ_TIME(ADC_ACQ_TIME_MICROSECONDS, 40)
/** ADC definitions and includes */
#define ADC_CHANNEL_ID 1 

/** This is the actual nRF ANx input to use. Note that a channel can be assigned to any ANx.*/
#define ADC_CHANNEL_INPUT NRF_SAADC_INPUT_AIN1

/** Buffer size definition */
#define BUFFER_SIZE 1

/* Other defines */
/** Interval between ADC samples */
#define TIMER_INTERVAL_MSEC 1 

/** ADC channel configuration */
static const struct adc_channel_cfg my_channel_cfg = {
	.gain = ADC_GAIN,
	.reference = ADC_REFERENCE,
	.acquisition_time = ADC_ACQUISITION_TIME,
	.channel_id = ADC_CHANNEL_ID,
	.input_positive = ADC_CHANNEL_INPUT
};

/** Refer to dts file */
#define GPIO0_NID DT_NODELABEL(gpio0)
/** Refer to dts file */
#define PWM0_NID DT_NODELABEL(pwm0)
/** Refer to dts file */
#define BOARDLED1 0x0d /* Pin at which LED1 is connected.  Addressing is direct (i.e., pin number) */

/** Size of stack area used by each thread (can be thread specific)*/
#define STACK_SIZE 1024

/** Thread scheduling priority */
#define thread_A_prio 1
/** Thread scheduling priority */
#define thread_B_prio 1
/** Thread scheduling priority */
#define thread_C_prio 1

/** Therad periodicity (in ms)*/
#define thread_A_period 200

/* Global vars */
struct k_timer my_timer;
const struct device *adc_dev = NULL;
static uint16_t adc_sample_buffer[BUFFER_SIZE];

/** Create thread stack space */
K_THREAD_STACK_DEFINE(thread_A_stack, STACK_SIZE);
/** Create thread stack space */
K_THREAD_STACK_DEFINE(thread_B_stack, STACK_SIZE);
/** Create thread stack space */
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

/** Takes one sample */
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


/** Main function */
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

/** Thread A code implementation. 
 * It reads 1 ADC value and sends to the FIFO queu. */
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
              adc_sample_buffer[0] = 0;
          }
        }
        printk("%d (A)->", adc_sample_buffer[0]);
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

/** Thread B code implementation. 
 * It gets 10 ADC values and does the average. */
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
        printk("(B), ", adc_sample_buffer[0]);
        valores[i] = data_ab->data;
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
          printk("\nValor calculado: %d (B)\n", data_bc.data);
        }           
    }
}

/** Thread C code implementation. 
 * It gets the average and sends it to the LED 1. */
void thread_C_code(void *argA , void *argB, void *argC)
{
    /* Local variables */
    struct data_item_t *data_bc;
    const struct device *pwm0_dev;          /* Pointer to PWM device structure */
    int pwm0_channel  = 13;                 /* Ouput pin associated to pwm channel. See DTS for pwm channel - output pin association */ 
    unsigned int pwmPeriod_us = 1000;       /* PWM period in us */
    int ret = 0;
    long int nact = 0;

    pwm0_dev = device_get_binding(DT_LABEL(PWM0_NID));
    if (pwm0_dev == NULL) {
	printk("Error: PWM device %s is not ready\n", pwm0_dev->name);
	return;
    }
    else  {
        printk("PWM device %s is ready\n", pwm0_dev->name);            
    }

    while(1) {
        data_bc = k_fifo_get(&fifo_bc, K_FOREVER);          
        printk("Valor final: %d (C)\n\n\n",data_bc->data);

        ret = pwm_pin_set_usec(pwm0_dev, pwm0_channel, pwmPeriod_us,(unsigned int)((pwmPeriod_us*data_bc->data)/1023), PWM_POLARITY_NORMAL);
        if (ret) {
          printk("Error %d: failed to set pulse width\n", ret);
          return;
        }
  }
}