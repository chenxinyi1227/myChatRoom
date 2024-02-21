src=*.c
client: ${src}
	gcc ${src} -o client -ljson-c 

g:${src}
	gcc -g ${src} -o gtest -ljson-c 

clean:
	@rm -rf	client gtest

var:
	valgrind --leak-check=full ./client