-- pg_pandas--1.0.sql

-- Create the pandas function
CREATE FUNCTION pandas(data anyelement, operation text)
RETURNS SETOF record
AS 'MODULE_PATHNAME', 'pg_pandas_fn'
LANGUAGE C VOLATILE;

-- Load the background worker
LOAD 'pg_pandas';
