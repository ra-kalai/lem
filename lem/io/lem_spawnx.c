#include <sys/syscall.h>
#include <sys/types.h>
#include <sys/wait.h>

#include <pthread.h>
#include <signal.h>

#define SIGALL_SET ((sigset_t *)(const unsigned long long [2]){ -1,-1 })
#define FDOP_CLOSE 1
#define FDOP_DUP2 2
#define FDOP_OPEN 4

struct fdop {
  struct fdop *next, *prev;
  int cmd, fd, srcfd, oflag;
  mode_t mode;
  char path[];
};

#define LEM_SPAWN_RESETIDS 1
#define LEM_SPAWN_SETPGROUP 2
#define LEM_SPAWN_SETSIGDEF 4
#define LEM_SPAWN_SETSIGMASK 8
#define LEM_SPAWN_SETSCHEDPARAM 16
#define LEM_SPAWN_SETSCHEDULER 32
#define LEM_SPAWN_SETSID 64
#define LEM_SPAWN_SCTTY 128

typedef struct {
  int __flags;
  pid_t __pgrp;
  sigset_t __def, __mask;
  int __prio, __pol, __pad[16];
} lem_spawnattr_t;

typedef struct {
  int __pad0[2];
  void *__actions;
  int __pad[16];
} lem_spawn_file_actions_t;

struct args {
  int p[2];
  sigset_t oldmask;
  const char *path;
  int (*exec)(const char *, char *const *, char *const *);
  const lem_spawn_file_actions_t *fa;
  const lem_spawnattr_t *restrict attr;
  char *const *argv, *const *envp;
};

static int __sys_dup2(int old, int new)
{
#ifdef SYS_dup2
  return syscall(SYS_dup2, old, new);
#else
  if (old==new) {
    int r = syscall(SYS_fcntl, old, F_GETFD);
    return r<0 ? r : old;
  } else {
    return syscall(SYS_dup3, old, new, 0);
  }
#endif
}

static int
lem_spawn_file_actions_init(lem_spawn_file_actions_t *fa)
{
  fa->__actions = 0;
  return 0;
}

static int
lem_spawn_file_actions_destroy(lem_spawn_file_actions_t *fa)
{
  struct fdop *op = fa->__actions, *next;
  while (op) {
    next = op->next;
    free(op);
    op = next;
  }
  return 0;
}

static int
lem_spawn_file_actions_addclose(lem_spawn_file_actions_t *fa, int fd)
{
  struct fdop *op = malloc(sizeof *op);
  if (!op) return ENOMEM;
  op->cmd = FDOP_CLOSE;
  op->fd = fd;
  if ((op->next = fa->__actions)) op->next->prev = op;
  op->prev = 0;
  fa->__actions = op;
  return 0;
}

static int
lem_spawn_file_actions_adddup2(lem_spawn_file_actions_t *fa, int srcfd, int fd)
{
  struct fdop *op = malloc(sizeof *op);
  if (!op) return ENOMEM;
  op->cmd = FDOP_DUP2;
  op->srcfd = srcfd;
  op->fd = fd;
  if ((op->next = fa->__actions)) op->next->prev = op;
  op->prev = 0;
  fa->__actions = op;
  return 0;
}

/*
static int
lem_spawn_file_actions_addopen(lem_spawn_file_actions_t *restrict fa, int fd, const char *restrict path, int flags, mode_t mode)
{
  struct fdop *op = malloc(sizeof *op + strlen(path) + 1);
  if (!op) return ENOMEM;
  op->cmd = FDOP_OPEN;
  op->fd = fd;
  op->oflag = flags;
  op->mode = mode;
  strcpy(op->path, path);
  if ((op->next = fa->__actions)) op->next->prev = op;
  op->prev = 0;
  fa->__actions = op;
  return 0;
}
*/

static int
lem_spawnattr_init(lem_spawnattr_t *attr)
{
  *attr = (lem_spawnattr_t){ 0 };
  return 0;
}

static int
lem_spawnattr_setflags(lem_spawnattr_t *attr, short flags)
{
  attr->__flags = flags;
  return 0;
}

static int
lem_spawnattr_destroy(lem_spawnattr_t *p)
{
  p = p;
  return 0;
}

static int
lem_spawnattr_setpgroup(lem_spawnattr_t *attr, pid_t pgrp)
{
  attr->__pgrp = pgrp;
  return 0;
}

static int
child(void *args_vp)
{
  int i, ret;
  struct sigaction sa = {0};
  struct args *args = args_vp;
  int p = args->p[1];
  const lem_spawn_file_actions_t *fa = args->fa;
  const lem_spawnattr_t *restrict attr = args->attr;
  sigset_t hset;

  close(args->p[0]);

  /* All signal dispositions must be either SIG_DFL or SIG_IGN
   * before signals are unblocked. Otherwise a signal handler
   * from the parent might get run in the child while sharing
   * memory, with unpredictable and dangerous results. To
   * reduce overhead, sigaction has tracked for us which signals
   * potentially have a signal handler. */
  sigfillset(&hset);
  for (i=1; i<_NSIG; i++) {
    if ((attr->__flags & LEM_SPAWN_SETSIGDEF)
         && sigismember(&attr->__def, i)) {
      sa.sa_handler = SIG_DFL;
    } else if (sigismember(&hset, i)) {
      if ((unsigned int)i-32<3U) {
        sa.sa_handler = SIG_IGN;
      } else {
        sigaction(i, 0, &sa);
        if (sa.sa_handler==SIG_IGN) continue;
        sa.sa_handler = SIG_DFL;
      }
    } else {
      continue;
    }
    sigaction(i, &sa, 0);
  }

  if (attr->__flags & LEM_SPAWN_SETPGROUP)
    if ((ret=syscall(SYS_setpgid, 0, attr->__pgrp)))
      goto fail;

  /* Use syscalls directly because the library functions attempt
   * to do a multi-threaded synchronized id-change, which would
   * trash the parent's state. */
  if (attr->__flags & LEM_SPAWN_RESETIDS)
    if ((ret=syscall(SYS_setgid, syscall(SYS_getgid))) ||
        (ret=syscall(SYS_setuid, syscall(SYS_getuid))) )
      goto fail;

  if (fa && fa->__actions) {
    struct fdop *op;
    int fd;
    for (op = fa->__actions; op->next; op = op->next);
    for (; op; op = op->prev) {
      /* It's possible that a file operation would clobber
       * the pipe fd used for synchronizing with the
       * parent. To avoid that, we dup the pipe onto
       * an unoccupied fd. */
      if (op->fd == p) {
        ret = syscall(SYS_dup, p);
        if (ret < 0) goto fail;
        syscall(SYS_close, p);
        p = ret;
      }
      switch(op->cmd) {
      case FDOP_CLOSE:
        if ((ret=syscall(SYS_close, op->fd)))
          goto fail;
        break;
      case FDOP_DUP2:
        if ((ret=__sys_dup2(op->srcfd, op->fd))<0)
          goto fail;
        break;
      case FDOP_OPEN:
        fd = syscall(SYS_open, op->path, op->oflag, op->mode);
        if ((ret=fd) < 0) goto fail;
        if (fd != op->fd) {
          if ((ret=__sys_dup2(fd, op->fd))<0)
            goto fail;
          syscall(SYS_close, fd);
        }
        break;
      }
    }
  }

  /* Close-on-exec flag may have been lost if we moved the pipe
   * to a different fd. We don't use F_DUPFD_CLOEXEC above because
   * it would fail on older kernels and atomicity is not needed --
   * in this process there are no threads or signal handlers. */
  syscall(SYS_fcntl, p, F_SETFD, FD_CLOEXEC);

  pthread_sigmask(SIG_SETMASK, (attr->__flags & POSIX_SPAWN_SETSIGMASK)
    ? &attr->__mask : &args->oldmask, 0);

  if (attr->__flags & LEM_SPAWN_SETSID)
    if ((ret=setsid()) == -1)
      goto fail;

  if (attr->__flags & LEM_SPAWN_SCTTY)
    if ((ret = ioctl(0, TIOCSCTTY, 1)))
      goto fail;

  args->exec(args->path, args->argv, args->envp);
  ret = -errno;

fail:
  /* Since sizeof errno < PIPE_BUF, the write is atomic. */
  ret = -ret;
  if (ret) while (write(p, &ret, sizeof ret) < 0);
  _exit(127);
}


static int
lem_posix_spawnx(pid_t *restrict res, const char *restrict path,
  int (*exec)(const char *, char *const *, char *const *),
  const lem_spawn_file_actions_t *fa,
  const lem_spawnattr_t *restrict attr,
  char *const argv[restrict], char *const envp[restrict])
{
  pid_t pid;
  char stack[1024];
  int ec=0, cs;
  struct args args;

  if (pipe2(args.p, O_CLOEXEC))
    return errno;

  pthread_setcancelstate(PTHREAD_CANCEL_DISABLE, &cs);

  args.path = path;
  args.exec = exec;
  args.fa = fa;
  args.attr = attr ? attr : &(const lem_spawnattr_t){0};
  args.argv = argv;
  args.envp = envp;
  pthread_sigmask(SIG_BLOCK, SIGALL_SET, &args.oldmask);

  //pid = fork();
  //if (pid == 0) {
  //  child(&args);
  //}
  pid = clone(child, stack+sizeof stack,
    CLONE_VM|CLONE_VFORK|SIGCHLD, &args);
  close(args.p[1]);

  if (pid > 0) {
    if (read(args.p[0], &ec, sizeof ec) != sizeof ec) ec = 0;
    else waitpid(pid, &(int){0}, 0);
  } else {
    ec = -pid;
  }

  close(args.p[0]);

  if (!ec && res) *res = pid;

  pthread_sigmask(SIG_SETMASK, &args.oldmask, 0);
  pthread_setcancelstate(cs, 0);

  return ec;
}

static int
lem_spawnp(pid_t *restrict res, const char *restrict file,
  const lem_spawn_file_actions_t *fa,
  const lem_spawnattr_t *restrict attr,
  char *const argv[restrict], char *const envp[restrict])
{
  return lem_posix_spawnx(res, file, execvpe, fa, attr, argv, envp);
}
