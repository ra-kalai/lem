/*
 * This file is part of LEM, a Lua Event Machine.
 * Copyright 2011-2012 Emil Renner Berthing
 *
 * LEM is free software: you can redistribute it and/or
 * modify it under the terms of the GNU General Public License as
 * published by the Free Software Foundation, either version 3 of
 * the License, or (at your option) any later version.
 *
 * LEM is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with LEM.  If not, see <http://www.gnu.org/licenses/>.
 */

#ifndef MAXPENDING
#define MAXPENDING      50
#endif

static int
server_closed(lua_State *T)
{
	struct ev_io *w;

	luaL_checktype(T, 1, LUA_TUSERDATA);
	w = lua_touserdata(T, 1);
	lua_pushboolean(T, w->fd < 0);
	return 1;
}

static int
server_busy(lua_State *T)
{
	struct ev_io *w;

	luaL_checktype(T, 1, LUA_TUSERDATA);
	w = lua_touserdata(T, 1);
	lua_pushboolean(T, w->data != NULL);
	return 1;
}

static int
server_close(lua_State *T)
{
	struct ev_io *w;
	int ret;

	luaL_checktype(T, 1, LUA_TUSERDATA);
	w = lua_touserdata(T, 1);
	if (w->fd < 0)
		return io_closed(T);

	if (w->data != NULL) {
		lem_debug("interrupting listen");
		ev_io_stop(LEM_ w);
		lua_pushnil(w->data);
		lua_pushliteral(w->data, "interrupted");
		lem_queue(w->data, 2);
		w->data = NULL;
	}

	lem_debug("closing server..");

	ret = close(w->fd);
	w->fd = -1;
	if (ret)
		return io_strerror(T, errno);

	lua_pushboolean(T, 1);
	return 1;
}

static int
server_interrupt(lua_State *T)
{
	struct ev_io *w;

	luaL_checktype(T, 1, LUA_TUSERDATA);
	w = lua_touserdata(T, 1);
	if (w->data == NULL) {
		lua_pushnil(T);
		lua_pushliteral(T, "not busy");
		return 2;
	}

	lem_debug("interrupting listening");
	ev_io_stop(LEM_ w);
	lua_pushnil(w->data);
	lua_pushliteral(w->data, "interrupted");
	lem_queue(w->data, 2);
	w->data = NULL;

	lua_pushboolean(T, 1);
	return 1;
}

static int
server__accept(lua_State *T, struct ev_io *w)
{
	struct sockaddr client_addr;
	unsigned int client_addrlen;
	int sock;

	sock = accept(w->fd, &client_addr, &client_addrlen);
	if (sock < 0) {
		if (errno == EAGAIN || errno == ECONNABORTED)
			return 0;

		close(w->fd);
		w->fd = -1;
		lua_pushnil(T);
		lua_pushfstring(T, "error accepting connection: %s",
		                strerror(errno));
		return 2;
	}

	/* make the socket non-blocking */
	if (fcntl(sock, F_SETFL, O_NONBLOCK) < 0) {
		close(sock);
		lua_pushnil(T);
		lua_pushfstring(T, "error making socket non-blocking: %s",
		                strerror(errno));
		return 2;
	}

	stream_new(T, sock, lua_upvalueindex(1));
	return 1;
}

static void
server_accept_cb(EV_P_ struct ev_io *w, int revents)
{
	int ret;

	(void)revents;

	ret = server__accept(w->data, w);
	if (ret == 0)
		return;

	w->data = NULL;
	ev_io_stop(EV_A_ w);
	lem_queue(w->data, ret);
}

static int
server_accept(lua_State *T)
{
	struct ev_io *w;
	int ret;

	luaL_checktype(T, 1, LUA_TUSERDATA);
	w = lua_touserdata(T, 1);
	if (w->fd < 0)
		return io_closed(T);
	if (w->data != NULL)
		return io_busy(T);

	ret = server__accept(T, w);
	if (ret > 0)
		return ret;

	lua_settop(T, 1);
	w->cb = server_accept_cb;
	w->data = T;
	ev_io_start(LEM_ w);
	return lua_yield(T, 1);
}

static void
server_autospawn_cb(EV_P_ struct ev_io *w, int revents)
{
	lua_State *T = w->data;
	struct sockaddr client_addr;
	unsigned int client_addrlen;
	int sock;
	lua_State *S;

	(void)revents;

	/* dequeue the incoming connection */
	client_addrlen = sizeof(struct sockaddr);
	sock = accept(w->fd, &client_addr, &client_addrlen);
	if (sock < 0) {
		if (errno == EAGAIN || errno == ECONNABORTED)
			return;
		lua_pushnil(T);
		lua_pushfstring(T, "error accepting connection: %s",
		                strerror(errno));
		goto error;
	}

	/* make the socket non-blocking */
	if (fcntl(sock, F_SETFL, O_NONBLOCK) < 0) {
		close(sock);
		lua_pushnil(T);
		lua_pushfstring(T, "error making socket non-blocking: %s",
		                strerror(errno));
		goto error;
	}

	S = lem_newthread();

	/* copy handler function to thread */
	lua_pushvalue(T, 2);
	lua_xmove(T, S, 1);

	/* create stream */
	stream_new(T, sock, lua_upvalueindex(1));
	/* move stream to new thread */
	lua_xmove(T, S, 1);

	lem_queue(S, 1);
	return;

error:
	ev_io_stop(EV_A_ w);
	close(w->fd);
	w->fd = -1;
	w->data = NULL;
	lem_queue(T, 2);
}

static int
server_autospawn(lua_State *T)
{
	struct ev_io *w;

	luaL_checktype(T, 1, LUA_TUSERDATA);
	luaL_checktype(T, 2, LUA_TFUNCTION);

	w = lua_touserdata(T, 1);
	if (w->fd < 0)
		return io_closed(T);
	if (w->data != NULL)
		return io_busy(T);

	w->cb = server_autospawn_cb;
	w->data = T;
	ev_io_start(LEM_ w);

	lem_debug("yielding");

	/* yield server object, function and metatable*/
	lua_settop(T, 2);
	lua_pushvalue(T, lua_upvalueindex(1));
	return lua_yield(T, 3);
}
