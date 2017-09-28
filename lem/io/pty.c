
struct pty_create_pair {
	struct lem_async a;
	lua_State *T;
	int master_fd;
	int slave_fd;
};

static void
pty_openpair_work(struct lem_async *a)
{
	struct pty_create_pair *u = (struct pty_create_pair *)a;
	int rc;

	u->master_fd = posix_openpt(O_RDWR);
	if (u->master_fd < 0) {
		u->master_fd = -1;
		u->slave_fd = errno;
		return ;
	}

	rc = grantpt(u->master_fd);
	if (rc) {
		u->master_fd = -2;
		u->slave_fd = errno;
		close(u->master_fd);
		return ;
	}

	rc = unlockpt(u->master_fd);
	if (rc) {
		u->master_fd = -2;
		u->slave_fd = errno;
		close(u->master_fd);
		return ;
	}

	u->slave_fd = open(ptsname(u->master_fd), O_RDWR);
	if (u->slave_fd < 0) {
		u->master_fd = -3;
		u->slave_fd = errno;
		close(u->master_fd);
		return ;
	}

	struct termios term_settings;

	rc = tcgetattr(u->slave_fd, &term_settings);

	cfmakeraw(&term_settings);
	tcsetattr(u->slave_fd, TCSANOW, &term_settings);

	fcntl(u->slave_fd, F_SETFL, O_NONBLOCK);
	fcntl(u->master_fd, F_SETFL, O_NONBLOCK);
}

static void
pty_openpair_reap(struct lem_async *a)
{
	struct pty_create_pair *u = (struct pty_create_pair *)a;
	lua_State *T = u->T;
	int master_fd = u->master_fd, slave_fd = u->slave_fd;

	free(u);

	if (master_fd < 0) {
		if (master_fd == -1) {
			lua_pushnil(T);
			lua_pushfstring(T, "error master tty: %s",
					strerror(slave_fd));
		} else if (master_fd == -2) {
			lua_pushnil(T);
			lua_pushfstring(T, "error grant master tty: %s",
					strerror(slave_fd));
		} else if (master_fd == -3) {
			lua_pushnil(T);
			lua_pushfstring(T, "error unlock master tty: %s",
					strerror(slave_fd));
		} else if (master_fd == -4) {
			lua_pushnil(T);
			lua_pushfstring(T, "error slave tty: %s",
					strerror(slave_fd));
		}
		lem_queue(T, 2);
		return ;
	}

	stream_new(T, master_fd, 1);
	stream_new(T, slave_fd, 1);

	lem_queue(T, 2);
}

	static int
pty_openpair(lua_State *T)
{
	struct pty_create_pair *u;

	u = lem_xmalloc(sizeof(struct pty_create_pair));

	lem_async_do(&u->a, pty_openpair_work, pty_openpair_reap);

	lua_pushvalue(T, lua_upvalueindex(1));

	u->T = T;

	return lua_yield(T, 1);
}
