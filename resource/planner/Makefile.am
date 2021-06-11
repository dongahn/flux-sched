AM_CXXFLAGS = \
    $(WARNING_CXXFLAGS) \
    -Wno-unused-local-typedefs \
    -Wno-deprecated-declarations \
    -Wno-unused-variable \
    -Wno-error \
    $(CODE_COVERAGE_CXXFLAGS)

AM_CPPFLAGS = \
	-I$(top_srcdir) \
	$(CZMQ_CFLAGS)

AM_LDFLAGS = \
	$(CODE_COVERAGE_LDFLAGS)

SUBDIRS = . test

noinst_LTLIBRARIES = libplanner.la
noinst_HEADERS = planner.h \
	planner_multi.h \
	planner_internal_tree.hpp \
	scheduled_point_tree.hpp \
	mintime_resource_tree.hpp

libplanner_la_SOURCES = planner.cpp \
	planner_multi.cpp \
	scheduled_point_tree.cpp \
	mintime_resource_tree.cpp
libplanner_la_CXXFLAGS = $(AM_CXXFLAGS) -I$(top_srcdir)/resource/planner
libplanner_la_LIBADD = \
	$(top_builddir)/src/common/libutil/libutil.la \
	$(top_builddir)/src/common/libczmqcontainers/libczmqcontainers.la \
	$(CZMQ_LIBS)
