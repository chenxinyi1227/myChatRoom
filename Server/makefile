src=*.c
server: ${src}
	gcc ${src} -o server -ljson-c -lmysqlclient -lmyAVLTree -lmyQueue -lmyLinkList

g:${src}
	gcc -g ${src} -o gtest -ljson-c -lmysqlclient

var:
	valgrind --leak-check=full ./server
#valgrind --tool=memcheck ./server

clean:
	@rm -rf server gtest