-- initialize database
-- start_ignore
DROP TABLE IF EXISTS worker_spi.counted1;
DROP SCHEMA IF EXISTS worker_spi;
-- end_ignore
CREATE SCHEMA worker_spi;
CREATE TABLE worker_spi.counted1(val int);

-- create raise notice function
CREATE FUNCTION raise_notice_func(gp_segment_id int) RETURNS int LANGUAGE plpgsql AS $$
DECLARE
BEGIN
RAISE NOTICE 'this is raise function';
RETURN gp_segment_id;
END;
$$;

-- start_ignore
\! gpconfig -c shared_preload_libraries -v 'gp_worker_spi'
\! gpstop -raf
-- end_ignore

\c

-- wait until the worker completes its initialization
DO $$
DECLARE
	visible bool;
	loops int := 0;
BEGIN
	LOOP
		visible := table_name IS NOT NULL
			FROM information_schema.tables
			WHERE table_name = 'counted1';
		IF visible OR loops > 120 * 10 THEN EXIT; END IF;
		PERFORM pg_sleep(0.1);
		loops := loops + 1;
	END LOOP;
END
$$;

-- wait until the worker has processed the tuple
DO $$
DECLARE
	count int;
	loops int := 0;
BEGIN
	LOOP
		count := count(*) FROM worker_spi.counted1;
		IF count > 0 OR loops > 120 * 10 THEN EXIT; END IF;
		PERFORM pg_sleep(0.1);
		loops := loops + 1;
	END LOOP;
END
$$;

-- the result should be (0, 1, 2)
SELECT DISTINCT val FROM worker_spi.counted1 ORDER BY val;

-- clean database
DROP FUNCTION raise_notice_func(gp_segment_id int);
DROP TABLE worker_spi.counted1;
DROP SCHEMA worker_spi;
-- start_ignore
\! gpconfig -c shared_preload_libraries -v ''
\! gpstop -raf
-- end_ignore