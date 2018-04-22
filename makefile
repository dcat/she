LDFLAGS = -static -ltermbox
CFLAGS  = -g

all: she

# cause GNU make doesn't know how to make files
.c:
	${CC} -o $@ ${CFLAGS} $< ${LDFLAGS}
