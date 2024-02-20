src=*.c
client: ${src}
	gcc ${src} -o a.out -ljson-c 

g:${src}
	gcc -g ${src} -o gtest -ljson-c 

clean:
	@rm -rf	a.out gtest

var:
	valgrind --leak-check=full ./client