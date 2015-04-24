/*
 * This file is part of LEM, a Lua Event Machine.
 * Copyright 2013 Emil Renner Berthing
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

struct unix_create {
	struct lem_async a;
	lua_State *T;
	const char *path;
	size_t len;
	int sock;
	int err;
};

static void
unix_connect_work(struct lem_async *a)
{
	struct unix_create *u = (struct unix_create *)a;
	struct sockaddr_un addr;
	int sock;

	sock = socket(AF_UNIX,
#ifdef SOCK_CLOEXEC
			SOCK_CLOEXEC |
#endif
			SOCK_STREAM, 0);
	if (sock < 0) {
		u->sock = -1;
		u->err = errno;
		return;
	}
#ifndef SOCK_CLOEXEC
	if (fcntl(sock, F_SETFD, FD_CLOEXEC) == -1) {
		u->sock = -1;
		u->err = errno;
		goto error;
	}
#endif
	addr.sun_family = AF_UNIX;
	memcpy(addr.sun_path, u->path, u->len+1);

	if (connect(sock, (struct sockaddr *)&addr,
				offsetof(struct sockaddr_un, sun_path) + u->len)) {
		u->sock = -2;
		u->err = errno;
		goto error;
	}

	/* make the socket non-blocking */
	if (fcntl(sock, F_SETFL, O_NONBLOCK) == -1) {
		u->sock = -1;
		u->err = errno;
		goto error;
	}

	u->sock = sock;
	return;

error:
	close(sock);
}

static void
unix_connect_reap(struct lem_async *a)
{
	struct unix_create *u = (struct unix_create *)a;
	lua_State *T = u->T;
	int sock = u->sock;

	if (sock >= 0) {
		free(u);

		stream_new(T, sock, 2);
		lem_queue(T, 1);
		return;
	}

	lua_pushnil(T);
	switch (-sock) {
	case 1:
		lua_pushfstring(T, "error creating socket: %s",
				strerror(u->err));
		break;
	case 2:
		lua_pushfstring(T, "error connecting to '%s': %s",
				u->path, strerror(u->err));
		break;
	}
	lem_queue(T, 2);
	free(u);
}

static int
unix_connect(lua_State *T)
{
	size_t len;
	const char *path = luaL_checklstring(T, 1, &len);
	struct unix_create *u;

	if (len >= UNIX_PATH_MAX)
		return luaL_argerror(T, 1, "path too long");

	u = lem_xmalloc(sizeof(struct unix_create));
	u->T = T;
	u->path = path;
	u->len = len;
	lem_async_do(&u->a, unix_connect_work, unix_connect_reap);

	lua_settop(T, 1);
	lua_pushvalue(T, lua_upvalueindex(1));
	return lua_yield(T, 2);
}

static void
unix_listen_work(struct lem_async *a)
{
	struct unix_create *u = (struct unix_create *)a;
	struct sockaddr_un addr;
	int sock;

	sock = socket(AF_UNIX,
#ifdef SOCK_CLOEXEC
			SOCK_CLOEXEC |
#endif
			SOCK_STREAM, 0);
	if (sock < 0) {
		u->sock = -1;
		u->err = errno;
		return;
	}
#ifndef SOCK_CLOEXEC
	if (fcntl(sock, F_SETFD, FD_CLOEXEC) == -1) {
		u->sock = -1;
		u->err = errno;
		goto error;
	}
#endif
	addr.sun_family = AF_UNIX;
	memcpy(addr.sun_path, u->path, u->len+1);

	if (bind(sock, (struct sockaddr *)&addr,
				offsetof(struct sockaddr_un, sun_path) + u->len)) {
		u->sock = -2;
		u->err = errno;
		goto error;
	}

	if (u->sock >= 0 && chmod(u->path, u->sock)) {
		u->sock = -3;
		u->err = errno;
		goto error;
	}

	if (listen(sock, u->err)) {
		u->sock = -4;
		u->err = errno;
		goto error;
	}

	/* make the socket non-blocking */
	if (fcntl(sock, F_SETFL, O_NONBLOCK) == -1) {
		u->sock = -1;
		u->err = errno;
		goto error;
	}

	u->sock = sock;
	return;

error:
	close(sock);
}

static void
unix_listen_reap(struct lem_async *a)
{
	struct unix_create *u = (struct unix_create *)a;
	lua_State *T = u->T;
	int sock = u->sock;

	if (sock >= 0) {
		free(u);
		server_new(T, sock, 2);
		lem_queue(T, 1);
		return;
	}

	lua_pushnil(T);
	switch (-sock) {
	case 1:
		lua_pushfstring(T, "error creating socket: %s",
				strerror(u->err));
		break;
	case 2:
		lua_pushfstring(T, "error binding to '%s': %s",
				u->path, strerror(u->err));
		break;
	case 3:
		lua_pushfstring(T, "error setting permissions on '%s': %s",
				u->path, strerror(u->err));
		break;
	case 4:
		lua_pushfstring(T, "error listening on '%s': %s",
				u->path, strerror(u->err));
		break;
	}
	lem_queue(T, 2);
	free(u);
}

static int
unix_listen(lua_State *T)
{
	size_t len;
	const char *path = luaL_checklstring(T, 1, &len);
	int perm = io_optperm(T, 2);
	int backlog = (int)luaL_optnumber(T, 3, MAXPENDING);
	struct unix_create *u;

	if (len >= UNIX_PATH_MAX)
		return luaL_argerror(T, 1, "path too long");

	u = lem_xmalloc(sizeof(struct unix_create));
	u->T = T;
	u->path = path;
	u->len = len;
	u->sock = perm;
	u->err = backlog;
	lem_async_do(&u->a, unix_listen_work, unix_listen_reap);

	lua_settop(T, 1);
	lua_pushvalue(T, lua_upvalueindex(1));
	return lua_yield(T, 2);
}

/*
 * io.unix.passfd_*
 */
struct pfhandle {
	struct lem_async a;
	lua_State *T;
	struct stream *s;
	struct ev_io w;
	int myfds[256];
	int num_fd;
	int ret;
};

static void
unix_passfd_send_work(struct lem_async *a)
{
	struct pfhandle *pf = (struct pfhandle *)a;
	struct stream *s = pf->s;

	/* make socket blocking */
	if (fcntl(s->w.fd, F_SETFL, 0) == -1) {
		pf->ret = errno;
		return;
	}

	struct msghdr msg;
	memset(&msg, 0, sizeof msg);

	int *myfds = pf->myfds;
	int num_fd = pf->num_fd;

	struct cmsghdr *cmsg;

	char nothing = 0;
	struct iovec    iov;
	iov.iov_base = &nothing;
	iov.iov_len = 1;

	msg.msg_iov = &iov;
	msg.msg_iovlen = 1;

	char buf[CMSG_SPACE(sizeof(int) * num_fd)];  /* ancillary data buffer */

	msg.msg_control = buf;
	msg.msg_controllen = sizeof buf;

	cmsg = CMSG_FIRSTHDR(&msg);
	cmsg->cmsg_level = SOL_SOCKET;
	cmsg->cmsg_type = SCM_RIGHTS;
	cmsg->cmsg_len = CMSG_LEN(sizeof(int) * num_fd);
	memcpy(CMSG_DATA(cmsg), myfds, num_fd * sizeof(int));
	msg.msg_controllen = cmsg->cmsg_len;

	int ret = sendmsg(s->w.fd, &msg, 0);

	lem_debug("sendmsg(%d, [%d fd]) = %d", s->w.fd, num_fd, ret);

	if (ret < 0) {
		pf->ret = errno;
		return ;
	} else if (ret == 0) {
		pf->ret = -1;
		return ;
	}

	/* make socket non-blocking again */
	if (fcntl(s->w.fd, F_SETFL, O_NONBLOCK) == -1) {
		pf->ret = errno;
		return;
	}
}

static void
unix_passfd_recv_work(struct lem_async *a)
{
	struct pfhandle *pf = (struct pfhandle *)a;
	struct stream *s = pf->s;

	/* make socket blocking */
	if (fcntl(s->r.fd, F_SETFL, 0) == -1) {
		pf->ret = errno;
		return;
	}

	int *myfds = pf->myfds;

	char            control[sizeof pf->myfds];
	struct msghdr   msg;
	struct cmsghdr  *cmsg;

	memset(&msg, 0, sizeof(msg));

	char            nothing;
	struct iovec    iov;

	iov.iov_base =  &nothing;
	iov.iov_len =   1;

	msg.msg_iov = &iov;
	msg.msg_iovlen = 1;

	/* control = ancillary message */
	msg.msg_control = control;
	msg.msg_controllen =  sizeof control;

	// cmsg = CMSG_FIRSTHDR(&msg);
	// cmsg->cmsg_len = CMSG_LEN(msg.msg_controllen);
	// cmsg->cmsg_level = SOL_SOCKET;
	// cmsg->cmsg_type = SCM_RIGHTS;

	int ret = recvmsg(s->r.fd, &msg, 0);

	lem_debug("recvmsg(%d) = %d", s->r.fd, ret);

	if (ret < 0) {
		pf->ret = errno;
		return ;
	} else if (ret == 0) {
		pf->ret = -1;
		return ;
	}

	cmsg = CMSG_FIRSTHDR(&msg);

	while (cmsg != NULL) {
		if (cmsg->cmsg_level == SOL_SOCKET &&
				cmsg->cmsg_type  == SCM_RIGHTS) {

			int num_fd = (cmsg->cmsg_len - CMSG_LEN(0))/sizeof(int);

			pf->num_fd = num_fd;
			memcpy(myfds, CMSG_DATA(cmsg), cmsg->cmsg_len - CMSG_LEN(0));
		} else {
			return ;
		}

		cmsg = CMSG_NXTHDR(&msg, cmsg);
		/* no more than one structure look to be fill
			 anyway so bail out */
		break ;
	}

	/* make socket non-blocking again */
	if (fcntl(s->r.fd, F_SETFL, O_NONBLOCK) == -1) {
		pf->ret = errno;
		return;
	}
}

static void
unix_passfd_send_reap(struct lem_async *a)
{
	struct pfhandle *pf = (struct pfhandle *)a;
	lua_State *T = pf->T;
	struct stream *s = pf->s;
	int ret;

	if (pf->ret == 0) {
		lua_pushinteger(T, pf->num_fd);
		ret = 1;
	} else {
		if (s->open)
			close(s->w.fd);
		s->open = 0;
		ret = io_strerror(T, pf->ret);
	}

	free(pf);

	s->w.data = NULL;
	lem_queue(T, ret);
}

static void
unix_passfd_recv_reap(struct lem_async *a)
{
	struct pfhandle *pf = (struct pfhandle *)a;
	lua_State *T = pf->T;
	struct stream *s = pf->s;
	int ret;

	if (pf->ret == 0) {
		if (pf->num_fd >= 1) {
			int i;

			lua_createtable(T, pf->num_fd, 0);

			for(i = 1; i <= pf->num_fd ; i++) {
				lua_pushinteger(T, pf->myfds[i-1]);
				lua_rawseti(T, -2, i);
			}
			ret = 1;
		} else {
			lua_pushnil(T);
			lua_pushstring(T, "no fd received, try again");
			ret = 2;
		}
	} else {
		if (s->open)
			close(s->r.fd);

		s->open = 0;
		ret = io_strerror(T, pf->ret);
	}

	free(pf);

	s->r.data = NULL;
	lem_queue(T, ret);
}

static void
unix_passfd_send_cb(EV_P_ ev_io *w, int revents)
{
	struct pfhandle *pf = w->data;
	(void) revents;

	lem_async_do(&pf->a, unix_passfd_send_work, unix_passfd_send_reap);
	ev_io_stop(EV_A_ w);
}

static void
unix_passfd_recv_cb(EV_P_ ev_io *w, int revents)
{
	struct pfhandle *pf = w->data;
	(void) revents;

	lem_async_do(&pf->a, unix_passfd_recv_work, unix_passfd_recv_reap);
	ev_io_stop(EV_A_ w);
}


static int
unix_passfd_send(lua_State *T)
{
	struct stream *s;
	struct pfhandle *pf;

	luaL_checktype(T, 1, LUA_TUSERDATA);
	s = lua_touserdata(T, 1);

	if (!s->open)
		return io_closed(T);
	if (s->w.data != NULL)
		return io_busy(T);

	int i, e;
	struct stream *os;
	struct ev_io *eio;

	pf = lem_xmalloc(sizeof *pf);
	int *myfds = pf->myfds;

	for (i = 1, e = lua_objlen(T, 2); i <= e; i++) {
		lua_rawgeti(T, 2, i);
		if (lua_isnumber(T, -1)) {
			int fd = lua_tointeger(T, -1);
			myfds[i-1] = fd;
		} else {
			luaL_checktype(T, -1, LUA_TUSERDATA);
			lua_getmetatable(T, -1);
			lua_pushstring(T, "kind");
			lua_rawget(T, -2);
			const char *kind = lua_tostring(T, -1);
			if (strcmp(kind, "stream") == 0) {
				lua_pop(T, 2);
				os = lua_touserdata(T, -1);
				myfds[i-1] = os->w.fd;
			} else if (strcmp(kind, "server")==0){
				lua_pop(T, 2);
				eio = lua_touserdata(T, -1);
				myfds[i-1] = eio->fd;
			}
		}
		lua_pop(T, 1);
	}

	pf->num_fd = i - 1;

	s->w.data = T;

	pf->T = T;
	pf->s = s;
	pf->ret = 0;

#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wstrict-aliasing"
	ev_io_init(&pf->w, unix_passfd_send_cb, pf->s->r.fd, EV_WRITE);
#pragma GCC diagnostic pop

	pf->w.data = pf;
	ev_io_start(LEM_ &pf->w);

	lua_settop(T, 2);

	return lua_yield(T, 2);
}

static int
unix_passfd_recv(lua_State *T) {
	struct stream *s;

	luaL_checktype(T, 1, LUA_TUSERDATA);

	s = lua_touserdata(T, 1);

	if (!s->open)
		return io_closed(T);
	if (s->w.data != NULL)
		return io_busy(T);

	struct pfhandle *pf = lem_xmalloc(sizeof *pf);

	s->r.data = T;

	pf->T = T;
	pf->s = s;
	pf->ret = 0;
	pf->num_fd = -1;

#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wstrict-aliasing"
	ev_io_init(&pf->w, unix_passfd_recv_cb, pf->s->r.fd, EV_READ);
#pragma GCC diagnostic pop

	pf->w.data = pf;
	ev_io_start(LEM_ &pf->w);

	lua_settop(T, 2);
	return lua_yield(T, 2);
}
