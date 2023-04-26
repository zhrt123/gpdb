-- This test is used to test if the segments involved in a dtx is correct

CREATE TABLE test_dispatch_normal (c1 int, c2 int) DISTRIBUTED BY (c1);
INSERT INTO test_dispatch_normal values (1,2),(2,3),(5,6);

SET Test_print_direct_dispatch_info = ON;

-- 1PC, dispatch commit message to 1 segments 
BEGIN;
UPDATE test_dispatch_normal SET c2 = 1 WHERE c1 = 1;
SELECT * FROM test_dispatch_normal WHERE c1 = 1;
COMMIT;

-- 1PC, but dispatch commit message to 2 segments
BEGIN;
UPDATE test_dispatch_normal SET c2 = 1 WHERE c1 = 1;
SELECT * FROM test_dispatch_normal WHERE c1 = 1 or c1 = 2;
COMMIT;

-- 1PC, but dispatch commit message to all segments
BEGIN;
UPDATE test_dispatch_normal SET c2 = 1 WHERE c1 = 1;
SELECT * FROM test_dispatch_normal;
COMMIT;

-- 2PC, dispatch prepare message to 2 segments, dispatch commit prepared message to 2 segments
-- dispatch commit one-phase message to 1 segment
BEGIN;
UPDATE test_dispatch_normal SET c2 = 1 WHERE c1 = 1;
UPDATE test_dispatch_normal SET c2 = 2 WHERE c1 = 2;
SELECT * FROM test_dispatch_normal WHERE c1 = 5;
COMMIT;

-- 2PC, dispatch prepare and commit message to all segments
BEGIN;
UPDATE test_dispatch_normal SET c2 = 1 WHERE c1 = 1;
UPDATE test_dispatch_normal SET c2 = 2 WHERE c1 = 2;
UPDATE test_dispatch_normal SET c2 = 5 WHERE c1 = 5;
SELECT * FROM test_dispatch_normal WHERE c1 = 5;
COMMIT;

-- Clean up
DROP TABLE test_dispatch_normal;
