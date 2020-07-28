/*-------------------------------------------------------------------------
 *
 * db2odbc_fdw.c - foreign data wrapper for odbc
 * 
 * Author: stanislawbartkowski@gmail.com
 * 
 * Copyright (c) 2010-2013, PostgreSQL Global Development Group
 *
 * IDENTIFICATION
 *		  db2odbc_fdw/db2odbc_fdw
 *
 *-------------------------------------------------------------------------
 */
#include "postgres.h"

#include <stdio.h>
#include <sys/stat.h>
#include <unistd.h>

#include "access/reloptions.h"
#include "catalog/pg_foreign_server.h"
#include "catalog/pg_foreign_table.h"
#include "catalog/pg_user_mapping.h"
#include "catalog/pg_type.h"
#include "commands/defrem.h"
#include "commands/explain.h"
#include "foreign/fdwapi.h"
#include "foreign/foreign.h"
#include "miscadmin.h"
#include "optimizer/cost.h"
#include "optimizer/pathnode.h"
#include "optimizer/restrictinfo.h"
#include "optimizer/planmain.h"
#include "mb/pg_wchar.h"
#include "storage/fd.h"
#include "utils/array.h"
#include "utils/builtins.h"
#include "funcapi.h"
#include "utils/rel.h"
#include "nodes/pg_list.h"

#include <sql.h>
#include <sqlext.h>

PG_MODULE_MAGIC;

typedef struct db2ColumnDesc
{
    char *buf;
    SQLULEN columnsize;
    bool isNumber;
} db2ColumnDesc;

typedef struct db2PrivateData
{
    AttInMetadata *attinmeta;
    SQLHENV env;
    SQLHDBC dbc;
    SQLHSTMT stmt;
    SQLSMALLINT no_columns;
    db2ColumnDesc *columnsbuf;
    char *cached;
    char **values;
} db2PrivateData;

// ---------------------------------------
// connection cache entry
// ---------------------------------------

#define DSN_MAX_LEN 128

typedef struct db2ConnectionCacheEntry
{
    char dsn_name[DSN_MAX_LEN];
    Oid userId;
    SQLHENV env;
    SQLHDBC dbc;
    struct db2ConnectionCacheEntry *next;
} db2ConnectionCacheEntry;

static db2ConnectionCacheEntry *cache = NULL; // initialize as null

// ----------------------------------------

/*
 * Describes the valid options for objects that use this wrapper.
 */
typedef struct db2FdwOption
{
    const char *optname;
    Oid optcontext; /* Oid of catalog in which option may appear */
    bool required;
} db2FdwOption;

#define DSN "dsn"
#define CACHED "cached"
#define USERNAME "username"
#define PASSWORD "password"
#define QUERY "sql_query"

#define ANYERROR -1

/*
 * Array of valid options
 *
 */
static struct db2FdwOption valid_options[] = {
    /* Foreign server options */
    {DSN, ForeignServerRelationId, true},
    {CACHED, ForeignServerRelationId, false},

    /* Foreign table options */
    {QUERY, ForeignTableRelationId, true},

    /* User mapping options */
    {USERNAME, UserMappingRelationId, true},
    {PASSWORD, UserMappingRelationId, true},

    /* Sentinel */
    {NULL, InvalidOid}};

static void lognotice(const char *s)
{
    elog(NOTICE, s);
}

//#define DEBUG

#ifdef DEBUG

#define N NOTICE
#define NCONNECTION NOTICE

#else

#define N DEBUG5
#define NCONNECTION DEBUG5

#endif

#define logdebug(...) elog(N, __VA_ARGS__)
// -------------------------------------------
// cache handling
// IMPORTANT: THREAD unsafe
// I do not know what about multithreading in postgres
// but this code (working with static variable) is NOT thread safe
// some synchronization is needed
// -------------------------------------------

static db2ConnectionCacheEntry *findConnection(const char *dsn, Oid userId)
{
    db2ConnectionCacheEntry *e;

    logdebug(__func__);
    for (e = cache; e != NULL; e = e->next)
    {
        if ((strcmp(e->dsn_name, dsn) == 0) && e->userId == userId)
        {
            return e;
        }
    }
    return NULL;
}

static void addNewConnection(const char *dsn, Oid userId, SQLHENV env, SQLHDBC dbc)
{
    db2ConnectionCacheEntry *e;

    logdebug(__func__);

    e = malloc(sizeof(db2ConnectionCacheEntry));
    strncpy(e->dsn_name, dsn, DSN_MAX_LEN - 1);
    e->userId = userId;
    e->env = env;
    e->dbc = dbc;
    e->next = cache;
    cache = e;
}

static void removeConnection(SQLHDBC dbc)
{
    db2ConnectionCacheEntry *prev, *e;
    logdebug(__func__);

    logdebug("Try remove connection from cache");
    prev = NULL;
    for (e = cache; e != NULL; e = e->next)
    {
        if (e->dbc == dbc)
        {
            if (prev == NULL)
            {
                cache = e->next;
            }
            else
            {
                prev->next = e->next;
            }
            SQLFreeHandle(SQL_HANDLE_DBC, e->dbc);
            SQLFreeHandle(SQL_HANDLE_ENV, e->env);
            free(e);

            logdebug("Connection removed from cache succesfully");
        }
    }
}

/*
 * SQL functions*/
extern Datum db2odbc_fdw_handler(PG_FUNCTION_ARGS);
extern Datum db2odbc_fdw_validator(PG_FUNCTION_ARGS);

PG_FUNCTION_INFO_V1(db2odbc_fdw_handler);
PG_FUNCTION_INFO_V1(db2odbc_fdw_validator);

/*
 * FDW callback routines
 */
static void db2_ExplainForeignScan(ForeignScanState *node,
                                   ExplainState *es);
static void db2_BeginForeignScan(ForeignScanState *node,
                                 int eflags);
static TupleTableSlot *db2_IterateForeignScan(
    ForeignScanState *node);
static void db2_ReScanForeignScan(ForeignScanState *node);
static void db2_EndForeignScan(ForeignScanState *node);
static void db2_GetForeignRelSize(PlannerInfo *root, RelOptInfo *baserel, Oid foreigntableid);
static void db2_GetForeignPaths(PlannerInfo *root, RelOptInfo *baserel, Oid foreigntableid);
static bool db2_AnalyzeForeignTable(Relation relation, AcquireSampleRowsFunc *func, BlockNumber *totalpages);
static ForeignScan *db2_GetForeignPlan(PlannerInfo *root, RelOptInfo *baserel, Oid foreigntableid, ForeignPath *best_path, List *tlist, List *scan_clauses, Plan *outer_plan);

/*
 * Foreign-data wrapper handler function: return a struct with pointers
 * to my callback routines.
 */
Datum
    db2odbc_fdw_handler(PG_FUNCTION_ARGS)
{
    FdwRoutine *fdwroutine;

    logdebug(__func__);
    fdwroutine = makeNode(FdwRoutine);

    fdwroutine->ExplainForeignScan = db2_ExplainForeignScan;
    fdwroutine->BeginForeignScan = db2_BeginForeignScan;
    fdwroutine->IterateForeignScan = db2_IterateForeignScan;
    fdwroutine->ReScanForeignScan = db2_ReScanForeignScan;
    fdwroutine->EndForeignScan = db2_EndForeignScan;
    fdwroutine->AnalyzeForeignTable = db2_AnalyzeForeignTable;

    fdwroutine->GetForeignRelSize = db2_GetForeignRelSize;
    fdwroutine->GetForeignPaths = db2_GetForeignPaths;
    fdwroutine->GetForeignPlan = db2_GetForeignPlan;

    logdebug("Returning %s", __func__);
    PG_RETURN_POINTER(fdwroutine);
}

static const char *get_name(Oid catalog)
{
    logdebug(__func__);

    switch (catalog)
    {
    case ForeignTableRelationId:
        return "foreign table";
    case ForeignServerRelationId:
        return "foreign data server";
    case UserMappingRelationId:
        return "foreing user mapping";
    }
    return "unrecognized";
}

static void createValidOptions(StringInfoData *buf, Oid context)
{
    db2FdwOption *opt1;

    logdebug(__func__);

    initStringInfo(buf);
    for (opt1 = valid_options; opt1->optname; opt1++)
    {
        if (context == opt1->optcontext)
            appendStringInfo(buf, "%s%s", (buf->len > 0) ? ", " : "",
                             opt1->optname);
    }
}

/*
 * Validate the generic options given to a FOREIGN DATA WRAPPER, SERVER,
 * USER MAPPING or FOREIGN TABLE that uses file_fdw.
 *
 * Raise an ERROR if the option or its value is considered invalid.
 */
Datum
    db2odbc_fdw_validator(PG_FUNCTION_ARGS)
{
    List *options_list;
    Oid context;
    ListCell *cell;
    StringInfoData buf;
    db2FdwOption *opt1;
    bool found;

    options_list = untransformRelOptions(PG_GETARG_DATUM(0));
    context = PG_GETARG_OID(1);
    logdebug("db2_fdw_validator : %s ", get_name(context));

    foreach (cell, options_list)
    {
        DefElem *def;
        const char *option;
        db2FdwOption *opt;
        bool valid;

        def = (DefElem *)lfirst(cell);
        option = defGetString(def);
        logdebug("%s : %s", def->defname, option);
        valid = false;
        for (opt = valid_options; opt->optname; opt++)
        {
            logdebug(opt->optname);
            if (strcmp(opt->optname, def->defname) == 0)
            {
                logdebug("recognized");
                if (context == opt->optcontext)
                {
                    valid = true;
                }
                break;
            }
        }
        if (!valid)
        {
            createValidOptions(&buf, context);
            ereport(ERROR,
                    (errcode(ERRCODE_FDW_INVALID_OPTION_NAME),
                     errmsg("invalid option \"%s\" %s", def->defname,
                            opt->optname == NULL ? "" : "(option name is recognized but is invalid in this context)"),
                     errhint("Valid options in this context are: %s", buf.len ? buf.data : "<none>")));
        }
    }

    // second phase, check required
    for (opt1 = valid_options; opt1->optname; opt1++)
    {
        if (!opt1->required)
        {
            continue;
        }
        if (context != opt1->optcontext)
        {
            continue;
        }
        logdebug("validate required option %s", opt1->optname);
        found = false;

        foreach (cell, options_list)
        {
            DefElem *def;

            def = (DefElem *)lfirst(cell);
            if (strcmp(opt1->optname, def->defname) == 0)
            {
                found = true;
                break;
            }
        }
        if (!found)
        {
            createValidOptions(&buf, context);
            ereport(ERROR,
                    (errcode(ERRCODE_FDW_OPTION_NAME_NOT_FOUND),
                     errmsg("option is required: %s", opt1->optname),
                     errhint("Valid options in this context are: %s", buf.len ? buf.data : "<none>")));
        }
    }

    PG_RETURN_VOID();
}

static void list_drivers()
{
    SQLRETURN ret;
    SQLHENV env;
    char dsn[256];
    char desc[256];
    SQLSMALLINT dsn_ret;
    SQLSMALLINT desc_ret;
    SQLUSMALLINT direction;

    logdebug(__func__);
    ret = SQLAllocHandle(SQL_HANDLE_ENV, SQL_NULL_HANDLE, &env);
    if (!SQL_SUCCEEDED(ret))
    {
        lognotice("Cannot allocate SQL_HANDLE_ENV");
    }
    SQLSetEnvAttr(env, SQL_ATTR_ODBC_VERSION, (void *)SQL_OV_ODBC3, 0);
    direction = SQL_FETCH_FIRST;
    while (SQL_SUCCEEDED(ret = SQLDataSources(env, direction,
                                              (SQLCHAR *)dsn, sizeof(dsn), &dsn_ret,
                                              (SQLCHAR *)desc, sizeof(desc), &desc_ret)))
    {
        direction = SQL_FETCH_NEXT;
        logdebug("%s - %s", dsn, desc);
    }
    SQLFreeHandle(SQL_HANDLE_ENV, env);
    logdebug("End of list drivers");
}

static void extract_error(char *fn,
                          SQLHANDLE handle,
                          SQLSMALLINT type,
                          SQLINTEGER *outnative)
{
    SQLINTEGER i = 0;
    SQLINTEGER native;
    SQLCHAR state[7];
    SQLCHAR text[256];
    SQLSMALLINT len;
    SQLRETURN ret;

    logdebug(__func__);
    elog(NOTICE,
         "\n"
         "The driver reported the following diagnostics while running "
         "%s\n",
         fn);

    if (outnative != NULL)
    {
        *outnative = ANYERROR;
    }
    do
    {
        ret = SQLGetDiagRec(type, handle, ++i, state, &native, text,
                            sizeof(text), &len);
        if (SQL_SUCCEEDED(ret))
        {
            elog(NOTICE, "SQLSTATE:%s : %ld : %ld : %s\n", state, (long int)i, (long int)native, text);
            if (outnative != NULL && *outnative == ANYERROR)
            {
                *outnative = native;
            }
        }
    } while (ret == SQL_SUCCESS);
}

static void getConnection(db2PrivateData *data, Oid foreigntableid, char **query)
{
    ForeignTable *table;
    ForeignServer *server;
    UserMapping *mapping;
    List *options;
    ListCell *lc;
    SQLRETURN ret;

    char *dsn;
    char *username;
    char *password;
    char *cached;
    db2ConnectionCacheEntry *e;

    cached = NULL;
    dsn = username = password = cached = NULL;

    logdebug(__func__);

    table = GetForeignTable(foreigntableid);
    server = GetForeignServer(table->serverid);
    mapping = GetUserMapping(GetUserId(), table->serverid);

    options = NIL;
    options = list_concat(options, table->options);
    options = list_concat(options, server->options);
    options = list_concat(options, mapping->options);

    foreach (lc, options)
    {
        DefElem *def = (DefElem *)lfirst(lc);
        if (strcmp(def->defname, DSN) == 0)
        {
            dsn = defGetString(def);
            logdebug("DSN: %s", dsn);
            continue;
        }
        if (strcmp(def->defname, CACHED) == 0)
        {
            cached = defGetString(def);
            logdebug("CACHED: %s", cached);
            continue;
        }
        if (strcmp(def->defname, PASSWORD) == 0)
        {
            password = defGetString(def);
            logdebug("PASSWORD: %s", password);
            continue;
        }
        if (strcmp(def->defname, USERNAME) == 0)
        {
            username = defGetString(def);
            logdebug("USERNAME: %s", username);
            continue;
        }
        if (strcmp(def->defname, QUERY) == 0)
        {
            *query = defGetString(def);
            logdebug("QUERY: %s", *query);
            continue;
        }
    }

    data->cached = cached;
    e = findConnection(dsn, GetUserId());
    if (e != NULL)
    {
        logdebug("Connection data received from cache");
        data->env = e->env;
        data->dbc = e->dbc;
    }
    else
    {
        /* Allocate an environment handle */
        ret = SQLAllocHandle(SQL_HANDLE_ENV, SQL_NULL_HANDLE, &data->env);
        if (!SQL_SUCCEEDED(ret))
        {
            lognotice("Cannot allocate SQL_HANDLE_ENV");
        }
        ret = SQLSetEnvAttr(data->env, SQL_ATTR_ODBC_VERSION, (void *)SQL_OV_ODBC3, 0);
        if (!SQL_SUCCEEDED(ret))
        {
            lognotice("Cannot set SQL_ATTR_ODBC_VERSION");
            extract_error("SQL_ATTR_ODBC_VERSION", data->env, SQL_HANDLE_ENV, NULL);
        }
        ret = SQLAllocHandle(SQL_HANDLE_DBC, data->env, &data->dbc);
        if (!SQL_SUCCEEDED(ret))
        {
            lognotice("Cannot allocate SQL_HANDLE_DBC");
            extract_error("SQLAllocHandle SQL_HANDLE_DBC", data->env, SQL_HANDLE_ENV, NULL);
        }
        ret = SQLConnect(data->dbc, (SQLCHAR *)dsn, SQL_NTS, (SQLCHAR *)username, SQL_NTS, (SQLCHAR *)password, SQL_NTS);
        if (SQL_SUCCEEDED(ret))
        {
            logdebug("Successfully connected to driver");
        }
        else
        {
            extract_error("SQLDriverConnect", data->dbc, SQL_HANDLE_DBC, NULL);
            ereport(ERROR,
                    (errcode(ERRCODE_FDW_UNABLE_TO_ESTABLISH_CONNECTION),
                     errmsg("cannot connect to odbc dsn %s", dsn),
                     errhint("Check connection data or make sure that target database is online")));
        }
        if (cached != NULL)
        {
            logdebug("Add connection data to cache");
            addNewConnection(dsn, GetUserId(), data->env, data->dbc);
        }
    }
}

static void closeConnection(db2PrivateData *data)
{
    if (data->cached == NULL)
    {
        logdebug("Disconnect");
        SQLDisconnect(data->dbc);
        SQLFreeHandle(SQL_HANDLE_DBC, data->dbc);
        SQLFreeHandle(SQL_HANDLE_ENV, data->env);
    }
    else
    {
        logdebug("Do not disconnect, hashed");
    }
}

/*
 * fileExplainForeignScan
 *		Produce extra output for EXPLAIN
 */
static void
db2_ExplainForeignScan(ForeignScanState *node, ExplainState *es)
{
    logdebug(__func__);
}

#define RETRYNUMB 2

/*
 * file_fixed_lengthBeginForeignScan
 *		Initiate access to the file
 */
static void
db2_BeginForeignScan(ForeignScanState *node, int eflags)
{
    db2PrivateData *data;
    char *query = NULL;
    int retry = 0;
    SQLRETURN ret;
    SQLINTEGER native;
    long int lcached;
    int i, failure,size;

    logdebug(__func__);
    list_drivers();
    data = (db2PrivateData *)palloc(sizeof(db2PrivateData));

    failure = 1;
    // it is
    while (retry < RETRYNUMB)
    {
        getConnection(data, RelationGetRelid(node->ss.ss_currentRelation), &query);
        SQLAllocHandle(SQL_HANDLE_STMT, data->dbc, &data->stmt);
        /* Retrieve a list of rows */
        ret = SQLExecDirect(data->stmt, (SQLCHAR *)query, SQL_NTS);
        if (SQL_SUCCEEDED(ret))
        {
            logdebug("SQLExecDirect");
            // OK
            failure = 0;
            break;
        }
        else
        {
            retry++;
            extract_error("Error while executing query", data->stmt, SQL_HANDLE_STMT, &native);
            if (data->cached == NULL)
            {
                logdebug("Not cached, failed");
                break;
            }
            logdebug("Error native code: %u", native);
            // remove from cache
            removeConnection(data->dbc);
            // try retry
            lcached = atol(data->cached);

            if (lcached == 0)
            {
                lcached = ANYERROR;
                logdebug("Cached is 0 (may an error), ANYERROR is assumed");
            }
            if (lcached == ANYERROR)
            {
                logdebug("ANYERROR assuemed, try again");
                continue;
            }
            if (lcached != native)
            {
                logdebug("Native code not valid for retry, fail");
                break;
            }
            logdebug("Native code valid for retry, try again");
        }
    } // while

    if (failure == 1)
    {
        ereport(ERROR,
                (errcode(ERRCODE_FDW_ERROR),
                 errmsg("Cannot execute query %s", query),
                 errhint("Check query syntax")));
    }

    ret = SQLNumResultCols(data->stmt, &data->no_columns);
    if (!SQL_SUCCEEDED(ret))
    {
        extract_error("SQLNumResultCols", data->stmt, SQL_HANDLE_STMT, NULL);
        ereport(ERROR,
                (errcode(ERRCODE_FDW_ERROR),
                 errmsg("Cannot retrieve number of columns %s", query),
                 errhint("Check query syntax")));
    }
    logdebug("Number of columns: %u", data->no_columns);
    data->columnsbuf = palloc(data->no_columns * sizeof(db2ColumnDesc));
    logdebug("Memory for buffor allocated");
    data->values = palloc(sizeof(char *) * data->no_columns);
    for (i = 0; i < data->no_columns; i++)
    {
        SQLCHAR name[255];
        SQLSMALLINT NameLengthPtr;
        SQLSMALLINT DataTypePtr;
        SQLSMALLINT DecimalDigitsPtr;
        SQLSMALLINT NullablePtr;
        ret = SQLDescribeCol(data->stmt,
                             i + 1,
                             name,
                             sizeof(SQLCHAR) * sizeof(name),
                             &NameLengthPtr,
                             &DataTypePtr,
                             &data->columnsbuf[i].columnsize,
                             &DecimalDigitsPtr,
                             &NullablePtr);
        if (!SQL_SUCCEEDED(ret))
        {
            extract_error("SQLDescribeCol", data->stmt, SQL_HANDLE_STMT, NULL);
            ereport(ERROR,
                    (errcode(ERRCODE_FDW_ERROR),
                     errmsg("Cannot retrieve column description for query %s", query),
                     errhint("Check query syntax")));
        }
        logdebug("Number of bytes for column %s : %lu", name, data->columnsbuf[i].columnsize);
        // important : for some reason it cause crash with declaration Size
        // or putting expression directly in palloc invocation
        size = sizeof(char) * (Size)data->columnsbuf[i].columnsize;
        data->columnsbuf[i].buf = palloc(size);
        logdebug("Memory for column buffer allocated");
        data->values[i] = data->columnsbuf[i].buf;
        data->columnsbuf[i].isNumber = false;
        if ((DataTypePtr == SQL_DECIMAL) || (DataTypePtr == SQL_NUMERIC) || (DataTypePtr == SQL_REAL) ||
            (DataTypePtr == SQL_DOUBLE) || (DataTypePtr == SQL_FLOAT))
        {
            logdebug("Decimal type");
            data->columnsbuf[i].isNumber = true;
        }
    }
    data->attinmeta = TupleDescGetAttInMetadata(node->ss.ss_currentRelation->rd_att);

    node->fdw_state = (void *)data;
}

/*
 * fileIterateForeignScan
 *		Read next record from the data file and store it into the
 *		ScanTupleSlot as a virtual tuple
 */
static TupleTableSlot *
db2_IterateForeignScan(ForeignScanState *node)
{
    db2PrivateData *data;
    SQLRETURN ret;
    SQLSMALLINT i;
    HeapTuple tuple;
    TupleTableSlot *slot;

    data = (db2PrivateData *)node->fdw_state;
    ret = SQLFetch(data->stmt);

    logdebug(__func__);
    logdebug("SQLFetch %u", ret);
    if (ret == SQL_NO_DATA_FOUND)
    {
        return NULL;
    }
    if (!SQL_SUCCEEDED(ret))
    {
        extract_error("SQLFetch", data->stmt, SQL_HANDLE_STMT, NULL);
        ereport(ERROR,
                (errcode(ERRCODE_FDW_ERROR),
                 errmsg("Cannot fetch next row"),
                 errhint("Check query syntax")));
    }
    slot = node->ss.ss_ScanTupleSlot;
    ExecClearTuple(slot);
    for (i = 0; i < data->no_columns; i++)
    {
        SQLLEN indicator;
        ret = SQLGetData(data->stmt, i + 1, SQL_C_CHAR,
                         data->columnsbuf[i].buf, data->columnsbuf[i].columnsize, &indicator);
        logdebug("GetData %s %u", data->columnsbuf[i].buf, i);
        logdebug("Indicator %ld", indicator);
        // for some reason indicator should be casted to int to have comparison correct
        if ((int)indicator == SQL_NULL_DATA)
        {
            data->values[i] = NULL;
        }
        else
        {
            data->values[i] = data->columnsbuf[i].buf;
            if (data->columnsbuf[i].isNumber)
            {
                char *p;
                logdebug("Decimal type, replace , with .");
                while ((p = strrchr(data->values[i], ',')))
                {
                    *p = '.';
                }
            }
        }
        if (!SQL_SUCCEEDED(ret))
        {
            extract_error("SQLGetData", data->stmt, SQL_HANDLE_STMT, NULL);
            ereport(ERROR,
                    (errcode(ERRCODE_FDW_ERROR),
                     errmsg("Cannot get data for next column"),
                     errhint("Check query syntax")));
        }
    }
    tuple = BuildTupleFromCStrings(data->attinmeta, data->values);
#if PG_VERSION_NUM < 120000
    ExecStoreTuple(tuple, slot, InvalidBuffer, false);
#else
    ExecStoreHeapTuple(tuple, slot, false);
#endif

    return slot;
}

/*
 * fileEndForeignScan
 *		Finish scanning foreign table and dispose objects used for this scan
 */
static void
db2_EndForeignScan(ForeignScanState *node)
{
    db2PrivateData *data;

    logdebug(__func__);
    data = (db2PrivateData *)node->fdw_state;
    SQLFreeHandle(SQL_HANDLE_STMT, data->stmt);
    closeConnection(data);
}

/*
 * fileReScanForeignScan
 *		Rescan table, possibly with new parameters
 */
static void
db2_ReScanForeignScan(ForeignScanState *node)
{
    logdebug(__func__);
}

static void db2_GetForeignRelSize(PlannerInfo *root, RelOptInfo *baserel, Oid foreigntableid)
{    
    logdebug(__func__);

    baserel->rows = 0;
    baserel->tuples = baserel->rows;
}

static void db2_EstimateCosts(PlannerInfo *root, RelOptInfo *baserel, Cost *startup_cost, Cost *total_cost, Oid foreigntableid)
{

    logdebug("----> starting %s", __func__);

    /* Fetch the foreign table options */
    //	odbcGetTableOptions(foreigntableid, &options);

    //	odbcGetTableSize(&options, &table_size);

    *startup_cost = 25;

    *total_cost = baserel->rows + *startup_cost;

    logdebug("----> finishing %s", __func__);
}

static void db2_GetForeignPaths(PlannerInfo *root, RelOptInfo *baserel, Oid foreigntableid)
{
    Cost startup_cost;
    Cost total_cost;
    Path *path;

    logdebug("----> starting %s", __func__);
    logdebug("baserel 1 %p", baserel->pathlist);

    db2_EstimateCosts(root, baserel, &startup_cost, &total_cost, foreigntableid);

    logdebug("Before create path");

    path = (Path *)create_foreignscan_path(root, baserel,
#if PG_VERSION_NUM >= 90600
                                                NULL, /* PathTarget */
#endif
                                                baserel->rows,
                                                startup_cost,
                                                total_cost,
                                                NIL,  /* no pathkeys */
                                                NULL, /* no outer rel either */
                                                NULL, /* no extra plan */
                                                NIL /* no fdw_private list */);


    add_path(baserel, path);

    logdebug("----> finishing %s", __func__);
}

static ForeignScan *db2_GetForeignPlan(PlannerInfo *root, RelOptInfo *baserel,
                                       Oid foreigntableid, ForeignPath *best_path, List *tlist, List *scan_clauses, Plan *outer_plan)
{
    Index scan_relid = baserel->relid;
    logdebug("----> starting %s", __func__);

    scan_clauses = extract_actual_clauses(scan_clauses, false);

    logdebug("----> finishing %s", __func__);

    return make_foreignscan(tlist, scan_clauses,
                            scan_relid, NIL, NIL,
                            NIL /* fdw_scan_tlist */, NIL, /* fdw_recheck_quals */
                            NULL /* outer_plan */);
}

static bool db2_AnalyzeForeignTable(Relation relation, AcquireSampleRowsFunc *func, BlockNumber *totalpages)
{
    logdebug("----> starting %s", __func__);
    logdebug("----> finishing %s", __func__);

    return false;
}
