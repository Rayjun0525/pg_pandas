/* pg_pandas.c
 *
 * PostgreSQL extension to apply any Pandas operation on SQL query results using a background worker.
 */

#include "postgres.h"
#include "fmgr.h"
#include "funcapi.h"
#include "access/htup_details.h"
#include "catalog/pg_type.h"
#include "utils/builtins.h"
#include "executor/spi.h"
#include "storage/ipc.h"
#include "storage/shmem.h"
#include "storage/proc.h"
#include "miscadmin.h"

#include <string.h>

PG_MODULE_MAGIC;

/* Forward declaration for worker communication */
Datum pg_pandas_fn(PG_FUNCTION_ARGS);
PG_FUNCTION_INFO_V1(pg_pandas_fn);

/* Shared memory structure for communication */
typedef struct {
    char data[8192];
    char operation[2048];
    char result[65536];
    bool ready;
    LWLock lock;
} PandasSharedData;

/* Pointer to shared memory */
static PandasSharedData *pandas_shared = NULL;

/* Function to communicate with background worker */
Datum
pg_pandas_fn(PG_FUNCTION_ARGS)
{
    FuncCallContext *funcctx;
    TupleDesc        tupdesc;
    AttInMetadata   *attinmeta;

    if (SRF_IS_FIRSTCALL())
    {
        MemoryContext oldcontext;
        text *op_t = PG_GETARG_TEXT_P(1);
        Datum input = PG_GETARG_DATUM(0);

        /* Convert operation text to C string */
        char *operation = text_to_cstring(op_t);

        /* Convert input data to JSON */
        char *json_data = NULL;
        Oid input_type = get_fn_expr_argtype(fcinfo->flinfo, 0);

        if (input_type == RECORDOID)
        {
            /* Handle record input */
            TupleDesc input_tupdesc = get_call_result_type(fcinfo, NULL, &tupdesc);
            if (!input_tupdesc)
                ereport(ERROR, (errmsg("Failed to get input tuple descriptor")));

            json_data = DatumGetCString(DirectFunctionCall1(row_to_json, input));
        }
        else
        {
            /* Convert scalar value to JSON */
            json_data = DatumGetCString(DirectFunctionCall1(to_json, input));
        }

        funcctx = SRF_FIRSTCALL_INIT();
        oldcontext = MemoryContextSwitchTo(funcctx->multi_call_memory_ctx);

        /* Send request to worker */
        LWLockAcquire(&pandas_shared->lock, LW_EXCLUSIVE);
        strncpy(pandas_shared->data, json_data, sizeof(pandas_shared->data) - 1);
        pandas_shared->data[sizeof(pandas_shared->data) - 1] = '\0';
        strncpy(pandas_shared->operation, operation, sizeof(pandas_shared->operation) - 1);
        pandas_shared->operation[sizeof(pandas_shared->operation) - 1] = '\0';
        pandas_shared->ready = true;
        LWLockRelease(&pandas_shared->lock);

        /* Wait for worker to process */
        while (true)
        {
            LWLockAcquire(&pandas_shared->lock, LW_SHARED);
            bool processed = pandas_shared->ready;
            LWLockRelease(&pandas_shared->lock);

            if (!processed)
                break;

            /* Check if result is ready */
            LWLockAcquire(&pandas_shared->lock, LW_SHARED);
            bool ready = pandas_shared->ready;
            LWLockRelease(&pandas_shared->lock);

            if (ready)
                break;

            CHECK_FOR_INTERRUPTS();
            pg_usleep(10000); /* Sleep for 10ms */
        }

        /* Parse JSON result from worker */
        if (get_call_result_type(fcinfo, NULL, &tupdesc) != TYPEFUNC_COMPOSITE)
        {
            ereport(ERROR,
                    (errcode(ERRCODE_FEATURE_NOT_SUPPORTED),
                     errmsg("function returning record called in context that cannot accept type record")));
        }

        attinmeta = TupleDescGetAttInMetadata(tupdesc);
        funcctx->attinmeta = attinmeta;

        /* Parse JSON and store in user_fctx */
        /* Implement JSON parsing here if necessary */

        MemoryContextSwitchTo(oldcontext);
    }

    funcctx = SRF_PERCALL_SETUP();

    if (funcctx->call_cntr < 1) /* Assuming single row result for simplicity */
    {
        AttInMetadata *attinmeta = funcctx->attinmeta;

        /* Create tuple from JSON result */
        HeapTuple tuple;
        Datum result;
        bool isnull;
        Datum *values;
        int ncols = tupdesc->natts;

        values = palloc(sizeof(Datum) * ncols);

        for (int i = 0; i < ncols; i++)
        {
            Form_pg_attribute attr = TupleDescAttr(tupdesc, i);
            char *colname = NameStr(attr->attname);
            /* For simplicity, assume all columns are text */
            values[i] = CStringGetTextDatum(pandas_shared->result);
            isnull = false;
        }

        tuple = BuildTupleFromCStrings(attinmeta, (char **)values);
        result = HeapTupleGetDatum(tuple);

        SRF_RETURN_NEXT(funcctx, result);
    }
    else
    {
        SRF_RETURN_DONE(funcctx);
    }
}
