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
    int udp_soc;
    int tcp_soc;
    vector<tuple<int,string, int, int>> Neighbors;
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
vector<tuple<int,string, int,int>> decode_msg(string msg){
    vector<tuple<int,string,int,int>> res;
    for(int i=0;i<msg.size();i+=66){
        int index = stoi(msg.substr(i, 5),nullptr,2);
        string ip = str_ip(msg.substr(i+5, 32));
        int port = stoi(msg.substr(i+37, 13),nullptr,2);
        int cost = stoi(msg.substr(i+50, 16),nullptr,2);
        res.push_back(make_tuple(index,ip, port, cost));
    }
    return res;
}

string encode_msg(string ip, int port){
    string msg="";
    msg+=ip_str(ip);
    msg+=bitset<13>(port).to_string();
    return msg;
}

void LSA(){
    cout<<"Link State Algo will be implemented here"<<endl;
}

int NODE_COUNT;
string IP;
vector<Node> nodes;
int SERVER_PORT=5000;
string SERVER_IP;
int BUF_SIZE=1024;
int TIMEOUT = 1;

int main(){
    cout<<"Enter the number of virtual nodes you want to create: ";
    cin>>NODE_COUNT;
    cout<<"Enter the IP address of ON: ";
    cin>>SERVER_IP;
    cout<<"Enter the IP address of VN: ";
    cin>>IP;

    nodes.resize(NODE_COUNT);

    for(int i=0;i<NODE_COUNT;i++){
        nodes[i].id=i;
        nodes[i].ip=IP;
        nodes[i].port=5000+i;
    }

    // Here I will bind the Ip, port to UDP socket

    for(int i=0;i<NODE_COUNT;i++){
        int sock = socket(AF_INET, SOCK_DGRAM, 0);
        if (sock < 0) {
            perror("socket creation failed");
            continue;
        }

        sockaddr_in addr{};
        addr.sin_family = AF_INET;
        addr.sin_port = htons(nodes[i].port);
        if (inet_pton(AF_INET, nodes[i].ip.c_str(), &addr.sin_addr) <= 0) {
            perror("Invalid IP address");
            close(sock);
            continue;
        }
        if (bind(sock, (struct sockaddr*)&addr, sizeof(addr)) < 0) {
            perror("bind failed");
            close(sock);
            continue;
        }
        nodes[i].udp_soc = sock;
    }

    // Here I will be sending TCP msg to the server

    for (int i = 0; i < NODE_COUNT; i++) {
        int sock = socket(AF_INET, SOCK_STREAM, 0);
        if (sock < 0) {
            perror("TCP socket creation failed");
            continue;
        }

        sockaddr_in server_addr{};
        server_addr.sin_family = AF_INET;
        server_addr.sin_port = htons(5000);

        if (inet_pton(AF_INET, SERVER_IP.c_str(), &server_addr.sin_addr) <= 0) {
            perror("Invalid server IP address");
            close(sock);
            continue;
        }

        if (connect(sock, (struct sockaddr*)&server_addr, sizeof(server_addr)) < 0) {
            perror("Connection to server failed");
            close(sock);
            continue;
        }
        string encoded_msg = encode_msg(nodes[i].ip, nodes[i].port);

        if (send(sock, encoded_msg.c_str(), encoded_msg.size(), 0) < 0) {
            perror("Send failed");
        } else {
            cout << "Virtual Node " << i << " sent message to server" << endl;
        }

        nodes[i].tcp_soc = sock;
    }
    cout<<"All Virtual nodes sent their info to Oracle Node"<<endl;
    
    // Here I will receive msg from server and update my neighbour list
    for (int i = 0; i < NODE_COUNT; i++) {
        int sock = nodes[i].tcp_soc; 

        char buffer[BUF_SIZE];
        int n = recv(sock, buffer, BUF_SIZE - 1, 0);  // blocking read
        if (n < 0) {
            perror("recv failed");
            continue;
        } else if (n == 0) {
            cout << "Connection closed by server for node " << i << endl;
            continue;
        }

        buffer[n] = '\0';
        string msg(buffer);
        cout << "Virtual Node " << i << " received msg" << endl;
        auto neighbors = decode_msg(msg);
        nodes[i].Neighbors = neighbors;
    } 
    for(int i=0;i<NODE_COUNT;i++){
        cout<<"Node "<<i<<" has neighbors: ";
        for(auto &[index, ip, port, cost]: nodes[i].Neighbors){
            cout<<"(Index: "<<index<<", IP: "<<ip<<", Port: "<<port<<", Cost: "<<cost<<") ";
        }
        cout<<endl;
    }
    LSA();
    // Here I will listen again from TCP after timeout for changes
    while(true){
        for (int i = 0; i < NODE_COUNT; i++) {
            int sock = nodes[i].tcp_soc; 

            char buffer[BUF_SIZE];
            int n = recv(sock, buffer, BUF_SIZE - 1, 0);  // blocking read
            if (n < 0) {
                perror("recv failed");
                continue;
            } else if (n == 0) {
                cout << "Connection closed by server for node " << i << endl;
                continue;
            }

            buffer[n] = '\0';
            string msg(buffer);
            cout << "Virtual Node " << i << " received msg" << endl;
            auto neighbors = decode_msg(msg);
            nodes[i].Neighbors = neighbors;
        } 
        cout<<"Updated";
        LSA();
    }



    // int sock = socket(AF_INET, SOCK_STREAM, 0);
    // if (sock<0){
    //     perror("socket error");
    //     return 0;
    // }
    // sockaddr_in serveraddr{};
    // serveraddr.sin_family=AF_INET;
    // serveraddr.sin_port=htons(PORT);

    // inet_pton(AF_INET, IP, &serveraddr.sin_addr);

    // connect(sock, (struct sockaddr*)&serveraddr, sizeof(serveraddr));

    // const char* msg = "hello from virtual node";
    // send(sock, msg, strlen(msg), 0);
    // string ip="127.3.5.0";
    // cout<<ip_str(ip)<<endl;
    // cout<<str_ip(ip_str(ip))<<endl;
    // string encoded_msg=encode_msg(ip, 8080);
    // cout<<encoded_msg<<endl;
}