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

struct server_io {
  ev_io w;
  enum {STREAM, DATAGRAM} server_kind;
};

static struct server_io *
server_new(lua_State *T, int fd, int mt, int kind)
{
	/* create userdata and set the metatable */
	struct server_io *ret = lua_newuserdata(T, sizeof(*ret));
	lua_pushvalue(T, mt);
	lua_setmetatable(T, -2);

	/* initialize userdata */
	ev_io_init(&ret->w, NULL, fd, EV_READ);
	ret->w.data = NULL;
	ret->server_kind = kind;

	return ret;
}

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
server__accept(lua_State *T, struct ev_io *w, int mt)
{
#ifdef SOCK_CLOEXEC
	int sock = accept4(w->fd, NULL, NULL, SOCK_CLOEXEC | SOCK_NONBLOCK);
#else
	int sock = accept(w->fd, NULL, NULL);
#endif

	if (sock < 0) {
		switch (errno) {
		case EAGAIN: case EINTR: case ECONNABORTED:
		case ENETDOWN: case EPROTO: case ENOPROTOOPT:
		case EHOSTDOWN:
#ifdef ENONET
		case ENONET:
#endif
		case EHOSTUNREACH: case EOPNOTSUPP: case ENETUNREACH:
			return 0;
		}
		lua_pushnil(T);
		lua_pushfstring(T, "error accepting connection: %s",
		                strerror(errno));
		return 2;
	}
#ifndef SOCK_CLOEXEC
	/* set FD_CLOEXEC and make the socket non-blocking */
	if (fcntl(sock, F_SETFD, FD_CLOEXEC) == -1 ||
			fcntl(sock, F_SETFL, O_NONBLOCK) == -1) {
		close(sock);
		lua_pushnil(T);
		lua_pushfstring(T, "error setting socket flags: %s",
		                strerror(errno));
		return 2;
	}
#endif
	stream_new(T, sock, mt);
	return 1;
}

static void
server_accept_cb(EV_P_ struct ev_io *w, int revents)
{
	int ret;

	(void)revents;

	ret = server__accept(w->data, w, 2);
	if (ret == 0)
		return;

	w->data = NULL;
	ev_io_stop(EV_A_ w);
	if (ret == 2) {
		close(w->fd);
		w->fd = -1;
	}
	lem_queue(w->data, ret);
}

static int
server_accept(lua_State *T)
{
	struct ev_io *w;

	luaL_checktype(T, 1, LUA_TUSERDATA);
	w = lua_touserdata(T, 1);
	if (w->fd < 0)
		return io_closed(T);
	if (w->data != NULL)
		return io_busy(T);

	switch (server__accept(T, w, lua_upvalueindex(1))) {
	case 1:
		return 1;
	case 2:
		close(w->fd);
		w->fd= -1;
		return 2;
	}

	w->cb = server_accept_cb;
	w->data = T;
	ev_io_start(LEM_ w);
	lua_settop(T, 1);
	lua_pushvalue(T, lua_upvalueindex(1));
	return lua_yield(T, 2);
}

static void
server_autospawn_cb(EV_P_ struct ev_io *w, int revents)
{
	lua_State *T = w->data;
	int sock;
	lua_State *S;

	(void)revents;

	for(;;) {
	/* dequeue the incoming connection */
#ifdef SOCK_CLOEXEC
		sock = accept4(w->fd, NULL, NULL, SOCK_CLOEXEC | SOCK_NONBLOCK);
#else
		sock = accept(w->fd, NULL, NULL);
#endif
		if (sock < 0) {
			switch (errno) {
				case EAGAIN: case EINTR: case ECONNABORTED:
				case ENETDOWN: case EPROTO: case ENOPROTOOPT:
				case EHOSTDOWN:
#ifdef ENONET
				case ENONET:
#endif
				case EHOSTUNREACH: case EOPNOTSUPP: case ENETUNREACH:
					return;
			}
			lua_pushnil(T);
			lua_pushfstring(T, "error accepting connection: %s",
					strerror(errno));
			goto error;
		}
#ifndef SOCK_CLOEXEC
		/* set FD_CLOEXEC and make the socket non-blocking */
		if (fcntl(sock, F_SETFD, FD_CLOEXEC) == -1 ||
				fcntl(sock, F_SETFL, O_NONBLOCK) == -1) {
			close(sock);
			lua_pushnil(T);
			lua_pushfstring(T, "error setting socket flags: %s",
					strerror(errno));
			goto error;
		}
#endif
		S = lem_newthread();

		/* copy handler function */
		lua_pushvalue(T, 2);
		/* create stream */
		stream_new(T, sock, 3);
		/* move function and stream to new thread */
		lua_xmove(T, S, 2);

		lem_queue(S, 1);
	}
	return;

error:
	ev_io_stop(EV_A_ w);
	close(w->fd);
	w->fd = -1;
	w->data = NULL;
	lem_queue(T, 2);
}

static void
server_recvfrom_cb(EV_P_ struct ev_io *w, int revents)
{
	lua_State *T = w->data;
	int ret;
	lua_State *S;

	(void)revents;

	static char payload_buf[1<<16];
	struct sockaddr_storage client_addr;
	char ip_address[INET6_ADDRSTRLEN];
	socklen_t client_addr_len = sizeof(client_addr);

	for(;;) {
		ret = recvfrom(w->fd, payload_buf, sizeof payload_buf, 0, (struct sockaddr*)&client_addr, &client_addr_len);
		/* dequeue the incoming connection */
		if (ret < 0) {
			switch (errno) {
				case EAGAIN: case EINTR: case ECONNABORTED:
				case ENETDOWN: case EPROTO: case ENOPROTOOPT:
				case EHOSTDOWN:
#ifdef ENONET
				case ENONET:
#endif
				case EHOSTUNREACH: case EOPNOTSUPP: case ENETUNREACH:
					return;
			}
			lua_pushnil(T);
			lua_pushfstring(T, "error while receiving an udp packet: %s",
					strerror(errno));
			goto error;
		}

		S = lem_newthread();

		/* copy handler function */
		lua_pushvalue(T, 2);

		/* push datagram */
		lua_pushlstring(T, payload_buf, ret);

		if (client_addr.ss_family == AF_INET) {
			lua_pushstring(T, inet_ntop(client_addr.ss_family,
																	&((struct sockaddr_in*)&client_addr)->sin_addr.s_addr,
																	ip_address,
																	sizeof ip_address));
			lua_pushinteger(T, ((struct sockaddr_in*)&client_addr)->sin_port);
		} else if (client_addr.ss_family == AF_INET6) {
			lua_pushstring(T, inet_ntop(client_addr.ss_family,
																	&((struct sockaddr_in6*)&client_addr)->sin6_addr,
																	ip_address,
																	sizeof ip_address));
			lua_pushinteger(T, ((struct sockaddr_in6*)&client_addr)->sin6_port);
		}

		/* move function and datagram and ip and port to new thread */
		lua_xmove(T, S, 4);

		lem_queue(S, 3);
	}
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
	struct server_io *io_server;

	luaL_checktype(T, 1, LUA_TUSERDATA);
	luaL_checktype(T, 2, LUA_TFUNCTION);

	io_server = lua_touserdata(T, 1);
	w = &io_server->w;

	if (w->fd < 0)
		return io_closed(T);
	if (w->data != NULL)
		return io_busy(T);

	if (io_server->server_kind == STREAM) {
		w->cb = server_autospawn_cb;
	} else {
		w->cb = server_recvfrom_cb;
	}

	w->data = T;
	ev_io_start(LEM_ w);

	lem_debug("yielding");

	/* yield server object, function and metatable*/
	lua_settop(T, 2);
	lua_pushvalue(T, lua_upvalueindex(1));
	return lua_yield(T, 3);
}
