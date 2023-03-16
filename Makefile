TARGETS = pylontech pylon2influx

#CC = gcc
CFLAGS = -g0 -Os -Wall -g 

# auto generate dependency files
CFLAGS += -MMD

.PHONY: default all clean info Debug cleanDebug

default: $(TARGETS)
all: default
Debug: all
cleanDebug: clean

ARCH         = $(shell uname -m && mkdir -p obj-`uname -m`/influxdb-post)

#LIBS = -lreadline
OBJDIR       = obj-$(ARCH)$(TGT)
SOURCES      = $(wildcard *.c *.cpp influxdb-post/*.c)
#OBJECTS     = $(patsubst %.c, %.o, $(SOURCES))
OBJECTS      = $(filter %.o, $(patsubst %.c, $(OBJDIR)/%.o, $(SOURCES)) $(patsubst %.cpp, $(OBJDIR)/%.o, $(SOURCES)))
MAINOBJS     = $(patsubst %, $(OBJDIR)/%.o,$(TARGETS))
LINKOBJECTS  = $(filter-out $(MAINOBJS), $(OBJECTS))
DEPS         = $(OBJECTS:.o=.d)

# include dependencies if they exist
-include $(DEPS)

$(OBJDIR)/%.o: %.c
	@echo -n "compiling $@ "
	@$(CC) $(CFLAGS) -c $< -o $@
	@echo ""

$(OBJDIR)/%.o: %.cpp
	@echo -n "compiling $< to $@ "
	@$(CXX) $(CXXFLAGS) $(CPPFLAGS) -c $< -o $@
	@echo ""

.PRECIOUS: $(TARGETS) $(ALLOBJECTS)

$(TARGETS): $(OBJECTS)
	@echo -n "linking $@"
	@$(CC) $(OBJDIR)/$@.o $(LINKOBJECTS) -Wall $(LIBS) -o $@
	@echo ""

build: clean all

clean:
	-rm -f $(OBJECTS)
	-rm -f $(TARGETS)
	-rm -f $(DEPS)

info:
	@echo "    TARGETS: $(TARGETS)"
	@echo "    SOURCES: $(SOURCES)"
	@echo "    OBJECTS: $(OBJECTS)"
	@echo "LINKOBJECTS: $(LINKOBJECTS)"
	@echo "   MAINOBJS: $(MAINOBJS)"
	@echo "       DEPS: $(DEPS)"
	@echo "         CC: $(CC)"
	@echo "     CFLAGS: $(CFLAGS)"
	@echo "       LIBS: $(LIBS)"
