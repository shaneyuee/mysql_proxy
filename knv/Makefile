
# default Attr_API
HAS_ATTR_API=1
ATTR_API_INC=-I ../comm/
ATTR_API_LIB=../comm/libcomm.a
ATTR_API_PIC_LIB=$(ATTR_API_LIB)

OPTFLAG=-O3 -finline-functions -falign-loops
CFLAGS=$(OPTFLAG) -Wall -pthread -fno-strict-aliasing -g
DEPS=

ifeq ($(HAS_ATTR_API),1)
	CFLAGS += $(ATTR_API_INC) -DHAS_ATTR_API
	EXTLIBS += $(ATTR_API_LIB)
	EXTLIBS_PIC += $(ATTR_API_PIC_LIB)
else
	DEPS += Attr_API.h
endif

SRC=$(shell ls *.c *.cc *.h)
OBJS=$(shell ls *.c *.cc | sed -e 's/\.cc\>/.o/' -e 's/\.c\>/.o/' | sed s%'.*/'%%g)
OBJS_PIC=$(patsubst %.o, %.pic.o, $(OBJS))

all:libknv.a

lib:libknv.a libknv_pic.a libknv.so

Attr_API.h:
	@echo "#define Attr_API(a,b)" > Attr_API.h

lib_no_attr_api:
	@make lib HAS_ATTR_API=0

libknv.a:$(DEPS) $(OBJS) $(SRC)
	rm -f libknv.a
	ar q libknv.a $(OBJS)

libknv_pic.a:$(DEPS) $(OBJS_PIC) $(SRC)
	rm -f libknv_pic.a
	ar q libknv_pic.a $(OBJS_PIC)

libknv.so:$(DEPS) $(OBJS_PIC) $(SRC) $(EXTLIBS_PIC)
	g++ -shared -o libknv.so $(OBJS_PIC) $(EXTLIBS_PIC)

%.pic.o:%.cc $(DEPS) $(wildcard *.h)
	g++ -fPIC -shared $(CFLAGS) -DPIC -o $@ -c $<

%.pic.o:%.c $(DEPS) $(wildcard *.h)
	gcc -fPIC -shared $(CFLAGS) -DPIC -o $@ -c $<

%.o:%.cc $(DEPS) $(wildcard *.h)
	g++ $(CFLAGS) -c $<

%.o:%.c $(DEPS) $(wildcard *.h)
	gcc $(CFLAGS) -c $<

test:Attr_API.h knvtest knvshow mempool_test

knvtest: $(DEPS) knv_node_test.cpp libknv.a  $(EXTLIBS)
	g++ $(CFLAGS) -o knvtest $^

knvshow: $(DEPS) knv_show.cpp libknv.a  $(EXTLIBS)
	g++ $(CFLAGS) -o knvshow $^

mempool_test: $(DEPS) mempool_test.cpp libknv.a  $(EXTLIBS)
	g++ $(CFLAGS) -o mempool_test $^

clean:
	rm -f *.o *.so *.a knvtest knvshow mempool_test Attr_API.h
