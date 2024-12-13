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
#include "storage/lwlock.h"
#include "miscadmin.h"

#include <string.h>

PG_MODULE_MAGIC;

typedef struct {
    char data[8192];
    char operation[2048];
    char result[65536];
    bool ready;
    bool terminate;
    LWLock lock;
} PandasSharedData;

static PandasSharedData *pandas_shared = NULL;
int pg_pandas_parallel = 1;  /* Default value */

void _PG_init(void);
Datum pg_pandas_fn(PG_FUNCTION_ARGS);
PG_FUNCTION_INFO_V1(pg_pandas_fn);

/* Initialize configuration parameters */
void
_PG_init(void)
{
    DefineCustomIntVariable("pg_pandas.parallel",
                            "Number of parallel pg_pandas workers",
                            "Sets the number of parallel workers for pg_pandas.",
                            &pg_pandas_parallel,
                            1,
                            1,
                            1024, /* Set maximum to 1024 */
                            PGC_POSTMASTER,
                            0,
                            NULL, NULL, NULL);

    /* Allocate shared memory */
    pq_init_shared_memory();

    if (!process_shared_preload_libraries_in_progress)
    {
        elog(ERROR, "pg_pandas must be loaded via shared_preload_libraries");
    }

    if (pg_pandas_parallel < 1 || pg_pandas_parallel > 16)
    {
        elog(ERROR, "pg_pandas.parallel must be between 1 and 16");
    }

    /* Initialize shared memory */
    bool found;
    pandas_shared = (PandasSharedData *) ShmemInitStruct("pg_pandas_shared",
                                                         sizeof(PandasSharedData),
                                                         &found);

    if (!found)
    {
        /* Initialize shared memory */
        memset(pandas_shared, 0, sizeof(PandasSharedData));
        LWLockInitialize(&pandas_shared->lock, LWLockNewTrancheId());
    }
}

/* Function to execute Pandas operations */
Datum
pg_pandas_fn(PG_FUNCTION_ARGS)
{
    FuncCallContext *funcctx;
    MemoryContext oldcontext;

    if (SRF_IS_FIRSTCALL())
    {
        /* Switch to multi-call memory context */
        funcctx = SRF_FIRSTCALL_INIT();

        /* Allocate a tuple descriptor for result */
        TupleDesc tupdesc;
        if (get_call_result_type(fcinfo, NULL, &tupdesc) != TYPEFUNC_COMPOSITE)
        {
            ereport(ERROR,
                    (errcode(ERRCODE_FEATURE_NOT_SUPPORTED),
                     errmsg("function returning record called in context that cannot accept type record")));
        }

        funcctx->tuple_desc = BlessTupleDesc(tupdesc);
        funcctx->max_calls = 1;

        /* Setup shared memory and lock */
        if (pandas_shared == NULL)
        {
            ereport(ERROR, (errmsg("Shared memory not initialized")));
        }

        /* Acquire lock */
        LWLockAcquire(&pandas_shared->lock, LW_EXCLUSIVE);

        /* Check if worker is ready */
        if (!pandas_shared->ready)
        {
            LWLockRelease(&pandas_shared->lock);
            ereport(ERROR, (errmsg("pg_pandas worker is busy")));
        }

        /* Get input arguments */
        Datum input_data = PG_GETARG_DATUM(0);
        text *operation_text = PG_GETARG_TEXT_P(1);

        /* Serialize input data and operation to shared memory */
        size_t data_len = VARSIZE_ANY_EXHDR(input_data);
        size_t operation_len = VARSIZE_ANY_EXHDR(operation_text);

        if (data_len >= sizeof(pandas_shared->data))
        {
            LWLockRelease(&pandas_shared->lock);
            ereport(ERROR, (errmsg("Input data size exceeds buffer capacity")));
        }

        if (operation_len >= sizeof(pandas_shared->operation))
        {
            LWLockRelease(&pandas_shared->lock);
            ereport(ERROR, (errmsg("Operation size exceeds buffer capacity")));
        }

        memcpy(pandas_shared->data, VARDATA_ANY(input_data), data_len);
        memcpy(pandas_shared->operation, VARDATA_ANY(operation_text), operation_len);
        pandas_shared->ready = false;

        /* Signal worker to process */
        LWLockRelease(&pandas_shared->lock);
    }

    /* Per-call state */
    funcctx = SRF_PERCALL_SETUP();

    if (funcctx->call_cntr < 1)
    {
        /* Acquire lock to read result */
        LWLockAcquire(&pandas_shared->lock, LW_SHARED);

        if (!pandas_shared->ready)
        {
            LWLockRelease(&pandas_shared->lock);
            ereport(ERROR, (errmsg("pg_pandas operation not completed")));
        }

        /* Prepare result tuple */
        Datum result;
        bool isnull = false;
        result = CStringGetTextDatum(pandas_shared->result);

        LWLockRelease(&pandas_shared->lock);

        /* Return result */
        SRF_RETURN_NEXT(funcctx, result);
    }
    else
    {
        /* No more results */
        SRF_RETURN_DONE(funcctx);
    }
}