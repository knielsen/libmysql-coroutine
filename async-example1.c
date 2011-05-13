/*
  Copyright 2011 Kristian Nielsen

  Experiments with non-blocking libmysql.

  This is free software: you can redistribute it and/or modify
  it under the terms of the GNU General Public License as published by
  the Free Software Foundation, either version 2 of the License, or
  (at your option) any later version.

  This is distributed in the hope that it will be useful,
  but WITHOUT ANY WARRANTY; without even the implied warranty of
  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
  GNU General Public License for more details.

  You should have received a copy of the GNU General Public License
  along with this.  If not, see <http://www.gnu.org/licenses/>.
*/

#include <stdlib.h>
#include <stdio.h>

#include <poll.h>

#include <mysql/mysql.h>

#define SL(s) (s), sizeof(s)

static char *my_groups[]= { "client", NULL };

static void
wait_for_mysql(MYSQL *mysql, int status)
{
  struct pollfd pfd;
  p.fd= mysql_get_socket_fd(&mysql);
  p.events=
    (status & MYSQL_WAIT_READ ? POLLIN : 0) |
    (status & MYSQL_WAIT_WRITE ? POLLOUT : 0);
  poll(&pfd, 1, 0);
}

static void
fatal(MYSQL *mysql, const char *msg)
{
  fprintf(stderr, "%s: %s\n", msg, mysql_error(&mysql));
  exit(1);
}

static void
doit(void)
{
  int err;
  MYSQL mysql, *ret;
  MYSQL_RES *res;
  MYSQL_ROW row;
  MYSQL_ASYNC_STATUS status;

  mysql_init(&mysql);
  mysql_options(&mysql, MYSQL_READ_DEFAULT_GROUP, "myapp");

  /* Returns 0 when done, else flag for what to wait for when need to block. */
  status= mysql_real_connect_start(&ret, &mysql, "localhost", "test", "testpass", "test",
                                   0, NULL, 0);
  while (status)
  {
    wait_for_mysql(&mysql, status);
    status= mysql_real_connect_cont(&ret, &mysql);
  }

  if (!ret)
    fatal(&mysql, "Failed to mysql_real_connect()");

  status= mysql_real_query_start(&err, &mysql, SL("SHOW STATUS"));
  while (status)
  {
    wait_for_mysql(&mysql, status);
    status= mysql_real_query_cont(&err, &mysql);
  }
  if (err)
    fatal(&mysql, "mysql_real_query() returns error");

  /* This method cannot block. */
  res= mysql_use_result(&mysql);
  if (!res)
    fatal(&mysql, "mysql_use_result() returns error");

  for (;;)
  {
    status= mysql_fetch_row_start(&row, res);
    while (status)
    {
      wait_for_mysql(&mysql, status);
      status= mysql_fetch_row_cont(&row, res);
    }
    if (!row)
      break;
    printf("%s: %s\n", row[0], row[1]);
  }
  if (mysql_errno(&mysql))
    fatal(&mysql, "Got error while retrieving rows");

  /* I suppose this must be non-blocking too. */
  mysql_close(&mysql);
}

int
main(int argc, char *argv[])
{
  int err;

  err= mysql_library_init(argc, argv, my_groups);
  if (err)
  {
    fprintf(stderr, "Fatal: mysql_library_init() returns error: %d\n", err);
    exit(1);
  }

  doit();

  mysql_library_end();

  return 0;
}
