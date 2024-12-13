# Makefile for pg_pandas extension

EXTENSION = pg_pandas
MODULES = pg_pandas pg_pandas_worker
DATA = pg_pandas--1.0.sql
PG_CONFIG = pg_config
PGXS := $(shell $(PG_CONFIG) --pgxs)
include $(PGXS)

# Link against Python library
SHLIB_LINK = -lpython3
CFLAGS += -I/usr/include/python3
