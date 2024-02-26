src=*.c
server: ${src}
	gcc ${src} -o server -ljson-c -lmysqlclient -lmyAVLTree -lmyQueue -lmyLinkList

g:${src}
	gcc -g ${src} -o gtest -ljson-c -lmysqlclient -lmyAVLTree -lmyQueue -lmyLinkList

var:
	valgrind --leak-check=full ./server
# valgrind --tool=massif ./server

#valgrind --tool=memcheck ./server
#ms_print massif.out.<PID>

clean:
	@rm -rf server gtest