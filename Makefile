all:exe
exe: TestThreadPoolExecutor.cc ThreadPoolExecutor.cc
	g++ -std=c++11 -Wall -g -O0 -o $@ $^ -pthread
clean:
	rm -rf *~ exe
