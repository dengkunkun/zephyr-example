/*
 * SPDX-License-Identifier: Apache-2.0
 *
 * @file app_msg.c
 * @brief Application message bus implementation.
 *
 * Provides a simple publish/subscribe model on top of Zephyr k_msgq.
 * Replaces the bare-metal msg.c circular queue.
 */

#include "app_msg.h"
#include <zephyr/logging/log.h>
#include <string.h>

LOG_MODULE_REGISTER(app_msg, LOG_LEVEL_INF);

struct subscriber {
	struct k_msgq *q;
	uint32_t       type_mask;
};

static struct subscriber subscribers[APP_MSG_MAX_SUBSCRIBERS];
static int sub_count;

int app_msg_init(void)
{
	memset(subscribers, 0, sizeof(subscribers));
	sub_count = 0;
	LOG_INF("app_msg init");
	return 0;
}

int app_msg_subscribe(struct k_msgq *q, uint32_t type_mask)
{
	if (sub_count >= APP_MSG_MAX_SUBSCRIBERS) {
		LOG_ERR("subscriber table full");
		return -ENOMEM;
	}
	subscribers[sub_count].q         = q;
	subscribers[sub_count].type_mask = type_mask;
	sub_count++;
	return 0;
}

void app_msg_publish(const struct app_msg *msg)
{
	uint32_t bit = BIT(msg->type);

	for (int i = 0; i < sub_count; i++) {
		if (subscribers[i].type_mask & bit) {
			/* Non-blocking: drop if queue is full */
			int ret = k_msgq_put(subscribers[i].q, msg, K_NO_WAIT);
			if (ret) {
				LOG_WRN("msg type %d dropped (q full)", msg->type);
			}
		}
	}
}
