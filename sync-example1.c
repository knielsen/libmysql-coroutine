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

#include <mysql/mysql.h>

#define SL(s) (s), sizeof(s)

static char *my_groups[]= { "client", NULL };

static void
doit(void)
{
  int err;
  MYSQL mysql;
  MYSQL_RES *res;
  MYSQL_ROW row;

  mysql_init(&mysql);
  mysql_options(&mysql, MYSQL_READ_DEFAULT_GROUP, "myapp");
  if (!mysql_real_connect(&mysql, "localhost", "test", "testpass", "test",
                          0, NULL, 0))
  {
    fprintf(stderr, "Failed to mysql_real_connect(): %s\n", mysql_error(&mysql));
    exit(1);
  }

  err= mysql_real_query(&mysql, SL("SHOW STATUS"));
  if (err)
  {
    fprintf(stderr, "mysql_real_query() returns error: %s\n", mysql_error(&mysql));
    exit(1);
  }

  res= mysql_use_result(&mysql);
  if (!res)
  {
    fprintf(stderr, "mysql_use_result() returns error: %s\n", mysql_error(&mysql));
    exit(1);
  }

  for (;;)
  {
    row= mysql_fetch_row(res);
    if (!row)
      break;
    printf("%s: %s\n", row[0], row[1]);
  }
  if (mysql_errno(&mysql))
  {
    fprintf(stderr, "Got error while retrieving rows: %s\n", mysql_error(&mysql));
  }

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
