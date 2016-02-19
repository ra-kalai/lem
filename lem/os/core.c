/*
 * This file is part of LEM, a Lua Event Machine.
 * Copyright 2011-2013 Emil Renner Berthing
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

#include <unistd.h>
#include <stdlib.h>
#include <sys/wait.h>
#include <errno.h>
#include <string.h>

#ifdef __linux__
#include <sys/prctl.h>
#endif
#if defined(__FreeBSD__) || defined(__APPLE__)
#include <sys/types.h>
#endif

#include <lem-parsers.h>

struct os_waitpid_t {
	struct lem_async a;
	lua_State *T;
	pid_t pid;
	int options;
	int status;
	int err;
};

static void
os_waitpid_work(struct lem_async *a)
{
	struct os_waitpid_t *g = (struct os_waitpid_t *)a;
	int ret;

retry_waitpid:
	ret = waitpid(g->pid, &g->status, g->options);
	if (ret == -1) {
		if (errno == EINTR) goto retry_waitpid;
		g->err = errno;
	}
}

static void
os_waitpid_reap(struct lem_async *a)
{
	struct os_waitpid_t *g = (struct os_waitpid_t *)a;
	lua_State *T = g->T;

	if (g->err != 0) {
		lua_pushnil(T);
		lua_pushfstring(T, "waitpid error: %s",
				strerror(g->err));
		lem_queue(T, 2);
		return ;
	}

	int wifexited = WIFEXITED(g->status);
	int wifsignaled = WIFSIGNALED(g->status);
	int wifstopped = WIFSTOPPED(g->status);
	int wifcontinued = WIFCONTINUED(g->status);

	lua_newtable(T);

	lua_pushinteger(T, wifexited);
	lua_setfield(T, -2, "WIFEXITED");

	lua_pushinteger(T, wifsignaled);
	lua_setfield(T, -2, "WIFSIGNALED");

	lua_pushinteger(T, wifstopped);
	lua_setfield(T, -2, "WIFSTOPPED");

	lua_pushinteger(T, wifcontinued);
	lua_setfield(T, -2, "WIFCONTINUED");

	if (wifexited) {
		lua_pushinteger(T, WEXITSTATUS(g->status));
		lua_setfield(T, -2, "WEXITSTATUS");
	}

	if (wifsignaled) {
		lua_pushinteger(T, WTERMSIG(g->status));
		lua_setfield(T, -2, "WTERMSIG");

		lua_pushinteger(T, WCOREDUMP(g->status));
		lua_setfield(T, -2, "WCOREDUMP");
	}

	if (wifstopped) {
		lua_pushinteger(T, WSTOPSIG(g->status));
		lua_setfield(T, -2, "WSTOPSIG");
	}

	lem_queue(T, 1);
}

static int
os_waitpid(lua_State *T)
{
	int pid = luaL_checkinteger(T, 1);
	int options = luaL_checkinteger(T, 2);
	struct os_waitpid_t *g;

	g = lem_xmalloc(sizeof *g);
	g->T = T;
	g->err = 0;
	g->pid = pid;
	g->options = options;

	lem_async_do(&g->a, os_waitpid_work, os_waitpid_reap);

	lua_settop(T, 0);
	return lua_yield(T, 0);
}

static int
os_getpid(lua_State *T)
{
	lua_pushinteger(T, getpid());
	return 1;
}

static int
os_getppid(lua_State *T)
{
	lua_pushinteger(T, getppid());
	return 1;
}

static int
os_setpgrp(lua_State *T)
{
	lua_pushinteger(T, setpgrp());
	return 1;
}

static int
os_setproctitle(lua_State *T)
{
	size_t len;
	const char *proc_title = lua_tolstring(T, 1, &len);

#if defined(__FreeBSD__) || defined(__APPLE__)
	setproctitle(proc_title);
#else
	static int size;
	static char **__initial_main_environ = NULL;

	if (__initial_main_environ == NULL) {
		__initial_main_environ = __lem_main_environ;
	}

	int env_len = -1;

	if (__lem_main_environ)
		while (__lem_main_environ[++env_len]);

	if (env_len > 0) {
		size = __initial_main_environ[env_len-1] + strlen(__initial_main_environ[env_len-1]) - __lem_main_argv[0];
	} else {
		size = strlen(__lem_main_argv[0]);
		return 1;
	}

	/* just leak some memory in case someone didn't dup an environment variable */
	if (__lem_main_environ) {
		char **new_environ = lem_xmalloc((env_len+1)*sizeof(char *));
		new_environ[env_len] = NULL;
		int i = -1;

		while (__lem_main_environ[++i])
			new_environ[i] = strdup(__lem_main_environ[i]);

		__lem_main_environ = new_environ;
	}

	__lem_main_argv[0][size] = 0;
	snprintf(__lem_main_argv[0], size, "%s", proc_title);
                              
#ifndef __CYGWIN__
	prctl(PR_SET_NAME, __lem_main_argv[0], 0, 0, 0);
#endif

#endif
	return 0;
}

int
luaopen_lem_os_core(lua_State *L)
{
	/* create module table */
	lua_newtable(L);

	/* insert the os.waitpid function */
	lua_pushcfunction(L, os_waitpid);
	lua_setfield(L, -2, "waitpid");

	/* insert the os.getpid function */
	lua_pushcfunction(L, os_getpid);
	lua_setfield(L, -2, "getpid");

	/* insert the os.getppid function */
	lua_pushcfunction(L, os_getppid);
	lua_setfield(L, -2, "getppid");

	/* insert the os.setpgrp function */
	lua_pushcfunction(L, os_setpgrp);
	lua_setfield(L, -2, "setpgrp");

	/* insert the os.setproctitle function */
	lua_pushcfunction(L, os_setproctitle);
	lua_setfield(L, -2, "setproctitle");

	lua_newtable(L);
	lua_pushinteger(L, WCONTINUED);
	lua_setfield(L, -2, "WCONTINUED");

	lua_pushinteger(L, WNOHANG);
	lua_setfield(L, -2, "WNOHANG");

	lua_pushinteger(L, WUNTRACED);
	lua_setfield(L, -2, "WUNTRACED");
	
	lua_setfield(L, -2, "waitpid_options");

	return 1;
}
