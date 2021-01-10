# Automatically generated build file. Do not edit.
COMPONENT_INCLUDES += $(PROJECT_PATH)/components/rf_test/include
COMPONENT_LDFLAGS += -L$(BUILD_DIR_BASE)/rf_test -lrf_test -L$(PROJECT_PATH)/components/rf_test/lib -lrftest
COMPONENT_LINKER_DEPS += $(PROJECT_PATH)/components/rf_test/lib/librftest.a
COMPONENT_SUBMODULES += 
COMPONENT_LIBRARIES += rf_test
COMPONENT_LDFRAGMENTS += 
component-rf_test-build: 
