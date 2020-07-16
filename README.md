## DB2ODBC FDW (beta) for PostgreSQL 12

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

## Example 
Assume that foreign DB2 database is referenced in ODBC as *TESTDB* and the foreign DB2 table is *test*.<br>
DB2 test table was created using the following command.
> db2 "create table test (id int, name varchar(100))"

```
CREATE EXTENSION db2odbc_fdw;

CREATE SERVER db2odbc_server FOREIGN DATA WRAPPER db2odbc_fdw OPTIONS (dsn 'BIGTEST');

CREATE USER MAPPING FOR postgres SERVER db2odbc_server OPTIONS (username 'db2inst1', password 'db2inst1');

CREATE FOREIGN TABLE db2test ( id int, name varchar(100)) SERVER db2odbc_server  OPTIONS ( sql_query 'select * from TEST'  );
```

The following parameter can be set on a ODBC foreign table:


sql_query:	User defined SQL statement for querying the foreign table.

IMPORTANT: columns read from query are mapped to foreign table definition from left to write. Mapping
is based on column order, not column name.

The following parameter can be set on a user mapping for a ODBC
foreign server:

username:	The username to authenticate to the foreign server with.
		
password:	The password to authenticate to the foreign server with.



## Configuration

Configure DB2 ODBC connection to target DB2 database (look below for more details).


Down

To build the code, you need to have one of the ODBC driver managers installed on your computer. 

A list of driver managers is available here: http://en.wikipedia.org/wiki/Open_Database_Connectivity

Once that's done, the extension can be built with:

PATH=/usr/local/pgsql91/bin/:$PATH make USE_PGXS=1 
sudo PATH=/usr/local/pgsql91/bin/:$PATH make USE_PGXS=1 install

(assuming you have PostgreSQL 9.1 in /usr/local/pgsql91).

Before setting up foreign data wrapper make sure that ODBC connection is configured and working.

Usage
-----

The following parameters can be set on ODBC foreign server:

dsn:		The Database Source Name for the foreign database system you're connecting to.

The following parameter can be set on a ODBC foreign table:


sql_query:	User defined SQL statement for querying the foreign table.

IMPORTANT: columns read from query are mapped to foreign table definition from left to write. Mapping
is based on column order, not column name.

The following parameter can be set on a user mapping for a ODBC
foreign server:

username:	The username to authenticate to the foreign server with.
		
password:	The password to authenticate to the foreign server with.


Example (assuming TSAMPLE dsn name pointing to DB2 SAMPLE database)
-------

CREATE EXTENSION db2odbc_fdw;

CREATE SERVER db2odbc_server 
	FOREIGN DATA WRAPPER db2odbc_fdw 
	OPTIONS (dsn 'TSAMPLE');

CREATE FOREIGN TABLE sample_emp (
  empno char(6),
  firstname varchar(12)
) 
 SERVER db2odbc_server
 OPTIONS (
   sql_query 'select * from emp'
 );

CREATE USER MAPPING FOR postgres
	SERVER db2odbc_server
	OPTIONS (username 'db2inst3', password 'db2inst3');

-------
Cached connection:

'cached' parameter : native code causing connection retry.

CREATE SERVER db2odbc_servercached 
        FOREIGN DATA WRAPPER db2odbc_fdw 
        OPTIONS (dsn 'TSAMPLE' , cached '-30081');


CREATE FOREIGN TABLE cached_sample_emp (
  empno char(6),
  firstname varchar(12)
) 
 SERVER db2odbc_servercached
 OPTIONS (
   sql_query 'select * from emp fetch first 10 rows only'
 );

CREATE USER MAPPING FOR sb
        SERVER db2odbc_servercached
        OPTIONS (username 'db2inst2', password 'db2inst2');

----------
Additional

Do not forget give access to foreign server to other users if necessary

Example:


GRANT ALL PRIVILEGES ON FOREIGN SERVER db2odbc_server TO  PUBLIC;

---------------------------
stanislawbartkowski@gmail.com
