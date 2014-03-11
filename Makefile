PKGS = "libdrm egl gbm gl"
CFLAGS = $(shell pkg-config --cflags $(PKGS)) -Wall -g
LDFLAGS = $(shell pkg-config --libs-only-L $(PKGS))
LDLIBS = $(shell pkg-config --libs-only-l $(PKGS))

all : eglkms

eglkms: eglkms.o

clean :
	rm *.o eglkms

