## make file for librq-service.


ARGS=-g -Wall
OBJS=librq-service.o

DESTDIR=
INCDIR=$(DESTDIR)/usr/include
LIBDIR=$(DESTDIR)/usr/lib


all: librq-service.so.1.0.1


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
	@-test -e $(INCDIR)/rq-service.h && rm $(INCDIR)/rq-service.h
	cp rq-service.h $(INCDIR)/
	cp librq-service.so.1.0.1 $(LIBDIR)/
	@-test -e $(LIBDIR)/librq-service.so && rm $(LIBDIR)/librq-service.so
	ln -s $(LIBDIR)/librq-service.so.1.0.1 $(LIBDIR)/librq-service.so


uninstall: $(INCDIR)/rq-service.h $(LIBDIR)/librq-service.so.1.0.1
	rm $(INCDIR)/rq-service.h
	rm $(LIBDIR)/librq-service.so.1.0.1
	rm $(LIBDIR)/librq-service.so.1
	rm $(LIBDIR)/librq-service.so
	

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
