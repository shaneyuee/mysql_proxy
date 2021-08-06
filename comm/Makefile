OBJS=$(shell ls *.c | sed 's/\.c\>/.o/')

all : $(OBJS)
	ar r libcomm.a $(OBJS)

%.o:%.c
	gcc -g -o $@ -c $<

clean:
	rm -f *.o libcomm.a
