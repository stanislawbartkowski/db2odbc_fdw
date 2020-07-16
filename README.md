## DB2ODBC FDW for PostgreSQL 12

This PostgreSQL extension for DB2 implements a Foreign Data Wrapper (FDW) to use DB2 ODBC connector. It is an adaptation of PostgresSQL ODBC connection https://github.com/CartoDB/odbc_fdw, because I'm not happy with that implementation.

## Building

Download source code and make the extension. <br>

> git clone https://github.com/stanislawbartkowski/db2odbc_fdw.git<br>
> cd ob2odbc_fdw<br>
> make<br>

The following target files are created if successful.
```
db2odbc_fdw.o
db2odbc_fdw.so
```
## Installation

As root user or sudo<br>
> (sudo) make install<br>
```
usr/bin/mkdir -p '/usr/local/pgsql/lib'
/usr/bin/mkdir -p '/usr/local/pgsql/share/extension'
/usr/bin/mkdir -p '/usr/local/pgsql/share/extension'
/usr/bin/install -c -m 755  db2odbc_fdw.so '/usr/local/pgsql/lib/db2odbc_fdw.so'
/usr/bin/install -c -m 644 .//db2odbc_fdw.control '/usr/local/pgsql/share/extension/'
/usr/bin/install -c -m 644 .//db2odbc_fdw--1.0.sql  '/usr/local/pgsql/share/extension/'
```
## Usage

The following parameters can be set on ODBC foreign server<br>

| Parameter | Description | Example
|---|---|--|
| dsn | The ODBC Database Source Name for the foreign DB2 database system you are connecting | BIGTEST
| sql_query | User defined SQL statement for querying the foreign DB2 table | SELECT * FROM TEST
| username | The username to authenticate in the foreign DB2 database | db2inst1
| password | The password to authenticate in the foreign DB2 database | secret
| cached (optional) | Native code causing connection retry | 

## Example 
Assume that foreign DB2 database is referenced in ODBC as *TESTDB* and the foreign DB2 table is *test*.<br>
DB2 test table was created using the following command.
> db2 "create table test (id int, name varchar(100))"<br>
> db2 "insert into test values(1,'name1')"<br>

```
CREATE EXTENSION db2odbc_fdw;

CREATE SERVER db2odbc_server FOREIGN DATA WRAPPER db2odbc_fdw OPTIONS (dsn 'BIGTEST');

(optional, cached connection)
CREATE SERVER db2odbc_servercached FOREIGN DATA WRAPPER db2odbc_fdw OPTIONS (dsn 'BIGTEST' , cached '-30081');

CREATE USER MAPPING FOR postgres SERVER db2odbc_server OPTIONS (username 'db2inst1', password 'db2inst1');

CREATE FOREIGN TABLE db2test ( id int, name varchar(100)) SERVER db2odbc_server  OPTIONS ( sql_query 'select * from TEST'  );

(if expected, give other user access to foreign server)

GRANT ALL PRIVILEGES ON FOREIGN SERVER db2odbc_server TO PUBLIC;
```
Test
> select * from db2test;<br>
```
 id | name  
----+-------
  1 | name1
(1 row)

```
