all:exe
exe: ../ThreadPoolExecutor/TestThreadPoolExecutor.cc ../ThreadPoolExecutor/ThreadPoolExecutor.cc main.cc
	g++ -std=c++11 -Wall -g -O2 -o $@ $^ -pthread
clean:
	rm -rf *~ exe
