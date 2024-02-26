# ----------------------------
# Makefile Options
# ----------------------------

NAME = ahex
DESCRIPTION = "Agon Hex Editor"
COMPRESSED = NO
INIT_LOC = 0B0000 #MOSLET
#INIT_LOC = 040000 #NON-MOSLET
LDHAS_EXIT_HANDLER:=0

CFLAGS = -Wall -Wextra -Oz
CXXFLAGS = -Wall -Wextra -Oz

# ----------------------------

include $(shell cedev-config --makefile)
