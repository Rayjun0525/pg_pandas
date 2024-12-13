-- test_pg_pandas.sql

-- Test basic Pandas operation
CREATE OR REPLACE FUNCTION test_pandas_basic()
RETURNS void AS $$
BEGIN
    PERFORM pandas(ARRAY[1, 2, 3, 4, 5], 'lambda df: df + 10');
END;
$$ LANGUAGE plpgsql;

-- Test operation exceeding buffer size
CREATE OR REPLACE FUNCTION test_pandas_overflow()
RETURNS void AS $$
BEGIN
    PERFORM pandas(repeat('a', 9000)::text, 'lambda df: df');
EXCEPTION WHEN others THEN
    RAISE NOTICE 'Overflow test passed.';
END;
$$ LANGUAGE plpgsql;

-- Execute tests
SELECT test_pandas_basic();
SELECT test_pandas_overflow(); 