CC = g++
FLAGS= -std=c++11


#targets
all: 
	$(CC) $(FLAGS) server.cpp -o tsamvgroup43
	$(CC) $(FLAGS) client.cpp -o client


clean:server client
	$(RM) tsamvgroup43
	$(RM) client
