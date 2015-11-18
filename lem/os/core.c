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

#include <sys/wait.h>
#include <errno.h>
#include <string.h>

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

int
luaopen_lem_os_core(lua_State *L)
{
	/* create module table */
	lua_newtable(L);

	/* insert the os.waitpid function */
	lua_pushcfunction(L, os_waitpid);
	lua_setfield(L, -2, "waitpid");

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
