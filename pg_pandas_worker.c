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
#include <signal.h>

#include <Python.h>
#include <cjson/cJSON.h>

PG_MODULE_MAGIC;

/* Shared memory structure for communication */
typedef struct {
    char data[8192];
    char operation[2048];
    char result[65536];
    bool ready;
    bool terminate;
    LWLock lock;
} PandasSharedData;

/* Pointer to shared memory */
static PandasSharedData *pandas_shared = NULL;

/* Function declarations */
void _PG_init(void);
void pg_pandas_worker_main(Datum main_arg);
static void process_pandas_operation(void);

/* List of allowed Python modules */
const char *allowed_modules[] = {"pandas", "numpy", "json", NULL};

/* Signal handler for shutdown */
static void
handle_shutdown(SIGNAL_ARGS)
{
    int save_errno = errno;
    if (pandas_shared != NULL)
    {
        LWLockAcquire(&pandas_shared->lock, LW_EXCLUSIVE);
        pandas_shared->terminate = true;
        LWLockRelease(&pandas_shared->lock);
    }
    errno = save_errno;
}

/* Initialize secure Python environment */
static void initialize_secure_python(void) {
    Py_Initialize();

    /* Import only allowed modules */
    for (int i = 0; allowed_modules[i] != NULL; i++) {
        PyRun_SimpleString("import sys");
        char import_command[256];
        snprintf(import_command, sizeof(import_command), "import %s", allowed_modules[i]);
        PyRun_SimpleString(import_command);
    }

    /* Restrict built-in functions */
    PyRun_SimpleString(
        "import builtins\n"
        "allowed_builtins = {'print': print, 'len': len, 'range': range}\n"
        "builtins.__dict__.clear()\n"
        "builtins.__dict__.update(allowed_builtins)\n"
    );
}

/* Register background worker on extension load */
void
_PG_init(void)
{
    BackgroundWorker worker;
    memset(&worker, 0, sizeof(BackgroundWorker));
    worker.bgw_name = "pg_pandas_worker";
    worker.bgw_type = "pg_pandas_worker";
    worker.bgw_flags = BGWORKER_SHMEM_ACCESS | BGWORKER_BACKEND_DATABASE_CONNECTION;
    worker.bgw_start_time = BgWorkerStart_RecoveryFinished;
    worker.bgw_main = pg_pandas_worker_main;
    worker.bgw_shutdown = handle_shutdown;
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
        pandas_shared->terminate = false;
    }

    /* Set up signal handler for graceful shutdown */
    pqsignal(SIGTERM, handle_shutdown);
    BackgroundWorkerUnblockSignals();

    /* Initialize Python */
    initialize_secure_python();

    /* Main loop */
    while (true)
    {
        /* Sleep to prevent busy waiting */
        pg_usleep(10000); /* Sleep for 10ms */

        /* Check for termination signal */
        if (pandas_shared->terminate)
            break;

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

        /* Execute Python code */
        PyObject *pModule = PyImport_AddModule("__main__");
        PyObject *pDict = PyModule_GetDict(pModule);
        PyObject *pResult;

        /* Execute prepared Python code */
        pResult = PyRun_String(pycode, Py_file_input, pDict, pDict);
        if (pResult == NULL)
        {
            PyErr_Print();
            ereport(LOG, (errmsg("Error executing Python code.")));
            /* Handle error: reset ready flag */
            pandas_shared->ready = true;
            LWLockRelease(&pandas_shared->lock);
            continue;
        }

        /* Retrieve result */
        PyObject *pValue = PyObject_CallMethod(pModule, "print", "O", Py_None);
        if (pValue == NULL)
        {
            PyErr_Print();
            ereport(LOG, (errmsg("Error retrieving Python code output.")));
        }
        else
        {
            /* Convert result to C string */
            const char *result_str = PyUnicode_AsUTF8(pValue);
            if (result_str)
            {
                strncpy(pandas_shared->result, result_str, sizeof(pandas_shared->result) - 1);
                pandas_shared->result[sizeof(pandas_shared->result) - 1] = '\0';
            }
            Py_DECREF(pValue);
        }

        Py_DECREF(pResult);

        /* Set ready flag and release lock */
        pandas_shared->ready = true;
        LWLockRelease(&pandas_shared->lock);
    }

    /* Finalize Python */
    Py_Finalize();
}
