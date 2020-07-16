DB2ODBC FDW (beta) for PostgreSQL 9.1+
===================================

This PostgreSQL extension implements a Foreign Data Wrapper (FDW) for
remote databases using Open Database Connectivity(ODBC): http://msdn.microsoft.com/en-us/library/ms714562(v=VS.85).aspx


Building
--------

To build the code, you need to have one of the ODBC driver managers installed
on your computer. 

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
