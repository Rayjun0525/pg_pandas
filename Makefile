# Makefile for pg_pandas extension

EXTENSION = pg_pandas
MODULES = pg_pandas pg_pandas_worker
DATA = pg_pandas--1.0.sql
PG_CONFIG = pg_config
PGXS := $(shell $(PG_CONFIG) --pgxs)
include $(PGXS)

# Link against Python library using python3-config
PYTHON_CONFIG ?= python3-config
PYTHON_INCLUDE = $(shell $(PYTHON_CONFIG) --includes)
PYTHON_LIBS = $(shell $(PYTHON_CONFIG) --ldflags)

CFLAGS += $(PYTHON_INCLUDE)
LDFLAGS += $(PYTHON_LIBS)

# Define parallel workers
CPPFLAGS += -DPG_PANDAS_PARALLEL=$(PG_PANDAS_PARALLEL)