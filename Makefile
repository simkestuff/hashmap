CC		= gcc
CPPFLAGS	= -MMD -MP
CFLAGS		= -Wall -Wextra -ggdb -fsanitize=address,undefined
CFLAGSR		= -Wall -Wextra -O2 -fPIC

SRCDIR		= src
BUILDDIR	= build
RELEASEDIR      = $(BUILDDIR)/release
SRCS            = $(wildcard $(SRCDIR)/*.c)
OBJS            = $(patsubst $(SRCDIR)/%.c, $(BUILDDIR)/%.o, $(SRCS))
TARGET		= $(BUILDDIR)/test

LIBNAME         = skhashmap
LIBSRC          = $(SRCDIR)/hashmap.c
LIBOBJ          = $(patsubst $(SRCDIR)/%.c, $(RELEASEDIR)/%.o, $(LIBSRC))

DEPS		= $(OBJS:.o=.d) $(LIBOBJ:.o=.d)


all: $(TARGET)

obj: $(OBJS)

static: $(RELEASEDIR)/lib$(LIBNAME).a
shared: $(RELEASEDIR)/lib$(LIBNAME).so

$(TARGET): $(OBJS)
	$(CC) $(CFLAGS) $(OBJS) -o $@

# debug objects
$(BUILDDIR)/%.o: $(SRCDIR)/%.c | $(BUILDDIR)
	$(CC) $(CPPFLAGS) $(CFLAGS) -c $< -o $@

-include $(DEPS)

# release objects
$(RELEASEDIR)/%.o: $(SRCDIR)/%.c | $(RELEASEDIR)
	$(CC) $(CPPFLAGS) $(CFLAGSR) -c $^ -o $@ 

$(RELEASEDIR)/lib$(LIBNAME).so: $(LIBOBJ)
	$(CC) $(CFLAGSR) -shared $^ -o $@

$(RELEASEDIR)/lib$(LIBNAME).a: $(LIBOBJ)
	ar rcs $@ $^

$(BUILDDIR) $(RELEASEDIR):
	mkdir -p $@

.PHONY: clean all obj static shared

clean:
	rm -fr $(BUILDDIR)
