/*
  Copyright 2011 Kristian Nielsen and Monty Program Ab.

  This file is free software; you can redistribute it and/or
  modify it under the terms of the GNU Lesser General Public
  License as published by the Free Software Foundation; either
  version 2.1 of the License, or (at your option) any later version.

  This library is distributed in the hope that it will be useful,
  but WITHOUT ANY WARRANTY; without even the implied warranty of
  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
  Lesser General Public License for more details.

  You should have received a copy of the GNU General Public License
  along with this.  If not, see <http://www.gnu.org/licenses/>.
*/
#include "my_test.h"
#include "ma_common.h"

#define TEST_ARRAY_SIZE 1024

static my_bool bulk_enabled= 0;

char *rand_str(size_t length) {
    const char charset[] = "0123456789"
                     "abcdefghijklmnopqrstuvwxyz"
                     "ABCDEFGHIJKLMNOPQRSTUVWXYZ";
    char *dest= (char *)malloc(length+1);
    char *p= dest;
    while (length-- > 0) {
        *dest++ = charset[rand() % sizeof(charset)];
    }
    *dest = '\0';
    return p;
}

static int check_bulk(MYSQL *mysql)
{
  bulk_enabled= (!(mysql->server_capabilities & CLIENT_MYSQL) &&
      (mysql->extension->mariadb_server_capabilities & MARIADB_CLIENT_STMT_BULK_OPERATIONS >> 32));
  diag("bulk %ssupported", bulk_enabled ? "" : "not ");
  return OK;
}

static int bulk1(MYSQL *mysql)
{
  MYSQL_STMT *stmt= mysql_stmt_init(mysql);
  const char *stmt_str= "INSERT INTO bulk1 VALUES (?,?)";
  unsigned int array_size= TEST_ARRAY_SIZE;
  int rc;
  unsigned int i;
  char **buffer;
  unsigned long *lengths;
  unsigned int *vals;
  MYSQL_BIND bind[2];
  MYSQL_RES *res;
  MYSQL_ROW row;
  unsigned int intval;

  if (!bulk_enabled)
    return SKIP;

  rc= mysql_select_db(mysql, "testc");

  rc= mysql_query(mysql, "DROP TABLE IF EXISTS bulk1");
  check_mysql_rc(rc, mysql);

  rc= mysql_query(mysql, "CREATE TABLE bulk1 (a int , b VARCHAR(255))");
  check_mysql_rc(rc, mysql);

  rc= mysql_stmt_prepare(stmt, stmt_str, strlen(stmt_str));
  check_stmt_rc(rc, stmt);

  /* allocate memory */
  buffer= calloc(TEST_ARRAY_SIZE, sizeof(char *));
  lengths= (unsigned long *)calloc(sizeof(long), TEST_ARRAY_SIZE);
  vals= (unsigned int *)calloc(sizeof(int), TEST_ARRAY_SIZE);

  for (i=0; i < TEST_ARRAY_SIZE; i++)
  {
    buffer[i]= rand_str(254);
    lengths[i]= -1;
    vals[i]= i; 
  }

  memset(bind, 0, sizeof(MYSQL_BIND) * 2);
  bind[0].buffer_type= MYSQL_TYPE_LONG;
  bind[0].buffer= vals;
  bind[1].buffer_type= MYSQL_TYPE_STRING;
  bind[1].buffer= (void *)buffer;
  bind[1].length= (unsigned long *)lengths;

  rc= mysql_stmt_attr_set(stmt, STMT_ATTR_ARRAY_SIZE, &array_size);
  check_stmt_rc(rc, stmt);

  rc= mysql_stmt_bind_param(stmt, bind);
  check_stmt_rc(rc, stmt);

  for (i=0; i < 100; i++)
  {
    rc= mysql_stmt_execute(stmt);
    check_stmt_rc(rc, stmt);
    FAIL_IF(mysql_stmt_affected_rows(stmt) != TEST_ARRAY_SIZE, "affected_rows != TEST_ARRAY_SIZE");
  }

  for (i=0; i < array_size; i++)
    free(buffer[i]);

  free(buffer);
  free(lengths);
  free(vals);

  rc= mysql_stmt_close(stmt);
  check_mysql_rc(rc, mysql);

  rc= mysql_query(mysql, "SELECT COUNT(*) FROM bulk1");
  check_mysql_rc(rc, mysql);

  res= mysql_store_result(mysql);
  row= mysql_fetch_row(res);
  intval= atoi(row[0]);
  mysql_free_result(res);
  FAIL_IF(intval != array_size * 100, "Expected 102400 rows");

  rc= mysql_query(mysql, "SELECT MAX(a) FROM bulk1");
  check_mysql_rc(rc, mysql);

  res= mysql_store_result(mysql);
  row= mysql_fetch_row(res);
  intval= atoi(row[0]);
  mysql_free_result(res);
  FAIL_IF(intval != array_size - 1, "Expected max value 1024");

  rc= mysql_query(mysql, "DROP TABLE IF EXISTS bulk1");
  check_mysql_rc(rc, mysql);
  return OK;
}

static int bulk2(MYSQL *mysql)
{
  MYSQL_STMT *stmt= mysql_stmt_init(mysql);
  int rc;
  MYSQL_BIND bind[2];
  unsigned int i;
  unsigned int array_size=1024;
  char indicator[1024];
  long lval[1024];

  if (!bulk_enabled)
    return SKIP;
  rc= mysql_query(mysql, "DROP TABLE IF EXISTS bulk2");
  check_mysql_rc(rc, mysql);

  rc= mysql_query(mysql, "CREATE TABLE bulk2 (a int default 4, b int default 2)");
  check_mysql_rc(rc, mysql);

  rc= mysql_stmt_prepare(stmt, "INSERT INTO bulk2 VALUES (?,1)", -1);
  check_stmt_rc(rc, stmt);

  memset(bind, 0, 2 * sizeof(MYSQL_BIND));

  for (i=0; i < array_size; i++)
  {
    indicator[i]= STMT_INDICATOR_DEFAULT;
    lval[i]= i;
  }

  bind[0].buffer_type= MYSQL_TYPE_LONG;
  bind[0].u.indicator= indicator;
  bind[1].buffer_type= MYSQL_TYPE_LONG;
  bind[1].buffer= &lval;

  rc= mysql_stmt_attr_set(stmt, STMT_ATTR_ARRAY_SIZE, &array_size);
  check_stmt_rc(rc, stmt);

  rc= mysql_stmt_bind_param(stmt, bind);
  check_stmt_rc(rc, stmt);

  rc= mysql_stmt_execute(stmt);
  check_stmt_rc(rc, stmt);

  mysql_stmt_close(stmt);

  return OK;
}

static int bulk3(MYSQL *mysql)
{
  struct st_bulk3 {
    char char_value[20];
    unsigned long length;
    int  int_value;
  };

  struct st_bulk3 val[3]= {{"Row 1", 5, 1},
                           {"Row 02", 6, 2},
                           {"Row 003", 7, 3}};
  int rc;
  MYSQL_BIND bind[2];
  MYSQL_STMT *stmt= mysql_stmt_init(mysql);
  size_t row_size= sizeof(struct st_bulk3);
  int array_size= 3;

  if (!bulk_enabled)
    return SKIP;
  rc= mysql_query(mysql, "DROP TABLE IF EXISTS bulk3");
  check_mysql_rc(rc,mysql);
  rc= mysql_query(mysql, "CREATE TABLE bulk3 (name varchar(20), row int)");
  check_mysql_rc(rc,mysql);

  rc= mysql_stmt_prepare(stmt, "INSERT INTO bulk3 VALUES (?,?)", -1);
  check_stmt_rc(rc, stmt);

  memset(bind, 0, sizeof(MYSQL_BIND)*2);
  
  rc= mysql_stmt_attr_set(stmt, STMT_ATTR_ARRAY_SIZE, &array_size);
  check_stmt_rc(rc, stmt);
  rc= mysql_stmt_attr_set(stmt, STMT_ATTR_ROW_SIZE, &row_size);
  check_stmt_rc(rc, stmt);

  bind[0].buffer_type= MYSQL_TYPE_STRING;
  bind[0].buffer= &val[0].char_value;
  bind[0].length= &val[0].length;
  bind[1].buffer_type= MYSQL_TYPE_LONG;
  bind[1].buffer= &val[0].int_value;

  rc= mysql_stmt_bind_param(stmt, bind);
  check_stmt_rc(rc, stmt);
  rc= mysql_stmt_execute(stmt);
  check_stmt_rc(rc, stmt);

  mysql_stmt_close(stmt);
  return OK;
}

static int bulk4(MYSQL *mysql)
{
  struct st_bulk4 {
    char char_value[20];
    char indicator1;
    int  int_value;
    char indicator2;
  };

  struct st_bulk4 val[]= {{"Row 1", STMT_INDICATOR_NTS, 0, STMT_INDICATOR_DEFAULT},
                          {"Row 2", STMT_INDICATOR_NTS, 0, STMT_INDICATOR_DEFAULT},
                          {"Row 3", STMT_INDICATOR_NTS, 0, STMT_INDICATOR_DEFAULT}};
  int rc;
  MYSQL_BIND bind[2];
  MYSQL_RES *res;
  MYSQL_STMT *stmt= mysql_stmt_init(mysql);
  size_t row_size= sizeof(struct st_bulk4);
  int array_size= 3;
  unsigned long lengths[3]= {-1, -1, -1};

  if (!bulk_enabled)
    return SKIP;
  rc= mysql_query(mysql, "DROP TABLE IF EXISTS bulk4");
  check_mysql_rc(rc,mysql);
  rc= mysql_query(mysql, "CREATE TABLE bulk4 (name varchar(20), row int not null default 3)");
  check_mysql_rc(rc,mysql);

  rc= mysql_stmt_prepare(stmt, "INSERT INTO bulk4 VALUES (?,?)", -1);
  check_stmt_rc(rc, stmt);

  memset(bind, 0, sizeof(MYSQL_BIND)*2);
  
  rc= mysql_stmt_attr_set(stmt, STMT_ATTR_ARRAY_SIZE, &array_size);
  check_stmt_rc(rc, stmt);
  rc= mysql_stmt_attr_set(stmt, STMT_ATTR_ROW_SIZE, &row_size);
  check_stmt_rc(rc, stmt);

  bind[0].buffer_type= MYSQL_TYPE_STRING;
  bind[0].u.indicator= &val[0].indicator1;
  bind[0].buffer= &val[0].char_value;
  bind[0].length= lengths;
  bind[1].buffer_type= MYSQL_TYPE_LONG;
  bind[1].u.indicator= &val[0].indicator2;

  rc= mysql_stmt_bind_param(stmt, bind);
  check_stmt_rc(rc, stmt);
  rc= mysql_stmt_execute(stmt);
  check_stmt_rc(rc, stmt);

  mysql_stmt_close(stmt);

  rc= mysql_query(mysql, "SELECT * FROM bulk4 WHERE row=3");
  check_mysql_rc(rc, mysql);
  res= mysql_store_result(mysql);
  rc= mysql_num_rows(res);
  mysql_free_result(res);
  FAIL_IF(rc != 3, "expected 3 rows");
  return OK;
}

static int bulk_null(MYSQL *mysql)
{
  MYSQL_STMT *stmt= mysql_stmt_init(mysql);
  int rc;
  MYSQL_BIND bind[2];
  unsigned int param_count= 2;
  unsigned int array_size= 2;
  unsigned long lengths[2]= {-1, -1};
  const char **buf= calloc(1, 2 * sizeof(char *));

  buf[0]= strdup("foo");
  buf[1]= strdup("foobar");

  rc= mariadb_stmt_execute_direct(stmt, "DROP TABLE IF EXISTS bulk_null", -1);
  check_stmt_rc(rc, stmt);

  rc= mariadb_stmt_execute_direct(stmt, "CREATE TABLE bulk_null (a int not null auto_increment primary key, b varchar(20))", -1);
  check_stmt_rc(rc, stmt);

  memset(bind, 0, 2 * sizeof(MYSQL_BIND));
  bind[0].buffer_type= MYSQL_TYPE_NULL;
  bind[1].buffer_type= MYSQL_TYPE_STRING;
  bind[1].buffer= buf;
  bind[1].length= lengths;

  mysql_stmt_close(stmt);
  stmt= mysql_stmt_init(mysql);

  rc= mysql_stmt_attr_set(stmt, STMT_ATTR_PREBIND_PARAMS, &param_count);
  check_stmt_rc(rc, stmt);

  rc= mysql_stmt_attr_set(stmt, STMT_ATTR_ARRAY_SIZE, &array_size);
  check_stmt_rc(rc, stmt);

  rc= mysql_stmt_bind_param(stmt, bind);
  check_stmt_rc(rc, stmt);

  rc= mariadb_stmt_execute_direct(stmt, "INSERT INTO bulk_null VALUES (?, ?)", -1);
  check_stmt_rc(rc, stmt);

  mysql_stmt_close(stmt);
  return OK;

}

struct my_tests_st my_tests[] = {
  {"check_bulk", check_bulk, TEST_CONNECTION_DEFAULT, 0,  NULL,  NULL},
  {"bulk1", bulk1, TEST_CONNECTION_DEFAULT, 0,  NULL,  NULL},
  {"bulk2", bulk2, TEST_CONNECTION_DEFAULT, 0,  NULL,  NULL},
  {"bulk3", bulk3, TEST_CONNECTION_DEFAULT, 0,  NULL,  NULL},
  {"bulk4", bulk4, TEST_CONNECTION_DEFAULT, 0,  NULL,  NULL},
  {"bulk_null", bulk_null, TEST_CONNECTION_DEFAULT, 0,  NULL,  NULL},
  {NULL, NULL, 0, 0, NULL, NULL}
};


int main(int argc, char **argv)
{
  if (argc > 1)
    get_options(argc, argv);

  get_envvars();

  run_tests(my_tests);

  return(exit_status());
}
