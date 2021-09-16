/*
 * Copyright 2018-2021 NXP
 *
 */
/*
 * The code contained herein is licensed under the GNU General Public
 * License. You may obtain a copy of the GNU General Public License
 * Version 2 or later at the following locations:
 *
 * http://www.opensource.org/licenses/gpl-license.html
 * http://www.gnu.org/copyleft/gpl.html
 */
/*
 * queue.c
 *
 * Author Ming Qian<ming.qian@nxp.com>
 */
#include <unistd.h>
#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <assert.h>
#include "list.h"
#include "pitcher_def.h"
#include "queue.h"

#define QUEUE_IDLE_MAX_THD		16

struct queue_node {
	struct list_head list;
	unsigned long item;
};

struct queue_t {
	struct list_head queue;
	struct list_head idles;
	long idle_num;
	long count;
};

static struct queue_node *__alloc_node(void)
{
	struct queue_node *node;

	node = pitcher_calloc(1, sizeof(*node));
	if (!node)
		return NULL;

	INIT_LIST_HEAD(&node->list);

	return node;
}

static void __free_node(struct queue_node *node)
{
	list_del_init(&node->list);
	SAFE_RELEASE(node, pitcher_free);
}

static struct queue_node *__get_idle_node(struct queue_t *queue)
{
	struct queue_node *node;

	assert(queue);

	if (list_empty(&queue->idles))
		return __alloc_node();

	node = list_first_entry(&queue->idles, struct queue_node, list);
	list_del_init(&node->list);
	queue->idle_num--;

	return node;
}

static void __put_idle_node(struct queue_t *queue, struct queue_node *node)
{
	assert(queue);
	if (!node)
		return;

	if (queue->idle_num < QUEUE_IDLE_MAX_THD) {
		list_add_tail(&node->list, &queue->idles);
		queue->idle_num++;
	} else {
		__free_node(node);
	}
}

static void __remove_queue_node(struct queue_t *queue, struct queue_node *node)
{
	if (!node || !queue)
		return;

	list_del_init(&node->list);
	queue->count--;
}

static  int __push_node_to_queue(struct queue_t *queue, struct queue_node *node)
{
	assert(queue);
	if (!node)
		return -RET_E_NULL_POINTER;

	list_add_tail(&node->list, &queue->queue);
	queue->count++;

	return RET_OK;
}

static struct queue_node *__pop_node_from_queue(struct queue_t *queue)
{
	struct queue_node *node;

	assert(queue);

	if (list_empty(&queue->queue))
		return NULL;
	node = list_first_entry(&queue->queue, struct queue_node, list);
	__remove_queue_node(queue, node);

	return node;
}

Queue pitcher_init_queue(void)
{
	struct queue_t *queue;

	queue = pitcher_calloc(1, sizeof(*queue));
	if (!queue)
		return NULL;

	INIT_LIST_HEAD(&queue->queue);
	INIT_LIST_HEAD(&queue->idles);
	queue->idle_num = 0;
	queue->count = 0;

	return queue;
}

void pitcher_destroy_queue(Queue q)
{
	struct queue_t *queue = q;
	struct queue_node *node;
	struct queue_node *tmp;

	if (!queue)
		return;

	list_for_each_entry_safe(node, tmp, &queue->queue, list) {
		__remove_queue_node(queue, node);
		__put_idle_node(queue, node);
	}

	list_for_each_entry_safe(node, tmp, &queue->idles, list) {
		list_del_init(&node->list);
		queue->idle_num--;
		__free_node(node);
	}

	SAFE_RELEASE(queue, pitcher_free);
}

int pitcher_queue_push_back(Queue q, unsigned long item)
{
	struct queue_t *queue = q;
	struct queue_node *node;
	int ret = RET_OK;

	if (!queue)
		return -RET_E_NULL_POINTER;

	node = __get_idle_node(queue);
	if (!node) {
		ret = -RET_E_NO_MEMORY;
		goto exit;
	}

	node->item = item;
	ret = __push_node_to_queue(queue, node);
exit:
	return ret;
}

int pitcher_queue_pop(Queue q, unsigned long *item)
{
	struct queue_t *queue = q;
	struct queue_node *node;
	int ret = RET_OK;

	if (!queue)
		return -RET_E_NULL_POINTER;

	node = __pop_node_from_queue(queue);
	if (!node) {
		ret = -RET_E_EMPTY;
		goto exit;
	}

	if (item)
		*item = node->item;
	__put_idle_node(queue, node);
	ret = RET_OK;
exit:
	return ret;
}

void pitcher_queue_clear(Queue q, queue_callback func, void *arg)
{
	struct queue_t *queue = q;
	struct queue_node *node;
	struct queue_node *tmp;

	if (!queue)
		return;

	list_for_each_entry_safe(node, tmp, &queue->queue, list) {
		__remove_queue_node(queue, node);
		if (func)
			func(node->item, arg);
		__put_idle_node(queue, node);
	}
}

void pitcher_queue_enumerate(Queue q, queue_callback func, void *arg)
{
	struct queue_t *queue = q;
	struct queue_node *node;
	struct queue_node *tmp;

	if (!queue || !func)
		return;

	list_for_each_entry_safe(node, tmp, &queue->queue, list) {
		if (func(node->item, arg)) {
			__remove_queue_node(queue, node);
			__put_idle_node(queue, node);
		}
	}
}

int pitcher_queue_is_empty(Queue q)
{
	struct queue_t *queue = q;

	if (!queue)
		return -RET_E_NULL_POINTER;

	return list_empty(&queue->queue);
}

long pitcher_queue_count(Queue q)
{
	struct queue_t *queue = q;

	if (!queue)
		return -RET_E_EMPTY;

	return queue->count;
}

int pitcher_queue_find(Queue q, queue_callback func, void *arg,
		int (*compare)(unsigned long item, unsigned long key),
		unsigned long key)
{
	struct queue_t *queue = q;
	struct queue_node *node;
	struct queue_node *tmp;

	if (!q || !compare)
		return -RET_E_INVAL;

	list_for_each_entry_safe(node, tmp, &queue->queue, list) {
		if (!compare(node->item, key))
			continue;
		if (func && func(node->item, arg)) {
			__remove_queue_node(queue, node);
			__put_idle_node(queue, node);
		}
		return RET_OK;
	}

	return -RET_E_NOT_FOUND;
}

