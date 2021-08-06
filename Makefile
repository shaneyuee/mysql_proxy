SRC=$(shell ls *.cc *.h comm/*.c comm/*.h ht/*.cc ht/*.h)
OBJS=$(shell ls *.cc)


all:mysql_proxy_server mysql_proxy_test mysql_proxy_test_raw ctest libmysqlproxy.a

mysql_proxy_server:mysql_proxy_server.cpp $(SRC) knv/libknv.a comm/libcomm.a ht/libht.a
	g++ -g -o mysql_proxy_server mysql_proxy_server.cpp $(OBJS) knv/libknv.a comm/libcomm.a ht/libht.a -Icomm -Iknv -Iht -pthread /usr/local/mysql/lib/libmysqlclient.a -I /usr/local/mysql/include/ -lz

mysql_proxy_test: mysql_proxy_test.cpp mysql_proxy_api.cpp mysql_proxy_test_raw.cpp
	g++ -g -o mysql_proxy_test mysql_proxy_test.cpp mysql_proxy_api.cpp knv/libknv.a -Iknv comm/libcomm.a -Icomm

mysql_proxy_test_raw:mysql_proxy_test_raw.cpp
	g++ -g -o mysql_proxy_test_raw mysql_proxy_test_raw.cpp knv/libknv.a -Iknv comm/libcomm.a -Icomm

ctest:crypt_test.cpp
	g++ -g -o ctest crypt_test.cpp

knv/libknv.a:
	make -C knv

comm/libcomm.a:
	make -C comm

ht/libht.a:
	make -C ht

libmysqlproxy.a:mysql_proxy_api.cpp
	g++ -g -o mysql_proxy_api.o -c mysql_proxy_api.cpp -Iknv -Icomm
	ar -r libmysqlproxy.a mysql_proxy_api.o

clean:
	make clean -C knv
	make clean -C comm
	make clean -C ht
	rm -f *.o mysql_proxy_server mysql_proxy_test mysql_proxy_test_raw ctest libmysqlproxy.a
