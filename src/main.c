/*
 * Copyright (c) 2016 Intel Corporation.
 *
 * SPDX-License-Identifier: Apache-2.0
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <zephyr/kernel.h>
#include <zephyr/sys/printk.h>
#include <zephyr/sys/__assert.h>
#include <zephyr/usb/usb_device.h>
#include <zephyr/usb/usbd.h>
#include <zephyr/console/console.h>
#include <zephyr/drivers/uart.h>
#include <zephyr/drivers/gpio.h>
#include <zephyr/device.h>
#include <zephyr/drivers/pwm.h>
#include <zephyr/sys/util.h>

#include "parser.h"


BUILD_ASSERT(DT_NODE_HAS_COMPAT(DT_CHOSEN(zephyr_console), zephyr_cdc_acm_uart),
	     "Console device is not ACM CDC UART device");


#define LED0_NODE DT_ALIAS(led0)

#define LED1_NODE DT_ALIAS(led1)


#if !DT_NODE_HAS_STATUS(LED0_NODE, okay)
#error "Unsupported board: led0 devicetree alias is not defined"
#endif

#if !DT_NODE_HAS_STATUS(LED1_NODE, okay)
#error "Unsupported board: led1 devicetree alias is not defined"
#endif

extern void blink_entry(void *, void *, void *);
extern void blink2_entry(void *, void *, void *);

#define DEFAULT_STACK_SIZE (500U)
#define DEFAULT_PRIORITY (5U)

#define TOTAL_TASKS (3U)

#define SLEEP_MSEC	(1U)

#define MIN_PERIOD (1U)
#define MAX_PERIOD (10000)

K_THREAD_STACK_DEFINE(task1_stack_area, DEFAULT_STACK_SIZE);
static struct k_thread task1_data;

K_THREAD_STACK_DEFINE(task2_stack_area, DEFAULT_STACK_SIZE);
static struct k_thread task2_data;

K_THREAD_STACK_DEFINE(task3_stack_area, DEFAULT_STACK_SIZE);
static struct k_thread task3_data;

K_FIFO_DEFINE(blink1_fifo);
K_FIFO_DEFINE(blink2_fifo);
K_FIFO_DEFINE(blink3_fifo);

K_SEM_DEFINE(blink_sem, 0, 1);

static const struct pwm_dt_spec pwm_led2 = PWM_DT_SPEC_GET(DT_ALIAS(pwm_led2));
static const struct gpio_dt_spec led0 = GPIO_DT_SPEC_GET_OR(LED0_NODE, gpios, {0});
static const struct gpio_dt_spec led1 = GPIO_DT_SPEC_GET_OR(LED1_NODE, gpios, {0});

typedef struct {
	uint32_t period;
	uint32_t brightness;
	bool enable;
} blink_data_t;

struct fifo_data_t {
	void *fifo_reserved; /* 1st word reserved for use by fifo */
	blink_data_t blink_data;
};

void task1_blink(const struct gpio_dt_spec * gpio, struct k_fifo * fifo, struct k_sem * pub, struct k_sem * sub)
{
	int ret;
	int led_state = 0;
	int led_mask = 0x01;
	size_t period = 500;
	bool enable = true;
	int64_t time_stamp;

	if (!device_is_ready(gpio->port)) {
		printk("Error: %s device is not ready\n", gpio->port->name);
		return;
	}

	ret = gpio_pin_configure_dt(gpio, GPIO_OUTPUT);
	if (ret != 0) {
		printk("Error %d: failed to configure pin %d \n",
			ret, gpio->pin);
		return;
	}

	time_stamp = k_uptime_get();
	while (1) {
		struct fifo_data_t * rx_data = k_fifo_get(fifo, K_MSEC(1));
		if (rx_data) {
			period = rx_data->blink_data.period;
			enable = rx_data->blink_data.enable;
			k_free(rx_data);
		}
		if (enable) {
			if (sub) {
				/* By the way, we can simply check the status of another GPIO */
				uint32_t sem = k_sem_count_get(sub);
				if ( sem == 0) {					
					led_mask = 0;
					gpio_pin_set(gpio->port, gpio->pin, 0);
				} else {
					led_mask = 0x01;
					gpio_pin_set(gpio->port, gpio->pin, led_state);
				}
			}
			if ((k_uptime_get() - time_stamp) >= period) {
				time_stamp = k_uptime_get();
				led_state ^= 1;
				gpio_pin_set(gpio->port, gpio->pin, led_state & led_mask);
				if (pub) {
					if (led_state == 0) {
						k_sem_give(pub);
					} else {
						k_sem_reset(pub);
					}
				}
			}
		}
	}
}


void pwm_blink(struct k_fifo * fifo)
{
	uint32_t pulse_width = 0;
	size_t period = 500;
	uint8_t dir = 1;
	uint32_t brightness = 50;
	int ret;
	bool enable = true;

	if (!pwm_is_ready_dt(&pwm_led2)) {
		printk("Error: PWM device %s is not ready\n",
		       pwm_led2.dev->name);
		return;
	}
	pwm_set_pulse_dt(&pwm_led2, pwm_led2.period);
	while (1) {
		struct fifo_data_t * rx_data = k_fifo_get(fifo, K_NO_WAIT);
		if (rx_data) {
			period = rx_data->blink_data.period;
			enable = rx_data->blink_data.enable;
			brightness = rx_data->blink_data.brightness;
			k_free(rx_data);
		}

		if (period == 0) {
			ret = pwm_set_pulse_dt(&pwm_led2, 0);
			if (ret) {
				printk("Error %d: failed to set pulse width %u\n", ret, pulse_width);
				return;
			}
		}
		else if (enable) {
			uint32_t max_period = pwm_led2.period * brightness / 100;
			uint32_t step = max_period / period;

			ret = pwm_set_pulse_dt(&pwm_led2, pulse_width);
			if (ret) {
				printk("Error %d: failed to set pulse width %u\n", ret, pulse_width);
				return;
			}

			if (dir) {
				pulse_width += step;
				if (pulse_width >= max_period) {
					pulse_width = max_period - step;
					dir = 0;
				}
			} else {
				if (pulse_width >= step) {
					pulse_width -= step;
				} else {
					pulse_width = step;
					dir = 1;
				}
			}
		}

		k_msleep(SLEEP_MSEC);
	}
}


void blink_entry(void * unused1, void * unused2, void * unused3)
{
	task1_blink(&led0, &blink1_fifo, &blink_sem, NULL);
}

void blink2_entry(void * unused1, void * unused2, void * unused3)
{
	task1_blink(&led1, &blink2_fifo, NULL, &blink_sem);
}

static void send_notification(const blink_data_t * data, struct k_fifo * fifo)
{
	struct fifo_data_t tx_data;
	memcpy(&tx_data.blink_data, data, sizeof(blink_data_t));

	size_t size = sizeof(struct fifo_data_t);
	char *mem_ptr = k_malloc(size);
	__ASSERT_NO_MSG(mem_ptr != 0);
	memcpy(mem_ptr, &tx_data, size);
	k_fifo_put(fifo, mem_ptr);
}

static void process_input(const char * input)
{
	static blink_data_t tasks_data[3] = {
		{.period = 500, .enable = true, .brightness = 100},
		{.period = 500, .enable = true, .brightness = 100},
		{.period = 500, .enable = true, .brightness = 50}
	};

	parser_data_t parse_results = ParseLine(input);
	uint32_t notify_mask = 0;

	switch(parse_results.cmd) {
		case CMD_TOGGLE_EXEC_ALL:
			if (0 == parse_results.args_count) {
				for(size_t i = 0; i < TOTAL_TASKS; i++) {
					tasks_data[i].enable = !tasks_data[i].enable;
					notify_mask |= 1 << i;
				}
			}
		break;
		case CMD_DELAY_ONE:
			if (2 == parse_results.args_count &&
				parse_results.arg1 <= TOTAL_TASKS &&
				parse_results.arg1 > 0) {
				size_t arg1 = parse_results.arg1 - 1;

				tasks_data[arg1].period = CLAMP(parse_results.arg2, MIN_PERIOD, MAX_PERIOD);
				notify_mask |= 1 << arg1;
			}
		break;
		case CMD_DELAY_ALL:
			if (1 == parse_results.args_count) {
				uint32_t period = CLAMP(parse_results.arg1, MIN_PERIOD, MAX_PERIOD);
				for (size_t i = 0; i < TOTAL_TASKS; i++) {
					tasks_data[i].period = period;
					notify_mask |= 1 << i;
				}
			}
		break;
		case CMD_TOGGLE_EXEC_ONE:
			if (1 == parse_results.args_count &&
				parse_results.arg1 <= TOTAL_TASKS && 
				parse_results.arg1 > 0) {
				size_t arg1 = parse_results.arg1 - 1;

				tasks_data[arg1].enable = !tasks_data[arg1].enable;
				notify_mask |= 1 << arg1;
			}
		break;
		case CMD_PWM:
			if (2 == parse_results.args_count &&
			    parse_results.arg1 <= 100 && 
				parse_results.arg1 >= 0) {
				tasks_data[2].brightness = parse_results.arg1;
				tasks_data[2].period = CLAMP(parse_results.arg2, MIN_PERIOD, MAX_PERIOD);
				notify_mask |= 1 << 2;
			}
		break;
		case CMD_UNSUPPORTED:
		/* FALLTHROUGH */
		default:

		break;
	};

	if (notify_mask & (1 << 0)) {
		send_notification(&tasks_data[0], &blink1_fifo);
	}
	if (notify_mask & (1 << 1)) {
		send_notification(&tasks_data[1], &blink2_fifo);
	}
	if (notify_mask & (1 << 2)) {
		send_notification(&tasks_data[2], &blink3_fifo);
	}
	/*using notify_mask as a flag if we have the wrong command*/
	if (0 == notify_mask)
	{
		printk("Unknown command\n");
	}
}

int main(void)
{
	const struct device *const dev = DEVICE_DT_GET(DT_CHOSEN(zephyr_console));	
	
	if (usb_enable(NULL)) {
		return 0;
	}

	uint32_t dtr = 0;
	/* Poll if the DTR flag was set */
	while (!dtr) {
		uart_line_ctrl_get(dev, UART_LINE_CTRL_DTR, &dtr);
		/* Give CPU resources to low priority threads. */
		k_sleep(K_MSEC(100));
	}


	k_tid_t tid1 = k_thread_create(&task1_data, task1_stack_area,
									K_THREAD_STACK_SIZEOF(task1_stack_area),
									blink_entry,
									NULL, NULL, NULL,
									DEFAULT_PRIORITY, 0, K_NO_WAIT);
	
	k_tid_t tid2 = k_thread_create(&task2_data, task2_stack_area,
									K_THREAD_STACK_SIZEOF(task2_stack_area),
									blink2_entry,
									NULL, NULL, NULL,
									DEFAULT_PRIORITY, 0, K_NO_WAIT);

	k_tid_t tid3 = k_thread_create(&task3_data, task3_stack_area,
									K_THREAD_STACK_SIZEOF(task2_stack_area),
									(k_thread_entry_t)pwm_blink,
									&blink3_fifo, NULL, NULL,
									DEFAULT_PRIORITY, 0, K_NO_WAIT);
	
	console_getline_init();

	while (1) {
		process_input(console_getline());
	}
	/* This code should not be executed. */
	k_thread_join(tid1, K_NO_WAIT);
	k_thread_join(tid2, K_NO_WAIT);
	k_thread_join(tid3, K_NO_WAIT);
}
