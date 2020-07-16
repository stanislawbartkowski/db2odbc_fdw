/*-------------------------------------------------------------------------
 *
 *                foreign-data wrapper for DB2/CLI/ODBC
 *
 * Copyright (c) 2020, PostgreSQL Global Development Group
 *
 * This software is released under the PostgreSQL Licence
 *
 * Author: stanislawbartkowski@gmail.com
 *
 * IDENTIFICATION
 *                db2odbc_fdw/db2odbc_fdw--1.0.sql
 *
 *-------------------------------------------------------------------------
 */

CREATE FUNCTION db2odbc_fdw_handler()
RETURNS fdw_handler
AS 'MODULE_PATHNAME'
LANGUAGE C STRICT;

CREATE FUNCTION db2odbc_fdw_validator(text[], oid)
RETURNS void
AS 'MODULE_PATHNAME'
LANGUAGE C STRICT;

CREATE FOREIGN DATA WRAPPER db2odbc_fdw
  HANDLER db2odbc_fdw_handler
  VALIDATOR db2odbc_fdw_validator;
