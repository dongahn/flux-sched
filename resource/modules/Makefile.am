AM_CXXFLAGS = \
    $(WARNING_CXXFLAGS) \
    -Wno-unused-local-typedefs \
    -Wno-deprecated-declarations \
    -Wno-unused-variable \
    -Wno-error \
    $(CODE_COVERAGE_CXXFLAGS)

AM_LDFLAGS = $(CODE_COVERAGE_LDFLAGS)

AM_CPPFLAGS = -I$(top_srcdir) $(CZMQ_CFLAGS) $(FLUX_CORE_CFLAGS) \
          $(BOOST_CPPFLAGS)

fluxmod_LTLIBRARIES = sched-fluxion-resource.la

#
# resource service module
#
sched_fluxion_resource_la_SOURCES = \
    resource_match.cpp
sched_fluxion_resource_la_CXXFLAGS = \
    -Wc,-Xclang,-load,-Xclang,/usr/global/tools/flux/toss_3_x86_64_ib/perfflow-0.0.1/lib/libWeavePass.so \
    $(AM_CXXFLAGS) \
    $(JOBSPEC_CFLAGS) \
    $(FLUX_HOSTLIST_CFLAGS) \
    $(FLUX_CORE_CFLAGS)
sched_fluxion_resource_la_LIBADD = \
    ../libresource.la \
    $(FLUX_IDSET_LIBS) \
    $(FLUX_HOSTLIST_LIBS) \
    $(FLUX_CORE_LIBS) \
    $(DL_LIBS) \
    $(HWLOC_LIBS) \
    $(UUID_LIBS) \
    -lperfflow_runtime \
    $(JANSSON_LIBS) \
    $(CZMQ_LIBS) \
    $(BOOST_LDFLAGS)
sched_fluxion_resource_la_LDFLAGS = \
    -L/usr/global/tools/flux/toss_3_x86_64_ib/perfflow-0.0.1/lib \
    $(AM_LDFLAGS) \
    $(fluxmod_ldflags) -module

