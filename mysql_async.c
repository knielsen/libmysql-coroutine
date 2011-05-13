/*
  MySQL non-blocking client library functions.
*/

extern ssize_t (*vio_external_read_hook)(int fd, void *buf, size_t count);
extern ssize_t (*vio_external_write_hook)(int fd, void *buf, size_t count);
extern int (*vio_external_connect_hook)(int, const struct sockaddr *, socklen_t);
extern int mysql_get_socket_fd(const MYSQL *mysql);


typedef enum {
  MYSQL_WAIT_READ= 1,
  MYSQL_WAIT_WRITE= 2,
  MYSQL_WAIT_TIMEOUT= 4
} MYSQL_ASYNC_STATUS;


/*
  ToDo: Implement my_async_connect_func(), it must set the socket non-blocking
  for future operation.
*/
static int
my_async_connect_func(int fd, const struct sockaddr *addr, socklen_t len)
{
  int res;

  /* ToDo: Set fd non-blocking. */

  /* ToDo: Hm, how do I get hold of my mysql (and/or my_blob) object? */

  res= connect(fd, addr, len);
  while (res < 0 && (errno == EINPROGRESS || errno == EALREADY))
  {
    my_context_yield(&b->async_context, MYSQL_WAIT_WRITE);
    res= connect(fd, addr, len);
  }
  return res;
}

static void
mysql_real_connect_start_internal(void *d)
{
  struct my_real_connect_params *parms;
  MYSQL *ret;
  struct my_blob *b;

  parms= (struct my_real_connect_params *)d;
  b= (struct my_blob *)parms->mysql->opaque_user_ptr;

  ret= mysql_real_connect(parms->mysql, parms->host, parms->user, parms->passwd,
                          parms->db, parms->port, parms->unix_socket,
                          parms->client_flags);
  b->async_call_active= 0;
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
  strut my_blob *b;
  struct my_real_connect_params parms;

  vio_external_read_hook= my_async_read_func;
  vio_external_write_hook= my_async_write_func;
  vio_external_connect_hook= my_async_connect_func;

  /*
    For now we use opaque user pointer in MYSQL struct.
    Eventually, we should extend the struct and integrate this into
    libmysqlclient source code
  */
  b= malloc(sizeof (*b));
  mysql->opaque_user_ptr= b;
  parms->mysql= mysql;
  parms->host= host;
  parms->user= user;
  parms->passwd= passwd;
  parms->db= db;
  parms->port= port;
  parms->unix_socket= unix_socket;
  parms->client_flags= client_flags;
  b->async_call_active= 1;

  res= my_context_spawn(&b->async_context, mysql_real_connect_start_internal,
                        parms, malloc(STACK_SIZE), STACK_SIZE);
  if (res < 0)
  {
    /* ToDo: MySQL error handling (so mysql_error() returns something sensible). */
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
    *ret= b->ret_result.r_mysql;
    return 0;
  }
}
