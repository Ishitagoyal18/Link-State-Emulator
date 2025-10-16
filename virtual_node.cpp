#include<iostream>
#include <sstream>
#include <cstring>
#include <sys/socket.h>
#include <unistd.h>
#include <arpa/inet.h>
#include <vector>
#include <tuple>
#include <bitset>
#include <string>
using namespace std;

//Just timepass code here
struct Node{
    int id;
    string ip;
    int port;
    vector<tuple<string, int, int, int>> Neighbors;
};

string str_ip(string bin){
    string ip;
    for (int i = 0; i < 32; i += 8) {
        string byte = bin.substr(i, 8);
        int Val = bitset<8>(byte).to_ulong();  // convert 8 bits → int
        ip += to_string(Val);
        if (i < 24) ip += ".";
    }
    return ip;
}

string ip_str(string ip){
    stringstream ss(ip);
    string part, bin = "";
    while (getline(ss, part, '.')) {
        int num = stoi(part);
        bin += bitset<8>(num).to_string();
    }
    return bin;
}
//function that takes a msg and return a tuple (IP, port,index,cost) of neighbour node
vector<tuple<string, int,int,int>> decode_msg(string msg){
    vector<tuple<string, int,int,int>> res;
    for(int i=0;i<msg.size();i+=66){
        int index = stoi(msg.substr(i, 5));
        string ip = str_ip(msg.substr(i+6, 32));
        int port = stoi(msg.substr(i+38, 13));
        int cost = stoi(msg.substr(i+51, 13));
        res.push_back(make_tuple(ip, port, index, cost));
    }
    return res;
}

string encode_msg(string ip, int port){
    string msg="";
    msg+=ip_str(ip);
    msg+=bitset<13>(port).to_string();
    return msg;
}

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
    // string ip="127.3.5.0";
    // cout<<ip_str(ip)<<endl;
    // cout<<str_ip(ip_str(ip))<<endl;
    // string encoded_msg=encode_msg(ip, 8080);
    // cout<<encoded_msg<<endl;
}