##########################################################################
#
#                foreign-data wrapper for DB2/ODBC
#
# Copyright (c) 2012, PostgreSQL Global Development Group
#
# This software is released under the PostgreSQL Licence
#
# Author: stanislawbartkowski@gmail.com
#
# IDENTIFICATION
#                 db2odbc_fdw/Makefile
# 
##########################################################################

MODULE_big = db2odbc_fdw
OBJS = db2odbc_fdw.o

EXTENSION = db2odbc_fdw
DATA = db2odbc_fdw--1.0.sql

REGRESS = db2odbc_fdw

EXTRA_CLEAN = sql/db2odbc_fdw.sql expected/db2odbc_fdw.out

SHLIB_LINK = -lodbc

PG_CPPFLAGS="-Wno-format-security"

PGXS=/usr/lib64/pgsql/lib/pgxs
PG_CONFIG = pg_config
PGXS := $(shell $(PG_CONFIG) --pgxs)
include $(PGXS)

