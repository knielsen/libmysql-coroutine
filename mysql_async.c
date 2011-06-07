/*
  MySQL non-blocking client library functions.
*/

extern int mysql_get_socket_fd(const MYSQL *mysql);


typedef enum {
  MYSQL_WAIT_READ= 1,
  MYSQL_WAIT_WRITE= 2,
  MYSQL_WAIT_TIMEOUT= 4
} MYSQL_ASYNC_STATUS;

struct mysql_async_context {
  /*
    This is set to the value that should be returned from foo_start() or
    foo_cont() when a call is suspended.
    It is also set to the event(s) that triggered when a suspended call is
    resumed, eg. whether we woke up due to connection completed or timeout
    in mysql_real_connect_cont().
  */
  MYSQL_ASYNC_STATUS ret_status;
  /*
    This is set to the result of the whole asynchronous operation when it
    completes. It uses a union, as different calls have different return
    types.
  */
  union {
    MYSQL *r_mysql;
    int *r_int;
  } ret_result;
  /*
    The timeout value, for suspended calls that need to wake up on a timeout
    (eg. mysql_real_connect_start().
  */
  uint timeout_value;
  /*
    This flag is set when we are executing inside some asynchronous call
    foo_start() or foo_cont(). It is used to decide whether to use the
    synchronous or asynchronous version of calls that may block such as
    recv().

    Note that this flag is not set when a call is suspended, eg. after
    returning from foo_start() and before re-entering foo_cont().
  */
  my_bool async_call_active;
  /*
    This flag is set when an asynchronous operation is in progress, but
    suspended. Ie. it is set when foo_start() or foo_cont() returns because
    the operation needs to block, suspending the operation.

    It is used to give an error (rather than crash) if the application
    attempts to call some foo_cont() method when no suspended operation foo is
    in progress.
  */
  my_bool suspended;
  /*
    This is used to save the execution contexts so that we can suspend an
    operation and switch back to the application context, to resume the
    suspended context later when the application re-invokes us with
    foo_cont().
  */
  my_context async_context;
};


struct my_real_connect_params {
  MYSQL *mysql;
  const char *host;
  const char *user;
  const char *passwd;
  const char *db;
  unsigned int port;
  const char *unix_socket;
  unsigned long client_flags;
}

static void
mysql_real_connect_start_internal(void *d)
{
  struct my_real_connect_params *parms;
  MYSQL *ret;
  struct mysql_async_context *b;

  parms= (struct my_real_connect_params *)d;
  b= parms->mysql->async_context;

  ret= mysql_real_connect(parms->mysql, parms->host, parms->user, parms->passwd,
                          parms->db, parms->port, parms->unix_socket,
                          parms->client_flags);
  b->ret_result.r_mysql= ret;
  b->ret_status= 0;
}

MYSQL_ASYNC_STATUS
mysql_real_connect_start(MYSQL **ret, MYSQL *mysql, const char *host,
                         const char *user, const char *passwd, const char *db,
                         unsigned int port, const char *unix_socket,
                         unsigned long client_flags)
{
  int res;
  struct mysql_async_context *b;
  struct my_real_connect_params parms;

  b= mysql->async_context;
  if (!b)
  {
    if (!(b= mysql->async_context= (struct mysql_async_context *)
          my_malloc(sizeof(*b), MYF(MY_ZEROFILL))))
    {
      set_mysql_error(mysql, CR_OUT_OF_MEMORY, unknown_sqlstate);
      *ret= NULL;
      return 0;
    }
  }
  parms.mysql= mysql;
  parms.host= host;
  parms.user= user;
  parms.passwd= passwd;
  parms.db= db;
  parms.port= port;
  parms.unix_socket= unix_socket;
  parms.client_flags= client_flags;

  b->async_call_active= 1;
  /* ToDo: Need to properly handle allocation of stack. */
  res= my_context_spawn(&b->async_context, mysql_real_connect_start_internal,
                        parms, malloc(STACK_SIZE), STACK_SIZE);
  b->async_call_active= 0;
  if (res < 0)
  {
    set_mysql_error(mysql, CR_OUT_OF_MEMORY, unknown_sqlstate);
    b->suspended= 0;
    return NULL;
  }
  else if (res > 0)
  {
    /* Suspended. */
    b->suspended= 1;
    return b->ret_status;
  }
  else
  {
    /* Finished. */
    b->suspended= 0;
    *ret= b->ret_result.r_mysql;
    return 0;
  }
}

MYSQL_ASYNC_STATUS
mysql_real_connect_cont(MYSQL **ret, MYSQL *mysql,
                        MYSQL_ASYNC_STATUS ready_status)
{
  int res;
  struct mysql_async_context *b;

  b= mysql->async_context;
  if (!b || !b->suspended)
  {
    set_mysql_error(mysql, "No suspended call is active", unknown_sqlstate);
    *ret= NULL;
    return 0;
  }

  b->async_call_active= 1;
  b->ret_status= ready_status;
  res= my_context_continue(&b->async_context);
  b->async_call_active= 0;
  if (res < 0)
  {
    set_mysql_error(mysql, CR_OUT_OF_MEMORY, unknown_sqlstate);
    b->suspended= 0;
    return NULL;
  }
  else if (res > 0)
  {
    /* Suspended. */
    return b->ret_status;
  }
  else
  {
    /* Finished. */
    b->suspended= 0;
    *ret= b->ret_result.r_mysql;
    return 0;
  }
}

int
my_connect_async(mysql_async_context *b, my_socket fd, const struct sockaddr *name, uint namelen, uint timeout)
{
  int res;
  int flags;
  socklen_t *s_err_size;
  /* Set fd non-blocking. */
  /*
    Note: This can be done similarly on Windows, except we need ioctlsocket()
    to set non-blocking and a second conenct() call to get result of connect attempt.
  */
  flags = fcntl(fd, F_GETFL, 0);
  fcntl(fd, F_SETFL, flags | O_NONBLOCK);

  /*
    Start to connect asynchronously.
    If this will block, we suspend the call and return control to the
    application context. The application will then resume us when the socket
    polls ready for write, indicating that the connection attempt completed.
  */
  res= connect(fd, addr, len);
  if (res < 0)
  {
    if (errno != EINPROGRESS && errno != EALREADY)
      return res;
    b->timeout_value= timeout;
    b->ret_status= MYSQL_WAIT_WRITE | MYSQL_WAIT_TIMEOUT;
    my_context_yield(&b->async_context);
    if (b->ret_status & MYSQL_WAIT_TIMEOUT)
      return -1;

    s_err_size= sizeof(int);
    if (getsockopt(fd, SOL_SOCKET, SO_ERROR, (char*) &res, &s_err_size) != 0)
      return -1;
    if (res)
    {
      errno= res;
      return -1;
    }
  }
  return res;
}

ssize_t
my_recv_async(mysql_async_context *b, int fd, unsigned char *buf, size_t size)
{
  ssize_t res;

  for (;;)
  {
    res= recv(fd, buf, size, MSG_DONTWAIT);
    if (res >= 0 || errno != EAGAIN)
      return res;
    b->ret_status= MYSQL_WAIT_READ;
    my_context_yield(&b->async_context);
  }
}

ssize_t
my_send_async(mysql_async_context *b, int fd, unsigned char *buf, size_t size)
{
  ssize_t res;

  for (;;)
  {
    res= send(fd, buf, size, MSG_DONTWAIT);
    if (res >= 0 || errno != EAGAIN)
      return res;
    b->ret_status= MYSQL_WAIT_WRITE;
    my_context_yield(&b->async_context);
  }
}

uint
mysql_get_timeout_value(const MYSQL *mysql)
{
  if (mysql->async_context && mysql->async_context->active)
    return mysql->async_context->timeout_value;
  else
    return 0;
}
