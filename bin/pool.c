/*
 * This file is part of LEM, a Lua Event Machine.
 * Copyright 2012 Emil Renner Berthing
 *
 * LEM is free software: you can redistribute it and/or modify it
 * under the terms of the GNU Lesser General Public License as
 * published by the Free Software Foundation, either version 3 of
 * the License, or (at your option) any later version.
 *
 * LEM is distributed in the hope that it will be useful, but
 * WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU Lesser General Public License for more details.
 *
 * You should have received a copy of the GNU Lesser General Public
 * License along with LEM.  If not, see <http://www.gnu.org/licenses/>.
 */

static unsigned int pool_jobs;
static unsigned int pool_min;
static unsigned int pool_max;
static unsigned int pool_threads;
static unsigned int pool_is_halting;
static time_t pool_delay;
static pthread_mutex_t pool_mutex;

static pthread_mutex_t pool_dlock;
#define pool_done_init()   pthread_mutex_init(&pool_dlock, NULL)
#define pool_done_lock()   pthread_mutex_lock(&pool_dlock)
#define pool_done_unlock() pthread_mutex_unlock(&pool_dlock)

static pthread_cond_t pool_cond;
static pthread_condattr_t pool_condattr;
static struct lem_async *pool_head;
static struct lem_async *pool_tail;
static struct lem_async *pool_done;
static struct ev_async pool_watch;

#define LEM_POOL_USED_CLOCK CLOCK_MONOTONIC

#ifdef __linux__
	#define LEM_POOL_FASTCLOCK CLOCK_MONOTONIC_COARSE
#elif __FreeBSD__
	#define LEM_POOL_FASTCLOCK CLOCK_MONOTONIC_FAST
#else
	#define LEM_POOL_FASTCLOCK CLOCK_MONOTONIC
#endif

static void *
pool_threadfunc(void *arg)
{
	struct lem_async *a;
	struct timespec ts;

	(void)arg;

	while (1) {
		clock_gettime(LEM_POOL_FASTCLOCK, &ts);
		ts.tv_sec  += pool_delay;

		pthread_mutex_lock(&pool_mutex);
		while ((a = pool_head) == NULL) {
retry_lock:
			if (pool_threads <= pool_min) {
				pthread_cond_wait(&pool_cond, &pool_mutex);
				continue;
			}

			if (pool_is_halting) {
				goto out;
			}

			if (pthread_cond_timedwait(&pool_cond, &pool_mutex, &ts)) {
				/* timeout */
				if (pool_threads > pool_min)
					goto out;
				else
					goto retry_lock;
			}

		}
		pool_head = a->next;
		pthread_mutex_unlock(&pool_mutex);

		lem_debug("Running job %p", a);
		a->work(a);
		lem_debug("Bye %p", a);

		pool_done_lock();
		a->next = pool_done;
		pool_done = a;
		pool_done_unlock();

		ev_async_send(LEM_ &pool_watch);
	}
out:
	pool_threads--;
	pthread_mutex_unlock(&pool_mutex);
	return NULL;
}

static void
pool_cb(EV_P_ struct ev_async *w, int revents)
{
	struct lem_async *a;
	struct lem_async *next;

	(void)revents;


	pool_done_lock();
	a = pool_done;
	pool_done = NULL;
	pool_done_unlock();

	for (; a; a = next) {
		pool_jobs--;
		next = a->next;
		if (a->reap)
			a->reap(a);
		else
			free(a);
	}

	if (pool_jobs == 0)
		ev_async_stop(EV_A_ w);
}

#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wstrict-aliasing"
static inline void
pool_watch_init(void)
{
	ev_async_init(&pool_watch, pool_cb);
}
#pragma GCC diagnostic pop

static int
pool_init(void)
{
	int ret;

	/*
	pool_jobs = 0;
	pool_min = 0;
	pool_threads = 0;
	*/
	pool_min = 1;
	pool_max = 8 /*INT_MAX*/;
	pool_delay = 3;
	/*
	pool_head = NULL;
	pool_tail = NULL;
	pool_done = NULL;
	*/

	pool_watch_init();

	ret = pthread_mutex_init(&pool_mutex, NULL);
	if (ret == 0)
		ret = pool_done_init();
	if (ret) {
		lem_log_error("error initializing lock: %s",
				strerror(ret));
		return -1;
	}

	ret = pthread_condattr_init(&pool_condattr);
	if (ret) {
		lem_log_error("error initializing cond_attr: %s",
				strerror(ret));
		return -1;
	}

	ret = pthread_condattr_setclock(&pool_condattr, LEM_POOL_USED_CLOCK);
	if (ret) {
		lem_log_error("error initializing clock on cond_attr: %s",
				strerror(ret));
		return -1;
	}

	ret = pthread_cond_init(&pool_cond, &pool_condattr);
	if (ret) {
		lem_log_error("error initializing cond: %s",
				strerror(ret));
		return -1;
	}

	return 0;
}

static void
pool_spawnthread(void)
{
	pthread_attr_t attr;
	pthread_t thread;
	int ret;

	ret = pthread_attr_init(&attr);
	if (ret)
		goto error;

	ret = pthread_attr_setdetachstate(&attr, PTHREAD_CREATE_DETACHED);
	if (ret) {
		pthread_attr_destroy(&attr);
		goto error;
	}

	//pthread_attr_setstacksize(&attr, PTHREAD_STACK_MIN*4);
	//if (ret) {
	//	pthread_attr_destroy(&attr);
	//	goto error;
	//}

	ret = pthread_create(&thread, &attr, pool_threadfunc, NULL);
	pthread_attr_destroy(&attr);
	if (ret)
		goto error;

	return;
error:
	lem_log_error("error spawning thread: %s", strerror(ret));
	lem_exit(EXIT_FAILURE);
}

void
lem_async_run(struct lem_async *a)
{
	int spawn = 0;

	if (pool_jobs == 0)
		ev_async_start(LEM_ &pool_watch);
	pool_jobs++;

	a->next = NULL;

	pthread_mutex_lock(&pool_mutex);
	if (pool_head == NULL) {
		pool_head = a;
		pool_tail = a;
	} else {
		pool_tail->next = a;
		pool_tail = a;
	}
	if (pool_is_halting == 0 &&
			pool_jobs > pool_threads && pool_threads < pool_max) {
		pool_threads++;
		spawn = 1;
	}
	pthread_mutex_unlock(&pool_mutex);
	pthread_cond_signal(&pool_cond);


	if (spawn)
		pool_spawnthread();
}

void
lem_async_config(int delay, int min, int max)
{
	int spawn;

	pool_delay = (time_t)delay;
	pool_min = min;
	pool_max = max;

	pthread_mutex_lock(&pool_mutex);
	spawn = min - pool_threads;
	if (spawn > 0)
		pool_threads = min;
	pthread_mutex_unlock(&pool_mutex);

	for (; spawn > 0; spawn--)
		pool_spawnthread();
}

static void
lem_exit_timeout(EV_P_ ev_timer *w, int revents) {
	(void)EV_A;
	(void)w;
	(void)revents;
	exit(exit_status);
}

static void
lem_pool_halt(EV_P_ struct ev_idle *w, int revents) {
	(void)w;
	(void)revents;
		pthread_mutex_lock(&pool_mutex);

		if (pool_threads == 0) {
			ev_idle_stop(EV_A_ w);
			ev_unloop(LEM_ EVBREAK_ALL);
		} else {
			pthread_cond_signal(&pool_cond);
		}
		pthread_mutex_unlock(&pool_mutex);
}


inline static void
lem_wait_pool_to_be_empty_upto_delay(double delay) {
	ev_now_update(LEM);

	lem_async_config(0, 0, pool_max);
	pool_is_halting = 1;

	if (delay == 0) {
		exit(exit_status);
	}

	ev_timer timer_exit_timeout;
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wstrict-aliasing"
	ev_timer_init (&timer_exit_timeout, lem_exit_timeout, delay, 0.);
#pragma GCC diagnostic pop
	ev_timer_start(LEM_ &timer_exit_timeout);

	ev_idle idle_watcher;
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wstrict-aliasing"
	ev_idle_init(&idle_watcher, lem_pool_halt);
#pragma GCC diagnostic pop
	ev_idle_start(LEM_ &idle_watcher);


	ev_loop(LEM_ 0);

	ev_timer_stop(LEM_ &timer_exit_timeout);
	pool_cb(LEM_ &pool_watch, 0);
}
