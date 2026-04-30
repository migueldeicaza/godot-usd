SCONS ?= scons
PLATFORM ?= macos
ARCH ?= arm64
JOBS ?= 8

SCONS_ARGS := platform=$(PLATFORM) arch=$(ARCH)

ifdef GODOT_CPP_PATH
SCONS_ARGS += godot_cpp_path=$(GODOT_CPP_PATH)
endif

ifdef USD_SDK_PATH
SCONS_ARGS += usd_sdk_path=$(USD_SDK_PATH)
endif

ifdef TBB_SDK_PATH
SCONS_ARGS += tbb_sdk_path=$(TBB_SDK_PATH)
endif

.PHONY: all

all:
	$(SCONS) $(SCONS_ARGS) target=template_debug -j$(JOBS)
	$(SCONS) $(SCONS_ARGS) target=template_release -j$(JOBS)
