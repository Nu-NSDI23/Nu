NU_PATH=../../../
CALADAN_PATH=$(NU_PATH)/caladan

ROOT_PATH=$(CALADAN_PATH)
include $(CALADAN_PATH)/build/shared.mk

NCORES = $(shell nproc)

INC += -Ithrift/lib/cpp/src/ -I.

override CXXFLAGS += -Wno-subobject-linkage -Wno-array-bounds -Wno-invalid-offsetof \
                     -DNCORES=$(NCORES)
override LDFLAGS += -Lthrift/lib/cpp/.libs -lthrift -lpthread

lib_src = $(wildcard gen-cpp/*.cpp)
lib_obj = $(lib_src:.cpp=.o)

librt_libs = $(CALADAN_PATH)/bindings/cc/librt++.a

src = $(lib_src)
obj = $(src:.cpp=.o)
dep = $(obj:.o=.d)

client_src = client.cpp
client_obj = $(client_src:.cpp=.o)

server_src = server.cpp
server_obj = $(server_src:.cpp=.o)

main_src = main.cpp
main_obj = $(main_src:.cpp=.o)

all: server client

%.d: %.cpp
	@$(CXX) $(CXXFLAGS) $< -MM -MT $(@:.d=.o) >$@
%.o: %.cpp
	$(CXX) $(CXXFLAGS) -c $< -o $@

server: $(server_obj) $(librt_libs) $(RUNTIME_DEPS) $(lib_obj)
	$(LDXX) -o $@ $(server_obj) $(librt_libs) $(RUNTIME_LIBS) $(lib_obj) $(LDFLAGS)
client: $(client_obj) $(librt_libs) $(RUNTIME_DEPS) $(lib_obj)
	$(LDXX) -o $@ $(client_obj) $(librt_libs) $(RUNTIME_LIBS) $(lib_obj) $(LDFLAGS)

ifneq ($(MAKECMDGOALS),clean)
-include $(dep)
endif

.PHONY: clean
clean:
	rm -f $(dep) server client *.o $(lib_obj)
