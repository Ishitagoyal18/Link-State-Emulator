#include<iostream>
#include <cstring>
#include <sys/socket.h>
#include <unistd.h>
#include <arpa/inet.h>
using namespace std;

int main(){
    const char *IP= "172.18.145.245";
    const int PORT = 5000;
    int sock = socket(AF_INET, SOCK_STREAM, 0);
    if (sock<0){
        perror("socket error");
        return 0;
    }
    sockaddr_in serveraddr{};
    serveraddr.sin_family=AF_INET;
    serveraddr.sin_port=htons(PORT);

    inet_pton(AF_INET, IP, &serveraddr.sin_addr);

    connect(sock, (struct sockaddr*)&serveraddr, sizeof(serveraddr));

    const char* msg = "hello from virtual node";
    send(sock, msg, strlen(msg), 0);
}