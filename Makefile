## make file for librq-service.

all: librq-service.so.1.0.1

ARGS=-g -Wall
OBJS=librq-service.o

librq-service.o: librq-service.c rq-service.h 
	gcc -c -fPIC librq-service.c  -o $@ $(ARGS)


librq-service.a: $(OBJS)
	@>$@
	@rm $@
	ar -r $@
	ar -r $@ $^

librq-service.so.1.0.1: $(OBJS)
	gcc -shared -Wl,-soname,librq-service.so.1 -o librq-service.so.1.0.1 $(OBJS)
	

install: librq-service.so.1.0.1 rq-service.h
	@-test -e /usr/include/rq-service.h && rm /usr/include/rq-service.h
	cp rq-service.h /usr/include/
	cp librq-service.so.1.0.1 /usr/lib/
	@-test -e /usr/lib/librq-service.so && rm /usr/lib/librq-service.so
	ln -s /usr/lib/librq-service.so.1.0.1 /usr/lib/librq-service.so
	ldconfig
	@echo "Install complete."


uninstall: /usr/include/rq-service.h /usr/lib/librq-service.so.1.0.1
	rm /usr/include/rq-service.h
	rm /usr/lib/librq-service.so.1.0.1
	rm /usr/lib/librq-service.so.1
	rm /usr/lib/librq-service.so
	

man-pages: manpages/librq-service.3
	@mkdir tmp.install
	@cp manpages/* tmp.install/
	@gzip tmp.install/*.3
	cp tmp.install/*.3.gz $(MANPATH)/man3/
	@rm -r tmp.install	
	@echo "Man-pages Install complete."


clean:
	@-[ -e librq-service.o ] && rm librq-service.o
	@-[ -e librq-service.so* ] && rm librq-service.so*
