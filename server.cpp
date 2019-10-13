//
// Simple chat server for TSAM-409
//
// Command line: ./tsamvgroup43 <port>
//
// Teacher: Jacky Mallett (jacky@ru.is)
//
// Authors: Helga Hilmarsdóttir (helga13@ru.is) and Kara Líf Ingibergsdóttir (kara17@ru.is)

#include <stdio.h>
#include <errno.h>
#include <stdlib.h>
#include <unistd.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <netinet/ip.h>
#include <netinet/tcp.h>
#include <netdb.h>
#include <arpa/inet.h>
#include <string.h>
#include <algorithm>
#include <map>
#include <vector>
#include <ifaddrs.h>
#include <net/if.h>
#include <iostream>
#include <sstream>
#include <thread>
#include <map>

#include <unistd.h>

// fix SOCK_NONBLOCK for OSX
#ifndef SOCK_NONBLOCK
#include <fcntl.h>
#define SOCK_NONBLOCK O_NONBLOCK
#endif

#define BACKLOG  5          // Allowed length of queue of waiting connections

// Simple class for handling connections from clients.
//
// Client(int socket) - socket to send/receive traffic from client.
class Client
{
  public:
    int sock;              // socket of client connection
    std::string name;      // Limit length of name of client's user

    Client(int socket) : sock(socket){} 

    ~Client(){}            // Virtual destructor defined for base class
};

class Server
{
  public:
    int sock;              // socket of server connection
    std::string name;      // Limit length of name of server's user
    std::string ip;        // server's ip address
    int port;              // servers's port 

    Server(int socket, std::string nam, std::string ipAddress, int por)
    {
      //initialize the socket, name, ip address and the port
      sock = socket;
      nam = name;
      ipAddress = ip;
      por = port;

    } 

    ~Server(){}            // Virtual destructor defined for base class
};

// Note: map is not necessarily the most efficient method to use here,
// especially for a server with large numbers of simulataneous connections,
// where performance is also expected to be an issue.
//
// Quite often a simple array can be used as a lookup table, 
// (indexed on socket no.) sacrificing memory for speed.

std::map<int, Client*> clients; // Lookup table for per Client information
std::map<int, Server*> servers; // Lookup table for per Server information


// Open socket for specified port.
//
// Returns -1 if unable to create the socket for any reason.

int open_socket(int portno)
{
   struct sockaddr_in sk_addr;   // address settings for bind()
   int sock;                     // socket opened for this port
   int set = 1;                  // for setsockopt

   // Create socket for connection. Set to be non-blocking, so recv will
   // return immediately if there isn't anything waiting to be read.
#ifdef __APPLE__     
   if((sock = socket(AF_INET, SOCK_STREAM, 0)) < 0)
   {
      perror("Failed to open socket");
      return(-1);
   }
#else
   if((sock = socket(AF_INET, SOCK_STREAM | SOCK_NONBLOCK, 0)) < 0)
   {
     perror("Failed to open socket");
    return(-1);
   }
#endif

   // Turn on SO_REUSEADDR to allow socket to be quickly reused after 
   // program exit.

   if(setsockopt(sock, SOL_SOCKET, SO_REUSEADDR, &set, sizeof(set)) < 0)
   {
      perror("Failed to set SO_REUSEADDR:");
   }
   set = 1;
#ifdef __APPLE__     
   if(setsockopt(sock, SOL_SOCKET, SOCK_NONBLOCK, &set, sizeof(set)) < 0)
   {
     perror("Failed to set SOCK_NOBBLOCK");
   }
#endif
   memset(&sk_addr, 0, sizeof(sk_addr));

   sk_addr.sin_family      = AF_INET;
   sk_addr.sin_addr.s_addr = INADDR_ANY;
   sk_addr.sin_port        = htons(portno);

   // Bind to socket to listen for connections from clients

   if(bind(sock, (struct sockaddr *)&sk_addr, sizeof(sk_addr)) < 0)
   {
      perror("Failed to bind to socket:");
      return(-1);
   }
   else
   {
      return(sock);
   }
}

// Close a client's connection, remove it from the client list, and
// tidy up select sockets afterwards.

void closeClient(int clientSocket, fd_set *openSockets, int *maxfds)
{
     // Remove client from the clients list
     clients.erase(clientSocket);

     // If this client's socket is maxfds then the next lowest
     // one has to be determined. Socket fd's can be reused by the Kernel,
     // so there aren't any nice ways to do this.

     if(*maxfds == clientSocket)
     {
        for(auto const& p : clients)
        {
            *maxfds = std::max(*maxfds, p.second->sock);
        }
     }

     // And remove from the list of open sockets.

     FD_CLR(clientSocket, openSockets);
}

// Process command from client on the server

void clientCommand(int clientSocket, fd_set *openSockets, int *maxfds, 
                  char *buffer) 
{
  std::vector<std::string> tokens;
  std::string token;

  // Split command from client into tokens for parsing
  std::stringstream stream(buffer);

  while(stream >> token)
      tokens.push_back(token);

  if((tokens[0].compare("LISTSERVERS") == 0) && (tokens.size() == 2))
  {
     clients[clientSocket]->name = tokens[1];
  }
  else if(tokens[0].compare("LEAVE") == 0)
  {
      // Close the socket, and leave the socket handling
      // code to deal with tidying up clients etc. when
      // select() detects the OS has torn down the connection.
 
      closeClient(clientSocket, openSockets, maxfds);
  }
  else if(tokens[0].compare("WHO") == 0)
  {
     std::cout << "Who is logged on" << std::endl;
     std::string msg;

     for(auto const& names : clients)
     {
        msg += names.second->name + ",";

     }
     // Reducing the msg length by 1 loses the excess "," - which
     // granted is totally cheating.
     send(clientSocket, msg.c_str(), msg.length()-1, 0);

  }
  // This is slightly fragile, since it's relying on the order
  // of evaluation of the if statement.
  else if((tokens[0].compare("MSG") == 0) && (tokens[1].compare("ALL") == 0))
  {
      std::string msg;
      for(auto i = tokens.begin()+2;i != tokens.end();i++) 
      {
          msg += *i + " ";
      }

      for(auto const& pair : clients)
      {
          send(pair.second->sock, msg.c_str(), msg.length(),0);
      }
  }
  else if(tokens[0].compare("MSG") == 0)
  {
      for(auto const& pair : clients)
      {
          if(pair.second->name.compare(tokens[1]) == 0)
          {
              std::string msg;
              for(auto i = tokens.begin()+2;i != tokens.end();i++) 
              {
                  msg += *i + " ";
              }
              send(pair.second->sock, msg.c_str(), msg.length(),0);
          }
      }
  }
  else
  {
      std::cout << "Unknown command from client:" << buffer << std::endl;
  }
     
}


//code given by Jackie to get the IP address
std::string ip()
{
  struct ifaddrs *myaddrs, *ifa;
    void *in_addr;
    char buf[64];

    if(getifaddrs(&myaddrs) != 0)
    {
        perror("getifaddrs");
        exit(1);
    }

    for (ifa = myaddrs; ifa != NULL; ifa = ifa->ifa_next)
    {
        if (ifa->ifa_addr == NULL)
            continue;
        if (!(ifa->ifa_flags & IFF_UP))
            continue;

        switch (ifa->ifa_addr->sa_family)
        {
            case AF_INET:
            {
                struct sockaddr_in *s4 = (struct sockaddr_in *)ifa->ifa_addr;
                in_addr = &s4->sin_addr;
                break;
            }

            case AF_INET6:
            {
                struct sockaddr_in6 *s6 = (struct sockaddr_in6 *)ifa->ifa_addr;
                in_addr = &s6->sin6_addr;
                break;
            }

            default:
                continue;
        }

        if (!inet_ntop(ifa->ifa_addr->sa_family, in_addr, buf, sizeof(buf)))
        {
            printf("%s: inet_ntop failed!\n", ifa->ifa_name);
        }
        else  
        {
            printf("%s: %s\n", ifa->ifa_name, buf);
        }
    }


    freeifaddrs(myaddrs);
    return std::string(buf);
}

// Process command from another server to the server

void serverCommand(int serverSocket, fd_set *openSockets, int *maxfds, char *buffer, int port) 
{
  std::vector<std::string> tokens;
  std::string token;

  // Split command from server into tokens for parsing
  std::stringstream stream(buffer);

  while(stream >> token)
      tokens.push_back(token);

    //try to recieve LISTSERVERS command and send back servers information 
  if((tokens[0].compare("\x01LISTSERVERS") == 0) && (tokens.size() == 3))
  {
      //string with the information we want to send to the other server, who am i? the group name
      //ip address and port. 
     std::string msg = "SERVERS, tsamvgroup43, " + ip() + "," + std::to_string(port);

     for(auto&&server : servers)
     {
        msg += server.second->name + "," + server.second->ip + "," + std::to_string(server.second->port) + ";";
     }
     //send the message 
     send(serverSocket, msg.c_str(), msg.length()-1, 0);
   }

  else if(tokens[0].compare("KEEPALIVE") == 0)
  {
      //TODO
  }     
}

//code from given client.cpp
void listenServer(int serverSocket)
{
    int nread;                                  // Bytes read from socket
    char buffer[1025];                          // Buffer for reading input

    while(true)
    {
       memset(buffer, 0, sizeof(buffer));
       nread = read(serverSocket, buffer, sizeof(buffer));

       if(nread == 0)                      // Server has dropped us
       {
          printf("Over and Out\n");
          exit(0);
       }
       else if(nread > 0)
       {
          printf("%s\n", buffer);
       }
       printf("here\n");
    }
}

int main(int argc, char* argv[])
{
    bool finished;
    int listenSock;                 // Socket for connections to server
    int clientSock;                 // Socket of connecting client
    fd_set openSockets;             // Current open sockets 
    fd_set readSockets;             // Socket list for select()        
    fd_set exceptSockets;           // Exception socket list
    int maxfds;                     // Passed to select() as max fd in set
    struct sockaddr_in client;
    socklen_t clientLen;
    char buffer[1025];              // buffer for reading from clients
    bool ifClient = true;

    if(argc < 2)
    {
        printf("Usage: chat_server <ip port>\n");
        exit(0);
    }

    //if the arguments are three than this is server connecting (./server2 <ip-address> <port>)
    //code from given client.cpp
    if(argc == 3)
    {
         struct addrinfo hints, *svr;              // Network host entry for server
         struct sockaddr_in serv_addr;             // Socket address for server
         int serverSocket;                         // Socket used for server 
         int nwrite;                               // No. bytes written to server
         char buffer[1025];                        // buffer for writing to server
         bool finished;                   
         int set = 1;                              // Toggle for setsockopt
         ifClient = false;                         //to know if it is server og client that is connecting
         hints.ai_family   = AF_INET;               // IPv4 only addresses
         hints.ai_socktype = SOCK_STREAM;

         memset(&hints,   0, sizeof(hints));

        if(getaddrinfo(argv[1], argv[2], &hints, &svr) != 0)
        {
          perror("getaddrinfo failed: ");
          exit(0);
        }

        struct hostent *server;
        server = gethostbyname(argv[1]);

        bzero((char *) &serv_addr, sizeof(serv_addr));
        serv_addr.sin_family = AF_INET;
        bcopy((char *)server->h_addr,
        (char *)&serv_addr.sin_addr.s_addr,
        server->h_length);
        serv_addr.sin_port = htons(atoi(argv[2]));

        serverSocket = socket(AF_INET, SOCK_STREAM, 0);

        // Turn on SO_REUSEADDR to allow socket to be quickly reused after 
        // program exit.

        if(setsockopt(serverSocket, SOL_SOCKET, SO_REUSEADDR, &set, sizeof(set)) < 0)
        {
          printf("Failed to set SO_REUSEADDR for port %s\n", argv[2]);
          perror("setsockopt failed: ");
        }

   
        if(connect(serverSocket, (struct sockaddr *)&serv_addr, sizeof(serv_addr) )< 0)
        {
          printf("Failed to open socket to server: %s\n", argv[1]);
          perror("Connect failed: ");
          exit(0);
        }

        // Listen and print replies from server
        std::thread serverThread(listenServer, serverSocket);

        finished = false;
        while(!finished)
        {  
          bzero(buffer, sizeof(buffer));

          fgets(buffer, sizeof(buffer), stdin);

          nwrite = send(serverSocket, buffer, strlen(buffer),0);

          if(nwrite  == -1)
          {
            perror("send() to server failed: ");
            finished = true;
          }

      }

  }

    // Setup socket for server to listen to

    listenSock = open_socket(atoi(argv[1])); 
    printf("Listening on port: %d\n", atoi(argv[1]));

    if(listen(listenSock, BACKLOG) < 0)
    {
        printf("Listen failed on port %s\n", argv[1]);
        exit(0);
    }
    else 
    // Add listen socket to socket set we are monitoring
    {
        FD_ZERO(&openSockets);
        FD_SET(listenSock, &openSockets);
        maxfds = listenSock;
    }

    finished = false;

    while(!finished)
    {
        // Get modifiable copy of readSockets
        readSockets = exceptSockets = openSockets;
        memset(buffer, 0, sizeof(buffer));

        // Look at sockets and see which ones have something to be read()
        int n = select(maxfds + 1, &readSockets, NULL, &exceptSockets, NULL);

        if(n < 0)
        {
            perror("select failed - closing down\n");
            finished = true;
        }
        else
        {
            // First, accept  any new connections to the server on the listening socket
            if(FD_ISSET(listenSock, &readSockets))
            {
               clientSock = accept(listenSock, (struct sockaddr *)&client,
                                   &clientLen);
               printf("accept***\n");
               // Add new client to the list of open sockets
               FD_SET(clientSock, &openSockets);

               // And update the maximum file descriptor
               maxfds = std::max(maxfds, clientSock) ;

               // create a new client to store information.
               clients[clientSock] = new Client(clientSock);

               // Decrement the number of sockets waiting to be dealt with
               n--;

               printf("Client connected on server: %d\n", clientSock);
            }
            // Now check for commands from clients
            while(n-- > 0)
            {
              //trying to distinguish between client and server
               if(ifClient == true)
               {
                for(auto const& pair : clients)
                {
                  Client *client = pair.second;

                  if(FD_ISSET(client->sock, &readSockets))
                  {
                      // recv() == 0 means client has closed connection
                      if(recv(client->sock, buffer, sizeof(buffer), MSG_DONTWAIT) == 0)
                      {
                          printf("Client closed connection: %d", client->sock);
                          close(client->sock);      

                          closeClient(client->sock, &openSockets, &maxfds);

                      }
                      // We don't check for -1 (nothing received) because select()
                      // only triggers if there is something on the socket for us.
                      else
                      {
                          std::cout << buffer << std::endl;
                          clientCommand(client->sock, &openSockets, &maxfds, 
                                        buffer);
                      }
                  }
               }
             }else
             {
              for(auto const& pair : servers)
                {
                  Server *server = pair.second;

                  if(FD_ISSET(server->sock, &readSockets))
                  {
                      // recv() == 0 means client has closed connection
                      if(recv(server->sock, buffer, sizeof(buffer), MSG_DONTWAIT) == 0)
                      {
                          printf("Server closed connection: %d", server->sock);
                          close(server->sock);      

                      }
                      // We don't check for -1 (nothing received) because select()
                      // only triggers if there is something on the socket for us.
                      else
                      {
                          //std::cout << buffer << std::endl;
                          /*serverCommand(server->sock, &openSockets, &maxfds, 
                                        buffer);*/
                      }
                  }
               }
             }
          }
        }
    }
}
