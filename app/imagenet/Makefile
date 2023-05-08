NU_PATH=../..
CALADAN_PATH=$(NU_PATH)/caladan

ROOT_PATH=$(CALADAN_PATH)
include $(CALADAN_PATH)/build/shared.mk

OPENCV_PATH=opencv/install

NCORES = $(shell nproc)

INC += -I$(NU_PATH)/inc -I$(CALADAN_PATH)/bindings/cc -I$(CALADAN_PATH)/ksched -I/usr/include/libnl3/ \
       -I$(OPENCV_PATH)/include/opencv4

override CXXFLAGS += -Wno-subobject-linkage -Wno-array-bounds -Wno-invalid-offsetof \
                     -Wno-deprecated-enum-enum-conversion -DNCORES=$(NCORES)
override LDFLAGS += -lcrypto -lpthread -lboost_program_options -lnuma  -ldl

librt_libs = $(CALADAN_PATH)/bindings/cc/librt++.a
lib_nu = $(NU_PATH)/libnu.a
lib_cv = -L$(OPENCV_PATH)/lib -lopencv_core -lopencv_imgproc -lopencv_imgcodecs

src = $(lib_src)
obj = $(src:.cpp=.o)
dep = $(obj:.o=.d)

image_src = image.cpp
image_obj = $(image_src:.cpp=.o)
dataloader_src = dataloader.cpp
dataloader_obj = $(dataloader_src:.cpp=.o)
baseline_src = baseline.cpp
baseline_obj = $(baseline_src:.cpp=.o)
baseline_gpu_src = baseline_gpu.cpp
baseline_gpu_obj = $(baseline_gpu_src:.cpp=.o)
distributed_src = distributed.cpp
distributed_obj = $(distributed_src:.cpp=.o)
time_calibrator_src = time_calibrator.cpp
time_calibrator_obj = $(time_calibrator_src:.cpp=.o)

all: baseline baseline_gpu distributed memtest time_calibrator

%.d: %.cpp
	@$(CXX) $(CXXFLAGS) $< -MM -MT $(@:.d=.o) >$@
%.o: %.cpp
	$(CXX) $(CXXFLAGS) -c $< -o $@

baseline: $(baseline_obj) $(librt_libs) $(RUNTIME_DEPS) $(lib_obj) $(image_obj) $(dataloader_obj)
	$(LDXX) -o $@ $(baseline_obj) $(image_obj) $(dataloader_obj) $(lib_nu) $(librt_libs) $(RUNTIME_LIBS) $(LDFLAGS) $(lib_cv)
baseline_gpu: $(baseline_gpu_obj) $(librt_libs) $(RUNTIME_DEPS) $(lib_obj) $(image_obj) $(dataloader_obj)
	$(LDXX) -o $@ $(baseline_gpu_obj) $(image_obj) $(dataloader_obj) $(lib_nu) $(librt_libs) $(RUNTIME_LIBS) $(LDFLAGS) $(lib_cv)
distributed: $(distributed_obj) $(librt_libs) $(RUNTIME_DEPS) $(lib_obj) $(image_obj) $(dataloader_obj)
	$(LDXX) -o $@ $(distributed_obj) $(image_obj) $(dataloader_obj) $(lib_nu) $(librt_libs) $(RUNTIME_LIBS) $(LDFLAGS) $(lib_cv)

memtest: memtest.o $(librt_libs) $(RUNTIME_DEPS) $(lib_obj) $(image_obj) $(dataloader_obj)
	$(LDXX) -o $@ memtest.o $(image_obj) $(dataloader_obj) $(lib_nu) $(librt_libs) $(RUNTIME_LIBS) $(LDFLAGS) $(lib_cv)
time_calibrator: $(time_calibrator_obj) $(librt_libs) $(RUNTIME_DEPS) $(lib_obj)
	$(LDXX) -o $@ $(time_calibrator_obj) $(lib_nu) $(librt_libs) $(RUNTIME_LIBS) $(LDFLAGS)

ifneq ($(MAKECMDGOALS),clean)
-include $(dep)
endif

.PHONY: clean
clean:
	rm -f $(dep) baseline baseline.o baseline_gpu baseline_gpu.o time_calibrator time_calibrator.o \
              distributed distributed.o image.o dataloader.o memtest memtest.o
