# src/test/modules/gp_worker_spi/Makefile

MODULES = gp_worker_spi

REGRESS = gp_worker_spi

# enable our module in shared_preload_libraries for dynamic bgworkers
REGRESS_OPTS = --dbname=contrib_regression

ifdef USE_PGXS
PG_CONFIG = pg_config
PGXS := $(shell $(PG_CONFIG) --pgxs)
include $(PGXS)
else
subdir = src/test/modules/gp_worker_spi
top_builddir = ../../../..
include $(top_builddir)/src/Makefile.global
include $(top_srcdir)/contrib/contrib-global.mk
endif
