NU_PATH=../../..
CALADAN_PATH=$(NU_PATH)/caladan

ROOT_PATH=$(CALADAN_PATH)
include $(CALADAN_PATH)/build/shared.mk

NCORES = $(shell nproc)

INC += -I$(NU_PATH)/inc -I$(CALADAN_PATH)/bindings/cc -I$(CALADAN_PATH)/ksched -I/usr/include/libnl3/

override CXXFLAGS += -Wno-subobject-linkage -Wno-array-bounds -Wno-invalid-offsetof \
                     -Wno-deprecated-enum-enum-conversion -DNCORES=$(NCORES)
override LDFLAGS += -lcrypto -lpthread -lboost_program_options -lnuma -ldl

librt_libs = $(CALADAN_PATH)/bindings/cc/librt++.a
lib_nu = $(NU_PATH)/libnu.a

quicksand_src = quicksand.cpp
quicksand_obj = $(quicksand_src:.cpp=.o)

all: quicksand

%.d: %.cpp
	@$(CXX) $(CXXFLAGS) $< -MM -MT $(@:.d=.o) >$@
%.o: %.cpp
	$(CXX) $(CXXFLAGS) -c $< -o $@

quicksand: $(quicksand_obj) $(librt_libs) $(RUNTIME_DEPS) $(lib_obj) $(image_obj)
	$(LDXX) -o $@ $(quicksand_obj) $(lib_nu) $(librt_libs) $(RUNTIME_LIBS) $(LDFLAGS)

ifneq ($(MAKECMDGOALS),clean)
-include $(dep)
endif

.PHONY: clean
clean:
	rm -f $(dep) quicksand quicksand.o

