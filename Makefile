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

LIBS = -lcurl
OBJDIR            = obj-$(ARCH)$(TGT)
SOURCES           = $(wildcard *.c *.cpp)
SOURCESINFLUX     = $(wildcard *.c *.cpp influxdb-post/*.c)
OBJECTS           = $(filter %.o, $(patsubst %.c, $(OBJDIR)/%.o, $(SOURCES)) $(patsubst %.cpp, $(OBJDIR)/%.o, $(SOURCES)))
OBJECTSINFLUX     = $(filter %.o, $(patsubst %.c, $(OBJDIR)/%.o, $(SOURCESINFLUX)) $(patsubst %.cpp, $(OBJDIR)/%.o, $(SOURCES)))
MAINOBJS          = $(patsubst %, $(OBJDIR)/%.o,$(TARGETS))
LINKOBJECTS       = $(filter-out $(MAINOBJS), $(OBJECTS))
LINKOBJECTSINFLUX = $(filter-out $(MAINOBJS), $(OBJECTSINFLUX))
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


pylontech: $(OBJECTS)
	@echo -n "linking $@"
	@$(CC) $(OBJDIR)/$@.o $(LINKOBJECTS) -Wall -o $@
	@echo ""


pylon2influx: $(OBJECTSINFLUX)
	@echo -n "linking $@"
	@$(CC) $(OBJDIR)/$@.o $(LINKOBJECTSINFLUX) -Wall $(LIBS) -o $@
	@echo ""

build: clean all

clean:
	-rm -f $(OBJECTS)
	-rm -f $(TARGETS)
	-rm -f $(DEPS)

info:
	@echo "          TARGETS: $(TARGETS)"
	@echo "          SOURCES: $(SOURCES)"
	@echo "    SOURCESINFLUX: $(SOURCESINFLUX)"
	@echo "          OBJECTS: $(OBJECTS)"
	@echo "    OBJECTSINFLUX: $(OBJECTSINFLUX)"
	@echo "      LINKOBJECTS: $(LINKOBJECTS)"
	@echo "LINKOBJECTSINFLUX: $(LINKOBJECTSINFLUX)"
	@echo "         MAINOBJS: $(MAINOBJS)"
	@echo "             DEPS: $(DEPS)"
	@echo "               CC: $(CC)"
	@echo "           CFLAGS: $(CFLAGS)"
	@echo "             LIBS: $(LIBS)"
