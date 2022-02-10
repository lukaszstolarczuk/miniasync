// SPDX-License-Identifier: BSD-3-Clause
/* Copyright 2021-2022, Intel Corporation */

/*
 * main.c -- main of async implementation example
 */
#include <atomic>
#include <cassert>
#include <emmintrin.h>
#include <fcntl.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <memory>
#include <thread>

#include "libminiasync.h"

struct async_print_data {
	void *value;
	std::shared_ptr<std::atomic<int>> i;
	uint64_t we_are_waiting;
	std::thread t;
};

struct async_print_output {
	/* XXX dummy field to avoid empty struct */
	uintptr_t foo;
};

FUTURE(async_print_fut, struct async_print_data, struct async_print_output);

static enum future_state
async_print_impl(struct future_context *ctx, struct future_notifier *notifier)
{
	struct async_print_data *data = (struct async_print_data *)future_context_get_data(ctx);
	int i_value = data->i->load();

	// set our bool variable as pointer to monitor (for changes)
	notifier->poller.ptr_to_monitor = &data->we_are_waiting;
	if (i_value == 0) {
		printf("async print of future's value: %p\n", data->value);

		// we're passing the first state so i++
		data->i->fetch_add(1);
		return FUTURE_STATE_RUNNING;
	} else if (i_value == 1) {
		data->we_are_waiting = true;
		printf("We should enter here just once!\n");

		if (!data->t.joinable()) {
			data->t = std::thread([ptr = data->i, &data, notifier]{
				sleep(2); // long operation, we can do this in background

				// instead of this variable update (for poller) we could
				// use notifier wake here
				data->we_are_waiting = false;
				
				// we're passing the state, so i++
				ptr->fetch_add(1);
			});
		}
		return FUTURE_STATE_RUNNING;
	} else if (i_value == 2) {
		data->t.join(); // just to properly close thread

		printf("And we've passed through all states, we're done!\n");
		return FUTURE_STATE_COMPLETE;
	}
	assert(0);
}

static struct async_print_fut
async_print(void *value)
{
	struct async_print_fut future = {0};
	future.data.value = value;
	future.data.we_are_waiting = false;
	future.data.i = std::make_shared<std::atomic<int>>(0);

	FUTURE_INIT(&future, (future_task_fn)async_print_impl);

	return future;
}

int
main(int argc, char *argv[])
{
	size_t testbuf_size = strlen("testbuf");
	char *buf_a = strdup("testbuf");
	char *buf_b = strdup("otherbuf");
	struct runtime *r = runtime_new();

	struct vdm *thread_mover = vdm_new(vdm_descriptor_threads_polled());
	struct async_print_fut print_5 = async_print((void *)0x5);

	// instead of calling wait_*, we may manually check for updates
	// so we really print this message once: "We should enter here just once!"
	struct future_notifier ntfr;
	while(true) {
		auto ret = future_poll(&print_5.base, &ntfr);
		while (*ntfr.poller.ptr_to_monitor != false) {
			;
		}
		if (ret == FUTURE_STATE_COMPLETE)
			break;
	}
	// if we comment above (custom) polling loop and use:
	// runtime_wait(r, FUTURE_AS_RUNNABLE(&print_5));
	// we'll get "We should enter here just once!" multiple times

	/* we could also wait for multiple futures */
	struct async_print_fut print_6 = async_print((void *)0x6);
	
	struct future *prints[] = { &print_5.base, &print_6.base };
	// runtime_wait_multiple(r, prints, 2);


	/* finish */
	vdm_delete(thread_mover);

	printf("\n\n%s %s %d\n", buf_a, buf_b, memcmp(buf_a, buf_b, testbuf_size));

	free(buf_a);
	free(buf_b);

	return 0;
}
