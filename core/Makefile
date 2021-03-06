# Disable all implicit Makefile rules
.SUFFIXES:

CXX ?= g++
PROTOC ?= protoc

# e.g., 4.9.3 -> 40903
CXXVERSION := $(shell $(CXX) -dumpversion | sed -e 's/\.\([0-9][0-9]\)/\1/g' -e 's/\.\([0-9]\)/0\1/g' -e 's/^[0-9]\{3,4\}$$/&00/')

CXXVERSION_4_8_OR_HIGHER := $(shell expr $(CXXVERSION) \>= 40800)
ifeq "$(CXXVERSION_4_8_OR_HIGHER)" "0"
  $(error g++ 4.8 or higher is required)
endif

RTE_SDK ?= $(abspath ../deps/dpdk-16.07)
RTE_TARGET ?= $(shell uname -m)-native-linuxapp-gcc
DPDK_LIB = dpdk

ifneq ($(wildcard $(RTE_SDK)/$(RTE_TARGET)/*),)
    DPDK_INC_DIR = $(RTE_SDK)/$(RTE_TARGET)/include
    DPDK_LIB_DIR = $(RTE_SDK)/$(RTE_TARGET)/lib
else ifneq ($(wildcard $(RTE_SDK)/build/*),)
    # if the user didn't do "make install" for DPDK
    DPDK_INC_DIR = $(RTE_SDK)/build/include
    DPDK_LIB_DIR = $(RTE_SDK)/build/lib
else
    ifeq ($(words $(MAKECMDGOALS)),1)
        ifneq ($(MAKECMDGOALS),clean)
            $(error DPDK is not available. \
                    Make sure $(abspath $(RTE_SDK)) is available and built)
        endif
    endif
endif

CXXFLAGS += -std=gnu++11 -g3 -ggdb3 -Ofast -march=native \
	    -Werror -Wall \
	    -isystem $(DPDK_INC_DIR) \
	    -D_GNU_SOURCE

HARDCORE = -Wextra -Wcast-align \
	   -Wno-missing-field-initializers	# FIXME

# -Wshadow should not be used for g++ 4.x, as it has too many false positives
CXXVERSION_5_OR_HIGHER := $(shell expr $(CXXVERSION) \>= 50000)
ifeq "$(CXXVERSION_5_OR_HIGHER)" "1"
  HARDCORE += -Wshadow
endif

LDFLAGS += -rdynamic -L$(DPDK_LIB_DIR) -Wl,-rpath=$(DPDK_LIB_DIR) -pthread \
	   -static-libstdc++

LIBS += -Wl,-non_shared \
	-Wl,--whole-archive -l$(DPDK_LIB) -Wl,--no-whole-archive \
	-lglog -lgflags -lprotobuf -lgrpc++ -lgrpc \
	-lssl -lcrypto -lunwind -llzma -lpcap -lz \
	-Wl,-call_shared \
	-ldl

ifdef SANITIZE
    LDFLAGS += -fsanitize=address -static-libasan
    CXXFLAGS += -fsanitize=address -fno-omit-frame-pointer
endif

ifdef COVERAGE
    LDFLAGS += --coverage
    CXXFLAGS += --coverage
endif


-include extra.mk

PROTO_DIR = ../protobuf
PROTOS = $(wildcard $(PROTO_DIR)/*.proto)
PROTO_SRCS = $(patsubst %.proto,%.pb.cc, $(notdir $(PROTOS)))
PROTO_SRCS += $(patsubst %.proto,%.grpc.pb.cc, $(notdir $(PROTOS)))
PROTO_HEADERS = $(patsubst %.cc,%.h, $(PROTO_SRCS))
PROTOCFLAGS += --proto_path=$(PROTO_DIR) --cpp_out=. --grpc_out=. --plugin=protoc-gen-grpc=`which grpc_cpp_plugin`

ALL_SRCS = $(wildcard *.cc utils/*.cc modules/*.cc drivers/*.cc)
HEADERS = $(wildcard *.h utils/*.h modules/*.h drivers/*.h)

TEST_SRCS = $(filter %_test.cc gtest_main.cc, $(ALL_SRCS))
TEST_OBJS = $(TEST_SRCS:.cc=.o)
TEST_EXEC = $(filter-out gtest_main, $(TEST_OBJS:%.o=%))
TEST_ALL_EXEC = all_test

BENCH_SRCS = $(filter %_bench.cc, $(ALL_SRCS))
BENCH_OBJS = $(BENCH_SRCS:.cc=.o)
BENCH_EXEC = $(BENCH_OBJS:%.o=%)

SRCS = $(filter-out $(TEST_SRCS) $(BENCH_SRCS), $(ALL_SRCS)) $(PROTO_SRCS)
OBJS = $(SRCS:.cc=.o)

EXEC = bessd
DEPS = .make.dep

GTEST_DIR = /usr/src/gtest

# if multiple targets are specified, do them one by one */
ifneq ($(words $(MAKECMDGOALS)),1)

.NOTPARALLEL:
$(sort all $(MAKECMDGOALS)):
	@$(MAKE) --no-print-directory -f $(firstword $(MAKEFILE_LIST)) $@

else

# parallel build by default
CORES ?= $(shell nproc || echo 1)
MAKEFLAGS += -j $(CORES)
VERBOSE ?= 0

.PHONY: all clean tags deps cscope tests benchmarks protobuf

all: $(EXEC) tests benchmarks

clean:
	rm -f $(DEPS) $(EXEC) *_test */*_test *_bench */*_bench \
		*.a *.pb.* *.o */*.o *.gcda *.gcno */*.gcda */*.gcno

tags:
	@ctags -R *

deps: $(DEPS)

cscope:
	@rm -f cscope.*
	@find . -name "*.c" -o -name "*.h" > cscope.files
	cscope -b -q -k
	@rm -f cscope.files

tests: $(TEST_OBJS) $(TEST_EXEC) $(TEST_ALL_EXEC)

benchmarks: $(BENCH_OBJS) $(BENCH_EXEC)

protobuf: $(PROTO_SRCS)

# This build wrapper takes 4 parameters:
# $(1): build type (CXX, LD, ...)
# $(2): Make target
# $(3): Make prerequisites
# $(4): command
define BUILD
$(2): $(3)
	$$(eval _TYPE = $$(strip $(1)))
	$$(eval _CMD = $$(strip $(4)))
	@if [ $$(VERBOSE) -eq 0 ]; then \
		printf "%-11s %s\n" [$$(_TYPE)] $$@; \
	else \
		printf "%-11s %s\n" [$$(_TYPE)] $$(_CMD); \
	fi
	@if ! $$(_CMD); then \
		echo "Error: \033[0;31m$$@"; \
		echo "\033[0;33m$$(_CMD)\033[0m"; \
		false; \
	fi
endef

$(eval $(call BUILD, \
	PROTOC, \
	%.pb.cc %.pb.h %.grpc.pb.cc %.grpc.pb.h, \
	$$(PROTO_DIR)/%.proto, \
	$$(PROTOC) $$< $$(PROTOCFLAGS)))

$(eval $(call BUILD, \
	DEPS, \
	$$(DEPS), \
	$$(PROTO_HEADERS) $$(ALL_SRCS) $$(HEADERS), \
	$$(CXX) $$(CXXFLAGS) -MM $$(ALL_SRCS) | sed 's|\(.*\)\.o: \(.*\)\.cc|\2.o: \2.cc|' > $$(DEPS)))

$(eval $(call BUILD, \
	CXX, \
	%.pb.o, \
	%.pb.cc, \
	$$(CXX) -o $$@ -c $$< $$(CXXFLAGS)))

$(eval $(call BUILD, \
	CXX, \
	%.o, \
	%.cc, \
	$$(CXX) -o $$@ -c $$< $$(CXXFLAGS) $$(HARDCORE)))

$(eval $(call BUILD, \
	LD, \
	$$(EXEC), \
	$$(OBJS), \
	$$(CXX) -o $$@ $$^ $$(LDFLAGS) $$(LIBS)))

$(eval $(call BUILD, \
	TEST_CXX, \
	%_test.o, \
	%_test.cc, \
	$$(CXX) -o $$@ -c $$< $$(CXXFLAGS) $$(HARDCORE)))

$(eval $(call BUILD, \
	TEST_LD, \
	%_test, \
	%_test.o gtest-all.o gtest_main.o bess.a, \
	$$(CXX) -o $$@ $$^ $$(LDFLAGS) $(LIBS)))

$(eval $(call BUILD, \
	TEST_LD, \
	$$(TEST_ALL_EXEC), \
	$$(TEST_OBJS) gtest-all.o bess.a, \
	$$(CXX) -o $$@ $$^ $$(LDFLAGS) $(LIBS)))

$(eval $(call BUILD, \
	TEST_CXX, \
	gtest-all.o, \
	$$(GTEST_DIR)/src/gtest-all.cc, \
	$$(CXX) -o $$@ -c $$< -I$$(GTEST_DIR) $$(CXXFLAGS) $$(HARDCORE)))

$(eval $(call BUILD, \
	BENCH_CXX, \
	%_bench.o, \
	%_bench.cc, \
	$$(CXX) -o $$@ -c $$< $$(CXXFLAGS) $$(HARDCORE)))

$(eval $(call BUILD, \
	BENCH_LD, \
	%_bench, \
	%_bench.o %.o bess.a, \
	$$(CXX) -o $$@ $$^ $$(LDFLAGS) -lbenchmark $(LIBS)))

CORE_OBJS = $(filter-out main.o %_bench.o, $(OBJS))

$(eval $(call BUILD, \
	AR, \
	bess.a, \
	$(CORE_OBJS), \
	$$(AR) rcs $$@ $$^))

ifneq ($(MAKECMDGOALS),clean)
	-include $(DEPS)
endif

endif
