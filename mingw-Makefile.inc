# how to call the mingw tools
MINGW_EXEC_PREFIX ?=

# set the environment for make
OS = Win32

#CPPFLAGS_XML =
#LIBS_XML = -lxml2

CC = $(MINGW_EXEC_PREFIX)gcc
CXX = $(MINGW_EXEC_PREFIX)g++
LDD =
RANLIB = $(MINGW_EXEC_PREFIX)ranlib
AR = $(MINGW_EXEC_PREFIX)ar
WINDRES = $(MINGW_EXEC_PREFIX)windres

# LuaJIT first builds a host tool (buildvm) that generates the VM for
# the target, so it needs the native compiler and the target OS
LUAJIT_MAKEFLAGS ?= HOST_CC=gcc TARGET_SYS=Windows
