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

#define _GNU_SOURCE
#include <stdlib.h>
#include <unistd.h>
#include <errno.h>
#include <string.h>
#include <assert.h>

#include <fcntl.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <arpa/inet.h>
#include <netinet/tcp.h>
#include <netdb.h>
#include <spawn.h>

#if defined(__FreeBSD__) || defined(__APPLE__)
#include <sys/un.h>
#include <sys/ucred.h>
#include <netinet/in.h>
extern char **__lem_main_environ;
#else
#include <sys/un.h>

#ifdef __CYGWIN__
#include <unistd.h>

ssize_t sendfile(int out_fd, int in_fd, off_t *offset, size_t count) {
	char *buff = malloc(count);

	off_t offset_to_restore;

	if (offset) {
		off_t set_offset;
		offset_to_restore = lseek(in_fd, 0, SEEK_CUR);
		set_offset = lseek(in_fd, *offset, SEEK_SET);
		if (set_offset == -1) {
			goto sendfile_fail_setoffset;
		}
	}

	ssize_t read_in = read(in_fd,buff, count);
	if (read_in <= 0) goto sendfile_fail_read;
	ssize_t write_out = write(out_fd, buff, read_in);

	if (offset != NULL) {
		lseek(in_fd, offset_to_restore, SEEK_SET);
		if (write_out > 0) {
			*offset += write_out;
		}
	} else {
		if (write_out > 0) {
			lseek(in_fd, SEEK_CUR, write_out);
		}
	}

	free(buff);
	return write_out;

	sendfile_fail_setoffset:
		free(buff);
		return -1;

	sendfile_fail_read:
		free(buff);
		return read_in;
}

#else
	#include <sys/sendfile.h>
#endif
#endif

#ifndef UNIX_PATH_MAX
#define UNIX_PATH_MAX  (sizeof ((struct sockaddr_un *)0)->sun_path)
#endif

#include <lem-parsers.h>

static int
io_closed(lua_State *T)
{
	lua_pushnil(T);
	lua_pushliteral(T, "closed");
	return 2;
}

static int
io_busy(lua_State *T)
{
	lua_pushnil(T);
	lua_pushliteral(T, "busy");
	return 2;
}

static int
io_strerror(lua_State *T, int err)
{
	lua_pushnil(T);
	lua_pushstring(T, strerror(err));
	return 2;
}

static int
io_optperm(lua_State *T, int idx)
{
	lua_Number n = luaL_optnumber(T, idx, -1);
	int mode = n;
	int octal;
	int i;

	if ((lua_Number)mode != n)
		goto error;
	if (mode == -1)
		return -1;
	if (mode < 0)
		goto error;

	octal = 0;
	for (i = 1; i <= 64; i *= 8) {
		int digit = mode % 10;
		if (digit > 7)
			goto error;

		octal += digit * i;
		mode /= 10;
	}
	if (mode != 0)
		goto error;

	return octal;
error:
	return luaL_argerror(T, idx, "invalid permissions");
}


static const int ip_famnumber[] = { AF_UNSPEC, AF_INET, AF_INET6 };
static const char *const ip_famnames[] = { "any", "ipv4", "ipv6", NULL };

#include "file.c"
#include "stream.c"
#include "server.c"
#include "tcp.c"
#include "udp.c"
#include "unix.c"

/*
 * io.open()
 */
struct open {
	struct lem_async a;
	lua_State *T;
	const char *path;
	int fd;
	int flags;
};

static void
io_open_work(struct lem_async *a)
{
	struct open *o = (struct open *)a;
	int fd;
	struct stat st;

	fd = open(o->path, o->flags
#ifdef O_CLOEXEC
			| O_CLOEXEC
#endif
			, o->fd >= 0 ? o->fd :
			S_IRUSR | S_IWUSR | S_IRGRP | S_IWGRP | S_IROTH | S_IWOTH);
	if (fd < 0) {
		o->flags = -errno;
		return;
	}
	if (
#ifndef O_CLOXEC
			fcntl(fd, F_SETFD, FD_CLOEXEC) == -1 ||
#endif
			fstat(fd, &st)) {
		o->flags = -errno;
		close(fd);
		return;
	}

	o->fd = fd;
	lem_debug("st.st_mode & S_IFMT = %o", st.st_mode & S_IFMT);
	switch (st.st_mode & S_IFMT) {
	case S_IFREG:
	case S_IFBLK:
		o->flags = 0;
		break;

	case S_IFCHR:
	case S_IFIFO:
		o->flags = 1;
		if (fcntl(fd, F_SETFL, O_NONBLOCK) == -1) {
			o->flags = -errno;
			close(fd);
		}
		break;

	default:
		o->flags = -EINVAL;
		break;
	}
}

static void
io_open_reap(struct lem_async *a)
{
	struct open *o = (struct open *)a;
	lua_State *T = o->T;
	int fd = o->fd;
	int ret = o->flags;

	lem_debug("ret = %d", ret);
	free(o);

	switch (ret) {
	case 0: file_new(T, fd, 2); break;
	case 1: stream_new(T, fd, 3); break;
	default:
		lem_queue(T, io_strerror(T, -ret));
		return;
	}

	lem_queue(T, 1);
}

static int
io_mode_to_flags(const char *mode)
{
	int omode;
	int oflags;

	switch (*mode++) {
	case 'r':
		omode = O_RDONLY;
		oflags = 0;
		break;
	case 'w':
		omode = O_WRONLY;
		oflags = O_CREAT | O_TRUNC;
		break;
	case 'a':
		omode = O_WRONLY;
		oflags = O_CREAT | O_APPEND;
		break;
	default:
		return -1;
	}

next:
	switch (*mode++) {
	case '\0':
		break;
	case '+':
		omode = O_RDWR;
		goto next;
	case 'b':
		/* this does nothing on *nix, but
		 * don't treat it as an error */
		goto next;
	case 'x':
		oflags |= O_EXCL;
		goto next;
	default:
		return -1;
	}

	return omode | oflags;
}

static int
io_open(lua_State *T)
{
	const char *path = luaL_checkstring(T, 1);
	int flags = io_mode_to_flags(luaL_optstring(T, 2, "r"));
	int perm = io_optperm(T, 3);
	struct open *o;

	if (flags < 0)
		return luaL_error(T, "invalid mode string");

	o = lem_xmalloc(sizeof(struct open));
	o->T = T;
	o->path = path;
	o->fd = perm;
	o->flags = flags;
	lem_async_do(&o->a, io_open_work, io_open_reap);

	lua_settop(T, 1);
	lua_pushvalue(T, lua_upvalueindex(1));
	lua_pushvalue(T, lua_upvalueindex(2));
	return lua_yield(T, 3);
}

/*
 * io.fromfd()
 */
struct fromfd {
	struct lem_async a;
	lua_State *T;
	int fd;
	int ret;
};

static int
io_socket_listening(int fd)
{
	int val;
	socklen_t len = sizeof(int);

	if (getsockopt(fd, SOL_SOCKET, SO_ACCEPTCONN, &val, &len) == 0 && val)
		return 1;

	return 0;
}

static void
io_fromfd_work(struct lem_async *a)
{
	struct fromfd *ff = (struct fromfd *)a;
	struct stat st;

	if (fstat(ff->fd, &st)) {
		ff->ret = -errno;
		return;
	}

	lem_debug("st.st_mode & S_IFMT = %o", st.st_mode & S_IFMT);
	switch (st.st_mode & S_IFMT) {
	case S_IFREG:
	case S_IFBLK:
		ff->ret = 0;
		break;

	case S_IFSOCK:
		if (io_socket_listening(ff->fd)) {
			ff->ret = 2;
			goto nonblock;
		}
		/* fallthrough */
	case S_IFCHR:
	case S_IFIFO:
		ff->ret = 1;
		goto nonblock;

	default:
		ff->ret = -EINVAL;
		break;
	}
	return;
nonblock:
	if (fcntl(ff->fd, F_SETFL, O_NONBLOCK) == -1)
		ff->ret = -errno;
}

static void
io_fromfd_reap(struct lem_async *a)
{
	struct fromfd *ff = (struct fromfd *)a;
	lua_State *T = ff->T;
	int fd = ff->fd;
	int ret = ff->ret;

	lem_debug("ret = %d", ret);
	free(ff);

	switch (ret) {
	case 0: file_new(T, fd, 1); break;
	case 1: stream_new(T, fd, 2); break;
	case 2: server_new(T, fd, 3, STREAM); break;
	default:
		lem_queue(T, io_strerror(T, -ret));
		return;
	}

	lem_queue(T, 1);
}

static int
io_fromfd(lua_State *T)
{
	int fd = luaL_checkinteger(T, 1);
	struct fromfd *ff;

	if (fd < 0)
		return luaL_argerror(T, 1, "invalid fd");

	ff = lem_xmalloc(sizeof(struct fromfd));
	ff->T = T;
	ff->fd = fd;
	lem_async_do(&ff->a, io_fromfd_work, io_fromfd_reap);

	lua_settop(T, 0);
	lua_pushvalue(T, lua_upvalueindex(1));
	lua_pushvalue(T, lua_upvalueindex(2));
	lua_pushvalue(T, lua_upvalueindex(3));
	return lua_yield(T, 3);
}

/*
 * io.popen()
 */
static const char *const io_popen_modes[] = { "r", "w", "rw", "3s", NULL };

static int
io_popen(lua_State *T)
{
	const char *cmd = luaL_checkstring(T, 1);
	int mode = luaL_checkoption(T, 2, "r", io_popen_modes);
	char *const argv[4] = { "/bin/sh", "-c", (char *)cmd, NULL };
	posix_spawn_file_actions_t fa;
	int fd[6];
	pid_t pid;
	int err = 0;

	switch (mode) {
	case 0: /* "r" */
		if (pipe(fd))
			return io_strerror(T, errno);
		posix_spawn_file_actions_init(&fa);
		posix_spawn_file_actions_adddup2(&fa, fd[1], 1);
		posix_spawn_file_actions_addclose(&fa, fd[1]);
		break;
	case 1: /* "w" */
		if (pipe(fd))
			return io_strerror(T, errno);
		err = fd[0];
		fd[0] = fd[1];
		fd[1] = err;
		posix_spawn_file_actions_init(&fa);
		posix_spawn_file_actions_adddup2(&fa, fd[1], 0);
		posix_spawn_file_actions_addclose(&fa, fd[1]);
		break;
	case 2: /* "rw" */
		if (socketpair(AF_UNIX, SOCK_STREAM, 0, fd))
			return io_strerror(T, errno);
		posix_spawn_file_actions_init(&fa);
		posix_spawn_file_actions_adddup2(&fa, fd[1], 0);
		posix_spawn_file_actions_adddup2(&fa, fd[1], 1);
		posix_spawn_file_actions_addclose(&fa, fd[1]);
		break;
	case 3: /* "3s" */
		{
			int i;
			for ( i = 0; i < 6; i += 2)
				if (pipe(fd+i)) {
					err = 1;
					break;
				}

			if (err) {
				int old_errno = errno;
				for ( i -= 2; i >= 0; i -= 2) {
					close((fd+i)[0]),
					close((fd+i)[1]);
				}

				return io_strerror(T, old_errno);
			}
		}

		err = fd[0];
		fd[0] = fd[1];
		fd[1] = err;

		posix_spawn_file_actions_init(&fa);
		posix_spawn_file_actions_adddup2(&fa, fd[1], 0);
		posix_spawn_file_actions_adddup2(&fa, fd[3], 1);
		posix_spawn_file_actions_adddup2(&fa, fd[5], 2);
		posix_spawn_file_actions_addclose(&fa, fd[1]);
		posix_spawn_file_actions_addclose(&fa, fd[3]);
		posix_spawn_file_actions_addclose(&fa, fd[5]);

		if (fcntl(fd[2], F_SETFD, FD_CLOEXEC) == -1 ||
				fcntl(fd[2], F_SETFL, O_NONBLOCK) == -1 ||
				fcntl(fd[4], F_SETFD, FD_CLOEXEC) == -1 ||
				fcntl(fd[4], F_SETFL, O_NONBLOCK) == -1) {
			err = errno;
			goto error;
		}

		break;
	}

	/* set our fd flags */
	if (fcntl(fd[0], F_SETFD, FD_CLOEXEC) == -1 ||
			fcntl(fd[0], F_SETFL, O_NONBLOCK) == -1) {
		err = errno;
		goto error;
	}

	err = posix_spawn(&pid, argv[0], &fa, NULL, argv, __lem_main_environ);
	lem_debug("err = %d, %s", err, strerror(err));
	if (err)
		goto error;

	posix_spawn_file_actions_destroy(&fa);

	if (mode == 3) {
		close(fd[1]);
		close(fd[3]);
		close(fd[5]);

		lua_newtable(T);
		stream_new(T, fd[0], lua_upvalueindex(1));
		lua_setfield(T, -2, "stdin");
		stream_new(T, fd[2], lua_upvalueindex(1));
		lua_setfield(T, -2, "stdout");
		stream_new(T, fd[4], lua_upvalueindex(1));
		lua_setfield(T, -2, "stderr");

		lua_pushinteger(T, pid);
	} else {
		close(fd[1]);

		stream_new(T, fd[0], lua_upvalueindex(1));
		lua_pushinteger(T, pid);
	}

	return 2;
error:
	posix_spawn_file_actions_destroy(&fa);
	close(fd[0]);
	close(fd[1]);
	if (mode == 3) {
		close(fd[2]);
		close(fd[3]);
		close(fd[4]);
		close(fd[5]);
	}
	return io_strerror(T, err);
}

/*
 * io.streamfile()
 */
struct streamfile {
	struct lem_async a;
	lua_State *T;
	const char *filename;
	int pipe[2];
	int file;
};

static void
io_streamfile_worker(struct lem_async *a)
{
	struct streamfile *s = (struct streamfile *)a;
	int file = s->file;
	int pipe = s->pipe[1];

#ifdef __FreeBSD__
	sendfile(file, pipe, 0, 0, NULL, NULL, SF_SYNC);
#else
#ifdef __APPLE__
	off_t len = 0;
	sendfile(file, pipe, 0, &len, NULL, 0);
#else
	while (sendfile(pipe, file, NULL, 2147483647) > 0);
#endif
#endif
	close(file);
	close(pipe);
}

static void
io_streamfile_open(struct lem_async *a)
{
	struct streamfile *s = (struct streamfile *)a;
	int file = open(s->filename,
#ifdef O_CLOEXEC
			O_CLOEXEC |
#endif
			O_RDONLY);
	if (file < 0) {
		s->file = -errno;
		return;
	}
#ifndef O_CLOEXEC
	if (fcntl(file, F_SETFD, FD_CLOEXEC) == -1) {
		s->file = -errno;
		goto err1;
	}
#endif

	if (socketpair(AF_UNIX,
#ifdef SOCK_CLOEXEC
				SOCK_CLOEXEC |
#endif
				SOCK_STREAM, 0, s->pipe)) {
		s->file = -errno;
		goto err1;
	}
	if (
#ifndef SOCK_CLOEXEC
			fcntl(s->pipe[1], F_SETFD, FD_CLOEXEC) == -1 ||
			fcntl(s->pipe[0], F_SETFD, FD_CLOEXEC) == -1 ||
#endif
			shutdown(s->pipe[0], SHUT_WR)) {
		s->file = -errno;
		goto err2;
	}
	if (fcntl(s->pipe[0], F_SETFL, O_NONBLOCK) == -1) {
		s->file = -errno;
		goto err2;
	}
	s->file = file;
	return;
err2:
	close(s->pipe[0]);
	close(s->pipe[1]);
err1:
	close(file);
}

static void
io_streamfile_reap(struct lem_async *a)
{
	struct streamfile *s = (struct streamfile *)a;
	lua_State *T = s->T;
	int ret = s->file;

	if (ret < 0) {
		free(s);
		lem_queue(T, io_strerror(T, -ret));
		return;
	}
	lem_debug("s->file = %d, s->pipe[0] = %d, s->pipe[1] = %d",
			ret, s->pipe[0], s->pipe[1]);

	lem_async_do(&s->a, io_streamfile_worker, NULL);

	stream_new(T, s->pipe[0], 2);
	lem_queue(T, 1);
}

static int
io_streamfile(lua_State *T)
{
	const char *filename = lua_tostring(T, 1);
	struct streamfile *s = lem_xmalloc(sizeof(struct streamfile));

	s->T = T;
	s->filename = filename;
	lem_async_do(&s->a, io_streamfile_open, io_streamfile_reap);

	lua_settop(T, 1);
	lua_pushvalue(T, lua_upvalueindex(1));
	return lua_yield(T, 2);
}

static void
push_stdstream(lua_State *L, int fd)
{
	struct stream *s;

	/* make the socket non-blocking */
	fcntl(fd, F_SETFL, O_NONBLOCK);

	s = stream_new(L, fd, -2);
	/* don't close this in __gc(), but make it blocking again */
	s->open = 2;
}

static int
io_set_collect_interval(lua_State *T)
{
	lua_Number v;
	v = luaL_checknumber(T, 1);
	ev_set_io_collect_interval(LEM_ v);
	lua_settop(T, 0);
	return 0;
}

struct ip_addr {
  struct sockaddr addr;
  int size;
};

static int
io_sendto(lua_State *T)
{
	int fd = lua_tointeger(T, 1);
  size_t len;
  const char *data = lua_tolstring(T, 2, &len);
	int flags = lua_tointeger(T, 3);
	struct ip_addr *u = lua_touserdata(T, 4);


  int ret = 0;

  if (u) {
    ret = sendto(fd, data, len, flags, &u->addr, u->size);
  } else {
    ret = sendto(fd, data, len, flags, NULL, 0);
  }

  if (ret < 0) {
    lua_pushnil(T);
    lua_pushfstring(T, "sendto error: %s", strerror(ret));
    return 2;
  }

  lua_pushinteger(T, ret);

	return 1;
}

static int
io_craftaddr(lua_State *T)
{
  size_t len;
  const char *ip = lua_tolstring(T, 1, &len);
	int port = lua_tointeger(T, 2);
	int family = luaL_checkoption(T, 3, "ipv4", ip_famnames);

  if (family == 1||family==2) {
    struct ip_addr *addr = lua_newuserdata(T, sizeof (struct ip_addr));

    if (family == 1) {
      struct sockaddr_in *ip4addr = (struct sockaddr_in*)addr;
      ip4addr->sin_family = AF_INET;
      ip4addr->sin_port = htons(port);
      inet_pton(AF_INET, ip, &ip4addr->sin_addr);
      addr->size = sizeof(*ip4addr);

    } else if (family == 2) {
      struct sockaddr_in6 *ip6addr = (struct sockaddr_in6*)addr;
      ip6addr->sin6_family = AF_INET6;
      ip6addr->sin6_port = htons(port);
      inet_pton(AF_INET6, ip, &ip6addr->sin6_addr);
      addr->size = sizeof(*ip6addr);
    }
  } else {
    return 0;
  }

	return 1;
}

int
luaopen_lem_io_core(lua_State *L)
{
	/* create module table */
	lua_newtable(L);

	/* create File metatable */
	lua_newtable(L);
	/* mt.__index = mt */
	lua_pushvalue(L, -1);
	lua_setfield(L, -2, "__index");
	/* mt.__gc = <file_gc> */
	lua_pushcfunction(L, file_gc);
	lua_setfield(L, -2, "__gc");
	/* mt.kind = <file> */
	lua_pushstring(L, "file");
	lua_setfield(L, -2, "kind");
	/* mt.closed = <file_closed> */
	lua_pushcfunction(L, file_closed);
	lua_setfield(L, -2, "closed");
	/* mt.close = <file_close> */
	lua_pushcfunction(L, file_close);
	lua_setfield(L, -2, "close");
	/* mt.readp = <file_readp> */
	lua_pushcfunction(L, file_readp);
	lua_setfield(L, -2, "readp");
	/* mt.write = <file_write> */
	lua_pushcfunction(L, file_write);
	lua_setfield(L, -2, "write");
	/* mt.size = <file_size> */
	lua_pushcfunction(L, file_size);
	lua_setfield(L, -2, "size");
	/* mt.seek = <file_seek> */
	lua_pushcfunction(L, file_seek);
	lua_setfield(L, -2, "seek");
	/* mt.lock = <file_lock> */
	lua_pushcfunction(L, file_lock);
	lua_setfield(L, -2, "lock");
	/* insert table */
	lua_setfield(L, -2, "File");

	/* create Stream metatable */
	lua_newtable(L);
	/* mt.__index = mt */
	lua_pushvalue(L, -1);
	lua_setfield(L, -2, "__index");
	/* mt.__gc = <stream_gc> */
	lua_pushcfunction(L, stream_gc);
	lua_setfield(L, -2, "__gc");
	/* mt.kind = <stream> */
	lua_pushstring(L, "stream");
	lua_setfield(L, -2, "kind");
	/* mt.closed = <stream_closed> */
	lua_pushcfunction(L, stream_closed);
	lua_setfield(L, -2, "closed");
	/* mt.close = <stream_close> */
	lua_pushcfunction(L, stream_close);
	lua_setfield(L, -2, "close");
	/* mt.readp = <stream_readp> */
	lua_pushcfunction(L, stream_readp);
	lua_setfield(L, -2, "readp");
	/* mt.write = <stream_write> */
	lua_pushcfunction(L, stream_write);
	lua_setfield(L, -2, "write");
#ifdef TCP_CORK
	/* mt.cork = <stream_cork> */
	lua_pushcfunction(L, stream_cork);
	lua_setfield(L, -2, "cork");
	/* mt.uncork = <stream_uncork> */
	lua_pushcfunction(L, stream_uncork);
	lua_setfield(L, -2, "uncork");
#endif
	/* mt.getpeer = <stream_getpeer> */
	lua_pushcfunction(L, stream_getpeer);
	lua_setfield(L, -2, "getpeer");
	/* mt.sendfile = <stream_sendfile> */
	lua_pushcfunction(L, stream_sendfile);
	lua_setfield(L, -2, "sendfile");
	/* mt.fileno = <stream_fileno> */
	lua_pushcfunction(L, stream_fileno);
	lua_setfield(L, -2, "fileno");
	/* insert io.stdin stream */
	push_stdstream(L, STDIN_FILENO);
	lua_setfield(L, -3, "stdin");
	/* insert io.stdout stream */
	push_stdstream(L, STDOUT_FILENO);
	lua_setfield(L, -3, "stdout");
	/* insert io.stderr stream */
	push_stdstream(L, STDERR_FILENO);
	lua_setfield(L, -3, "stderr");
	/* insert table */
	lua_setfield(L, -2, "Stream");

	/* create Server metatable */
	lua_newtable(L);
	/* mt.__index = mt */
	lua_pushvalue(L, -1);
	lua_setfield(L, -2, "__index");
	/* mt.__gc = <server_close> */
	lua_pushcfunction(L, server_close);
	lua_setfield(L, -2, "__gc");
	/* mt.kind = <server> */
	lua_pushstring(L, "server");
	lua_setfield(L, -2, "kind");
	/* mt.closed = <server_closed> */
	lua_pushcfunction(L, server_closed);
	lua_setfield(L, -2, "closed");
	/* mt.busy = <server_busy> */
	lua_pushcfunction(L, server_busy);
	lua_setfield(L, -2, "busy");
	/* mt.close = <server_close> */
	lua_pushcfunction(L, server_close);
	lua_setfield(L, -2, "close");
	/* mt.fileno = <server_fileno> */
	lua_pushcfunction(L, server_fileno);
	lua_setfield(L, -2, "fileno");
	/* mt.interrupt = <server_interrupt> */
	lua_pushcfunction(L, server_interrupt);
	lua_setfield(L, -2, "interrupt");
	/* mt.accept = <server_accept> */
	lua_getfield(L, -2, "Stream"); /* upvalue 1 = Stream */
	lua_pushcclosure(L, server_accept, 1);
	lua_setfield(L, -2, "accept");
	/* mt.autospawn = <server_autospawn> */
	lua_getfield(L, -2, "Stream"); /* upvalue 1 = Stream */
	lua_pushcclosure(L, server_autospawn, 1);
	lua_setfield(L, -2, "autospawn");
	/* insert table */
	lua_setfield(L, -2, "Server");

	/* insert open function */
	lua_getfield(L, -1, "File");   /* upvalue 1 = File   */
	lua_getfield(L, -2, "Stream"); /* upvalue 2 = Stream */
	lua_pushcclosure(L, io_open, 2);
	lua_setfield(L, -2, "open");
	/* insert the fromfd function */
	lua_getfield(L, -1, "File");   /* upvalue 1 = File   */
	lua_getfield(L, -2, "Stream"); /* upvalue 2 = Stream */
	lua_getfield(L, -3, "Server"); /* upvalue 3 = Server */
	lua_pushcclosure(L, io_fromfd, 3);
	lua_setfield(L, -2, "fromfd");
	/* insert popen function */
	lua_getfield(L, -1, "Stream"); /* upvalue 1 = Stream */
	lua_pushcclosure(L, io_popen, 1);
	lua_setfield(L, -2, "popen");
	/* insert streamfile function */
	lua_getfield(L, -1, "Stream"); /* upvalue 1 = Stream */
	lua_pushcclosure(L, io_streamfile, 1);
	lua_setfield(L, -2, "streamfile");

	/* create tcp table */
	lua_createtable(L, 0, 0);
	/* insert the connect function */
	lua_getfield(L, -2, "Stream"); /* upvalue 1 = Stream */
	lua_pushcclosure(L, tcp_connect, 1);
	lua_setfield(L, -2, "connect");
	/* insert the listen4 function */
	lua_getfield(L, -2, "Server"); /* upvalue 1 = Server */
	lua_pushcclosure(L, tcp_listen4, 1);
	lua_setfield(L, -2, "listen4");
	/* insert the listen6 function */
	lua_getfield(L, -2, "Server"); /* upvalue 1 = Server */
	lua_pushcclosure(L, tcp_listen6, 1);
	lua_setfield(L, -2, "listen6");
	/* insert the tcp table */
	lua_setfield(L, -2, "tcp");

	/* create udp table */
	lua_createtable(L, 0, 0);
	/* insert the connect function */
	lua_getfield(L, -2, "Stream"); /* upvalue 1 = Stream */
	lua_pushcclosure(L, udp_connect, 1);
	lua_setfield(L, -2, "connect");
	/* insert the listen4 function */
	lua_getfield(L, -2, "Server"); /* upvalue 1 = Server */
	lua_pushcclosure(L, udp_listen4, 1);
	lua_setfield(L, -2, "listen4");
	/* insert the listen6 function */
	lua_getfield(L, -2, "Server"); /* upvalue 1 = Server */
	lua_pushcclosure(L, udp_listen6, 1);
	lua_setfield(L, -2, "listen6");
	/* insert the udp table */
	lua_setfield(L, -2, "udp");

	/* create unix table */
	lua_createtable(L, 0, 0);
	/* insert the connect function */
	lua_getfield(L, -2, "Stream"); /* upvalue 1 = Stream */
	lua_pushcclosure(L, unix_connect, 1);
	lua_setfield(L, -2, "connect");
	/* insert the listen function */
	lua_getfield(L, -2, "Server"); /* upvalue 1 = Server */
	lua_pushcclosure(L, unix_listen, 1);
	lua_setfield(L, -2, "listen");

	/* insert the unix.passfd_* function */
	lua_pushcfunction(L, unix_passfd_send);
	lua_setfield(L, -2, "passfd_send");

	lua_pushcfunction(L, unix_passfd_recv);
	lua_setfield(L, -2, "passfd_recv");

	/* insert the unix table */
	lua_setfield(L, -2, "unix");

	/* set set_collect_interval function */
	lua_pushcfunction(L, io_set_collect_interval);
	lua_setfield(L, -2, "set_collect_interval");

	lua_pushcfunction(L, io_sendto);
	lua_setfield(L, -2, "sendto");

	lua_pushcfunction(L, io_craftaddr);
	lua_setfield(L, -2, "craftaddr");

	return 1;
}
