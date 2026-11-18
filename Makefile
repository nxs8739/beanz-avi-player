ifeq ($(strip $(DEVKITPPC)),)
$(error DEVKITPPC is not set)
endif
include $(DEVKITPPC)/gamecube_rules
TARGET := beanz_avi_player
BUILD := build
SOURCES := source
INCLUDES := source
LIBS := -ljpeg -lfat -logc -lm
LIBDIRS := $(DEVKITPRO)/portlibs/ppc $(DEVKITPRO)/libogc $(DEVKITPRO)/libogc/lib/cube
CFLAGS := -O2 -Wall -Wextra -std=gnu11 $(MACHDEP) $(INCLUDE)
LDFLAGS := $(MACHDEP) -Wl,-Map,$(notdir $@).map -L$(DEVKITPRO)/portlibs/ppc/lib -L$(DEVKITPRO)/libogc/lib/cube
ifneq ($(BUILD),$(notdir $(CURDIR)))
export OUTPUT := $(CURDIR)/$(TARGET)
export VPATH := $(foreach dir,$(SOURCES),$(CURDIR)/$(dir))
export DEPSDIR := $(CURDIR)/$(BUILD)
CFILES := $(foreach dir,$(SOURCES),$(notdir $(wildcard $(dir)/*.c)))
export OFILES := $(CFILES:.c=.o)
export INCLUDE := $(foreach dir,$(INCLUDES),-I$(CURDIR)/$(dir)) $(foreach dir,$(LIBDIRS),-I$(dir)/include)
export LIBPATHS := $(foreach dir,$(LIBDIRS),-L$(dir)/lib)
export LD := $(CC)
.PHONY: $(BUILD) clean
$(BUILD):
	@mkdir -p $(BUILD)
	@$(MAKE) --no-print-directory -C $(BUILD) -f $(CURDIR)/Makefile
clean:
	@rm -rf $(BUILD) $(OUTPUT).elf $(OUTPUT).dol $(OUTPUT).map
else
DEPENDS := $(OFILES:.o=.d)
$(OUTPUT).dol: $(OUTPUT).elf
$(OUTPUT).elf: $(OFILES)
-include $(DEPENDS)
endif
