/*-------------------------------------------------------------------------
 *
 * gp_worker_spi.c
 *
 * Portions Copyright (c) 2023-Present VMware, Inc. or its affiliates.
 *
 *
 * IDENTIFICATION
 *	   src/test/modules/gp_worker_spi/gp_worker_spi.c
 *
 *-------------------------------------------------------------------------
 */
#include "postgres.h"

/* These are always necessary for a bgworker */
#include "miscadmin.h"
#include "postmaster/bgworker.h"
#include "storage/ipc.h"
#include "storage/latch.h"
#include "storage/lwlock.h"
#include "storage/proc.h"
#include "storage/shmem.h"

/* these headers are used by this particular worker's code */
#include "access/xact.h"
#include "executor/spi.h"
#include "fmgr.h"
#include "lib/stringinfo.h"
#include "pgstat.h"
#include "utils/builtins.h"
#include "utils/snapmgr.h"
#include "tcop/utility.h"

PG_MODULE_MAGIC;

void _PG_init(void);
void gp_worker_spi_main(Datum) pg_attribute_noreturn();

/* flags set by signal handlers */
static volatile sig_atomic_t got_sighup = false;
static volatile sig_atomic_t got_sigterm = false;

/* GUC variables */
static int gp_worker_spi_naptime = 10;
static int gp_worker_spi_total_workers = 1;
static char *gp_worker_spi_database = NULL;

typedef struct worktable
{
	const char *name;
} worktable;

/*
 * Signal handler for SIGTERM
 *		Set a flag to let the main loop to terminate, and set our latch to wake
 *		it up.
 */
static void
gp_worker_spi_sigterm(SIGNAL_ARGS)
{
	int save_errno = errno;

	got_sigterm = true;
	SetLatch(MyLatch);

	errno = save_errno;
}

/*
 * Signal handler for SIGHUP
 *		Set a flag to tell the main loop to reread the config file, and set
 *		our latch to wake it up.
 */
static void
gp_worker_spi_sighup(SIGNAL_ARGS)
{
	int save_errno = errno;

	got_sighup = true;
	SetLatch(MyLatch);

	errno = save_errno;
}

/*
 * Entrypoint of this module.
 *
 * We register more than one worker process here, to demonstrate how that can
 * be done.
 */
void _PG_init(void)
{
	BackgroundWorker worker;
	unsigned int i;

	/* get the configuration */
	DefineCustomIntVariable("gp_worker_spi.naptime",
							"Duration between each check (in seconds).",
							NULL,
							&gp_worker_spi_naptime,
							10,
							1,
							INT_MAX,
							PGC_SIGHUP,
							0,
							NULL,
							NULL,
							NULL);

	if (!process_shared_preload_libraries_in_progress)
		return;

	DefineCustomIntVariable("gp_worker_spi.total_workers",
							"Number of workers.",
							NULL,
							&gp_worker_spi_total_workers,
							1,
							1,
							100,
							PGC_POSTMASTER,
							0,
							NULL,
							NULL,
							NULL);

	DefineCustomStringVariable("gp_worker_spi.database",
							   "Database to connect to.",
							   NULL,
							   &gp_worker_spi_database,
							   "contrib_regression",
							   PGC_POSTMASTER,
							   0,
							   NULL, NULL, NULL);

	/* set up common data for all our workers */
	memset(&worker, 0, sizeof(worker));
	worker.bgw_flags = BGWORKER_SHMEM_ACCESS |
					   BGWORKER_BACKEND_DATABASE_CONNECTION;
	worker.bgw_start_time = BgWorkerStart_RecoveryFinished;
	worker.bgw_restart_time = BGW_NEVER_RESTART;
	sprintf(worker.bgw_library_name, "gp_worker_spi");
	sprintf(worker.bgw_function_name, "gp_worker_spi_main");
	worker.bgw_notify_pid = 0;
	worker.bgw_start_rule = NULL;

	/*
	 * Now fill in worker-specific data, and do the actual registrations.
	 */
	for (i = 1; i <= gp_worker_spi_total_workers; i++)
	{
		snprintf(worker.bgw_name, BGW_MAXLEN, "gp_worker_spi worker %d", i);
		snprintf(worker.bgw_type, BGW_MAXLEN, "gp_worker_spi");
		worker.bgw_main_arg = Int32GetDatum(i);

		RegisterBackgroundWorker(&worker);
	}
}

void gp_worker_spi_main(Datum main_arg)
{
	int index = DatumGetInt32(main_arg);
	worktable *table;
	StringInfoData buf;
	char name[20];

	table = palloc(sizeof(worktable));
	sprintf(name, "counted%d", index);
	table->name = pstrdup(name);

	/* Establish signal handlers before unblocking signals. */
	pqsignal(SIGHUP, gp_worker_spi_sighup);
	pqsignal(SIGTERM, gp_worker_spi_sigterm);

	/* We're now ready to receive signals */
	BackgroundWorkerUnblockSignals();

	/* Connect to our database */
	elog(WARNING, "xxxx %s", gp_worker_spi_database);
	BackgroundWorkerInitializeConnection(gp_worker_spi_database, NULL, 0);

	/*
	 * Quote identifiers passed to us. Note that this must be done after
	 * gp_initialize_worker_spi, because that routine assumes the names are not
	 * quoted.
	 *
	 * Note some memory might be leaked here.
	 */
	table->name = quote_identifier(table->name);

	initStringInfo(&buf);
	appendStringInfo(&buf,
					 "WITH notic AS ("
					 "SELECT raise_notice_func(gp_segment_id) AS r "
					 "FROM gp_dist_random('gp_id')) "
					 "INSERT INTO worker_spi.%s(val) SELECT r FROM notic",
					 table->name, table->name);

	/*
	 * Main loop: do this until the SIGTERM handler tells us to terminate
	 */
	while (!got_sigterm)
	{
		int ret;

		/*
		 * Background workers mustn't call usleep() or any direct equivalent:
		 * instead, they may wait on their process latch, which sleeps as
		 * necessary, but is awakened if postmaster dies.  That way the
		 * background process goes away immediately in an emergency.
		 */
		(void)WaitLatch(MyLatch,
						WL_LATCH_SET | WL_TIMEOUT | WL_EXIT_ON_PM_DEATH,
						gp_worker_spi_naptime * 1000L,
						PG_WAIT_EXTENSION);
		ResetLatch(MyLatch);

		CHECK_FOR_INTERRUPTS();

		/*
		 * In case of a SIGHUP, just reload the configuration.
		 */
		if (got_sighup)
		{
			got_sighup = false;
			ProcessConfigFile(PGC_SIGHUP);
		}

		/*
		 * Start a transaction on which we can run queries.  Note that each
		 * StartTransactionCommand() call should be preceded by a
		 * SetCurrentStatementStartTimestamp() call, which sets both the time
		 * for the statement we're about the run, and also the transaction
		 * start time.  Also, each other query sent to SPI should probably be
		 * preceded by SetCurrentStatementStartTimestamp(), so that statement
		 * start time is always up to date.
		 *
		 * The SPI_connect() call lets us run queries through the SPI manager,
		 * and the PushActiveSnapshot() call creates an "active" snapshot
		 * which is necessary for queries to have MVCC data to work on.
		 *
		 * The pgstat_report_activity() call makes our activity visible
		 * through the pgstat views.
		 */
		SetCurrentStatementStartTimestamp();
		StartTransactionCommand();
		SPI_connect();
		PushActiveSnapshot(GetTransactionSnapshot());
		pgstat_report_activity(STATE_RUNNING, buf.data);

		/* We can now execute queries via SPI */
		ret = SPI_execute(buf.data, false, 0);

		if (ret != SPI_OK_INSERT)
			elog(FATAL, "cannot select from table worker_spi.%s: error code %d",
				 table->name, ret);

		/*
		 * And finish our transaction.
		 */
		SPI_finish();
		PopActiveSnapshot();
		CommitTransactionCommand();
		pgstat_report_stat(false);
		pgstat_report_activity(STATE_IDLE, NULL);
	}

	proc_exit(1);
}
