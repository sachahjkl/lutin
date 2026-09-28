NAME := ai-dsi
GAME_TITLE := Lutin
GAME_SUBTITLE := AI coding agent
GAME_AUTHOR := Lutin contributors
DEFINES := -DCJSON_NESTING_LIMIT=64

SOURCEDIRS := source $(LUA_SOURCE)/src $(JSON_SOURCE)
INCLUDEDIRS := $(LUA_SOURCE)/src $(JSON_SOURCE)
ARM7ELF := $(BLOCKSDS)/sys/arm7/main_core/arm7_dswifi_maxmod.elf
LIBS := -lcurl -lmbedtls -lmbedx509 -lmbedcrypto -lz -ldswifi9 -lnds9 -lm
LIBDIRS := $(NETWORK_LIBRARIES)/thirdparty/blocksds/external/libcurl $(NETWORK_LIBRARIES)/thirdparty/blocksds/external/mbedtls $(NETWORK_LIBRARIES)/toolchain/gcc-arm-none-eabi/arm-none-eabi $(BLOCKSDS)/libs/dswifi

include $(BLOCKSDS)/sys/default_makefiles/rom_arm9/Makefile
