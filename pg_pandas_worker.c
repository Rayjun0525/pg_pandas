/* pg_pandas_worker.c
 *
 * Background worker for pg_pandas extension.
 * Processes Pandas operations in the background.
 */

#include "postgres.h"
#include "fmgr.h"
#include "postmaster/bgworker.h"
#include "storage/ipc.h"
#include "storage/shmem.h"
#include "storage/lwlock.h"
#include "miscadmin.h"

#include <unistd.h>
#include <string.h>

#include <Python.h>
#include <cjson/cJSON.h>

PG_MODULE_MAGIC;

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

/* Function declarations */
void _PG_init(void);
void pg_pandas_worker_main(Datum main_arg);
static void process_pandas_operation(void);

/* Register background worker on extension load */
void
_PG_init(void)
{
    if (!process_shared_preload_libraries_in_progress)
        return;

    /* Request shared memory space */
    RequestAddinShmemSpace(sizeof(PandasSharedData));
    RequestAddinLWLocks(1);

    /* Register the background worker */
    BackgroundWorker worker;
    memset(&worker, 0, sizeof(BackgroundWorker));
    strncpy(worker.bgw_name, "pg_pandas_worker", BGW_MAXLEN);
    strncpy(worker.bgw_type, "pg_pandas_worker", BGW_MAXLEN);
    worker.bgw_flags = BGWORKER_SHMEM_ACCESS | BGWORKER_BACKEND_DATABASE_CONNECTION;
    worker.bgw_start_time = BgWorkerStart_RecoveryFinished;
    worker.bgw_main = pg_pandas_worker_main;
    worker.bgw_restart_time = BGW_NEVER_RESTART;

    RegisterBackgroundWorker(&worker);
}

/* Background worker main function */
void
pg_pandas_worker_main(Datum main_arg)
{
    /* Establish connection to shared memory */
    bool found;
    pandas_shared = ShmemInitStruct("pg_pandas_shared", sizeof(PandasSharedData), &found);
    if (!found)
    {
        /* First time initialization */
        memset(pandas_shared, 0, sizeof(PandasSharedData));
        LWLockInitialize(&pandas_shared->lock, LWLockNewTrancheId());
    }

    /* Initialize Python */
    Py_Initialize();
    PyRun_SimpleString("import pandas as pd");
    PyRun_SimpleString("import psycopg2");
    PyRun_SimpleString("import os");
    PyRun_SimpleString("import json");

    /* Main loop */
    while (true)
    {
        /* Sleep to prevent busy waiting */
        pg_usleep(10000); /* Sleep for 10ms */

        /* Check if there is a new task */
        LWLockAcquire(&pandas_shared->lock, LW_SHARED);
        bool ready = pandas_shared->ready;
        LWLockRelease(&pandas_shared->lock);

        if (!ready)
            continue;

        /* Acquire exclusive lock to process */
        LWLockAcquire(&pandas_shared->lock, LW_EXCLUSIVE);

        /* Retrieve data and operation */
        char *data = pandas_shared->data;
        char *operation = pandas_shared->operation;

        /* Reset ready flag */
        pandas_shared->ready = false;

        /* Prepare Python code */
        char pycode[10000];
        snprintf(pycode, sizeof(pycode),
                 "import os\n"
                 "import pandas as pd\n"
                 "import psycopg2\n"
                 "import json\n"
                 "import sys\n"
                 "dbhost = os.environ.get('PGHOST', 'localhost')\n"
                 "dbport = os.environ.get('PGPORT', '5432')\n"
                 "dbuser = os.environ.get('PGUSER', 'postgres')\n"
                 "dbpass = os.environ.get('PGPASSWORD', '')\n"
                 "dbname = os.environ.get('PGDATABASE', 'postgres')\n"
                 "conn = psycopg2.connect(dbname=dbname, user=dbuser, password=dbpass, host=dbhost, port=dbport)\n"
                 "df = pd.read_json('%s')\n"
                 "user_operation = %s\n"
                 "result = user_operation(df)\n"
                 "result_json = result.to_json(orient='records')\n"
                 "print(result_json)\n",
                 data, operation);

        /* Execute Python code and capture output */
        FILE *fp = popen(pycode, "r");
        if (fp == NULL)
        {
            strncpy(pandas_shared->result, "Error executing Python code.", sizeof(pandas_shared->result) - 1);
            pandas_shared->result[sizeof(pandas_shared->result) - 1] = '\0';
            pclose(fp);
            LWLockRelease(&pandas_shared->lock);
            continue;
        }

        /* Read the result */
        if (fgets(pandas_shared->result, sizeof(pandas_shared->result), fp) == NULL)
        {
            strncpy(pandas_shared->result, "Error reading Python output.", sizeof(pandas_shared->result) - 1);
            pandas_shared->result[sizeof(pandas_shared->result) - 1] = '\0';
        }
        pclose(fp);

        /* Set ready flag */
        pandas_shared->ready = true;

        /* Release lock */
        LWLockRelease(&pandas_shared->lock);
    }

    /* Finalize Python (unreachable in current loop) */
    Py_Finalize();
}
