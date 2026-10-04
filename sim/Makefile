# Host simulator build: make -C sim   (needs .pio/libdeps from a prior `pio pkg install`)
LV  := ../.pio/libdeps/cores3/lvgl
AJ  := ../.pio/libdeps/cores3/ArduinoJson/src
INC := -I../include -I$(LV) -I$(AJ) -DLV_CONF_INCLUDE_SIMPLE
CXXFLAGS := -std=gnu++17 -O1 -g0 -w -MMD -MP $(INC)
CFLAGS   := -std=gnu11 -O1 -g0 -w $(INC)
LVSRC := $(shell find $(LV)/src -name '*.c' | grep -v -E '/(arm2d|helium|neon|nxp|renesas|dma2d|opengles|vg_lite|stm32|sdl|ppa|pxp|g2d|eve|espressif|wayland)')
LVOBJ := $(patsubst $(LV)/%.c,build/lv/%.o,$(LVSRC))
APP   := $(wildcard ../src/core/*.cpp) $(wildcard ../src/ui/*.cpp) sim_main.cpp sim_plat.cpp
APPOBJ:= $(patsubst %.cpp,build/app/%.o,$(notdir $(APP)))
vpath %.cpp ../src/core ../src/ui .

sim: $(LVOBJ) $(APPOBJ)
	g++ -o $@ $^ -lpthread -lm

build/lv/%.o: $(LV)/%.c
	@mkdir -p $(dir $@)
	@gcc $(CFLAGS) -c $< -o $@

build/app/%.o: %.cpp
	@mkdir -p $(dir $@)
	g++ $(CXXFLAGS) -c $< -o $@

clean:
	rm -rf build sim

-include $(APPOBJ:.o=.d)
