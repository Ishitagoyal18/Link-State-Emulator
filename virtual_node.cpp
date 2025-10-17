#include <iostream>
#include <sstream>
#include <cstring>
#include <sys/socket.h>
#include <unistd.h>
#include <arpa/inet.h>
#include <vector>
#include <set>
#include <tuple>
#include <bitset>
#include <string>
#include <unordered_map>
#include <limits>
#include <queue>
using namespace std;


struct LSA_pkt{
    int sender_id;
    int seq_number;
    vector<tuple<int,string, int, int>> Neighbors;
    int ttl;
};

struct Node{
    int id;
    string ip;
    int port;
    int udp_soc;
    int tcp_soc;
    vector<tuple<int,string, int, int>> Neighbors;
    vector<LSA_pkt> LSA_database;
    int seq_number;
    set<int> received_ids;
};

int NODE_COUNT;
string IP;
vector<Node> nodes;
int SERVER_PORT=5000;
string SERVER_IP;
int BUF_SIZE=1024;
int TIMEOUT = 2;
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

string encode_LSA(LSA_pkt lsa){
    string msg="";
    msg+=bitset<5>(lsa.sender_id).to_string();
    msg+=bitset<8>(lsa.seq_number).to_string();
    msg+=bitset<8>(lsa.ttl).to_string();
    for(auto &[index, ip, port, cost]: lsa.Neighbors){
        msg+=bitset<5>(index).to_string();
        msg+=ip_str(ip);
        msg+=bitset<13>(port).to_string();
        msg+=bitset<16>(cost).to_string();
    }
    return msg;
}

LSA_pkt decode_LSA(string msg){
    LSA_pkt lsa;
    lsa.sender_id = stoi(msg.substr(0, 5),nullptr,2);
    lsa.seq_number = stoi(msg.substr(5, 8),nullptr,2);
    lsa.ttl = stoi(msg.substr(13, 8),nullptr,2);
    for(int i=21;i<msg.size();i+=66){
        int index = stoi(msg.substr(i, 5),nullptr,2);
        string ip = str_ip(msg.substr(i+5, 32));
        int port = stoi(msg.substr(i+37, 13),nullptr,2);
        int cost = stoi(msg.substr(i+50, 16),nullptr,2);
        lsa.Neighbors.push_back(make_tuple(index,ip, port, cost));
    }
    return lsa;
}

bool node_has_complete_db(const Node &node, int total_nodes) {
    vector<bool> seen(total_nodes, false);
    int count = 0;
    for (const auto &lsa : node.LSA_database) {
        if (lsa.sender_id >= 0 && lsa.sender_id < total_nodes && !seen[lsa.sender_id]) {
            seen[lsa.sender_id] = true;
            ++count;
        }
    }
    return (count == total_nodes);
}

void dijkstra_and_print(int src, const Node &node, int total_nodes) {
    const int INF = numeric_limits<int>::max() / 4;
    vector<vector<pair<int,int>>> adj(total_nodes);
    for (const auto &lsa : node.LSA_database) {
        int u = lsa.sender_id;
        for (const auto &t : lsa.Neighbors) {
            int v; string vip; int vport; int cost;
            tie(v, vip, vport, cost) = t;
            if (u >= 0 && u < total_nodes && v >= 0 && v < total_nodes) {
                adj[u].push_back({v, cost});
                adj[v].push_back({u, cost});
            }
        }
    }

    // Dijkstra
    vector<int> dist(total_nodes, INF);
    vector<int> prev(total_nodes, -1);
    dist[src] = 0;
    priority_queue<pair<int,int>, vector<pair<int,int>>, greater<pair<int,int>>> pq;
    pq.push({0, src});
    while (!pq.empty()) {
        auto [d,u] = pq.top(); pq.pop();
        if (d != dist[u]) continue;
        for (auto &e : adj[u]) {
            int v = e.first, w = e.second;
            if (dist[v] > dist[u] + w) {
                dist[v] = dist[u] + w;
                prev[v] = u;
                pq.push({dist[v], v});
            }
        }
    }

    cout << "Routing table for node " << src << " (as seen by node " << node.id << "):\n";
    cout << "Dest\tNextHop\tCost\n";
    for (int dest = 0; dest < total_nodes; ++dest) {
        if (dest == src) {
            cout << dest << "\t-\t0\n";
            continue;
        }
        if (dist[dest] >= INF) {
            cout << dest << "\t-\tINF\n";
            continue;
        }
        int p = dest;
        int prev_node = prev[p];
        if (prev_node == -1) {
            cout << dest << "\t-\t" << dist[dest] << "\n";
            continue;
        }
        while (prev[p] != -1 && prev[p] != src) {
            p = prev[p];
        }
        int next_hop;
        if (prev[p] == src) next_hop = p;
        else if (prev[dest] == src) next_hop = dest;
        else next_hop = p;
        cout << dest << "\t" << next_hop << "\t" << dist[dest] << "\n";
    }
    cout << "-----------------------------------------\n";
}

void LSA(vector<Node>& nodes, int NODE_COUNT) {
    unordered_map<int,int> sock_to_idx;
    for (int i = 0; i < NODE_COUNT; ++i) {
        if (nodes[i].seq_number < 0 || nodes[i].seq_number > 1000000) nodes[i].seq_number = 0;
        sock_to_idx[nodes[i].udp_soc] = i;
    }

    for (int i = 0; i < NODE_COUNT; ++i) {
        LSA_pkt lsa;
        lsa.sender_id = nodes[i].id;
        nodes[i].seq_number = nodes[i].seq_number + 1;
        lsa.seq_number = nodes[i].seq_number;
        lsa.ttl = 8;
        lsa.Neighbors = nodes[i].Neighbors;

        bool replaced = false;
        for (auto &e : nodes[i].LSA_database) {
            if (e.sender_id == lsa.sender_id) {
                e = lsa;
                replaced = true;
                break;
            }
        }
        if (!replaced) nodes[i].LSA_database.push_back(lsa);

        string enc = encode_LSA(lsa);
        for (auto &t : nodes[i].Neighbors) {
            int nidx; string nip; int nport; int ncost;
            tie(nidx, nip, nport, ncost) = t;
            sockaddr_in dest{};
            dest.sin_family = AF_INET;
            dest.sin_port = htons(nport);
            if (inet_pton(AF_INET, nip.c_str(), &dest.sin_addr) <= 0) continue;
            sendto(nodes[i].udp_soc, enc.c_str(), enc.size(), 0,
                   (struct sockaddr*)&dest, sizeof(dest));
        }
    }

    int overall_attempts = 0;
    const int MAX_OVERALL_ATTEMPTS = 50;
    while (true) {
        int complete_count = 0;
        for (int i = 0; i < NODE_COUNT; ++i) {
            if (node_has_complete_db(nodes[i], NODE_COUNT)) complete_count++;
        }
        if (complete_count == NODE_COUNT) {
            cout << "All nodes have complete LSA DB. Stopping flooding.\n";
            break;
        }
        if (++overall_attempts > MAX_OVERALL_ATTEMPTS) {
            cout << "Reached max attempts (" << MAX_OVERALL_ATTEMPTS << "). Stopping flooding.\n";
            break;
        }

        fd_set readfds;
        FD_ZERO(&readfds);
        int maxfd = 0;
        for (int i = 0; i < NODE_COUNT; ++i) {
            FD_SET(nodes[i].udp_soc, &readfds);
            if (nodes[i].udp_soc > maxfd) maxfd = nodes[i].udp_soc;
        }
        timeval tv; tv.tv_sec = TIMEOUT; tv.tv_usec = 0;
        fd_set tempfds = readfds;
        int rv = select(maxfd + 1, &tempfds, nullptr, nullptr, &tv);
        if (rv <= 0) {
            for (int i = 0; i < NODE_COUNT; ++i) {
                if (!node_has_complete_db(nodes[i], NODE_COUNT)) {
                    if (!nodes[i].LSA_database.empty()) {
                        for (auto &lsa : nodes[i].LSA_database) {
                            if (lsa.sender_id == nodes[i].id) {
                                string enc = encode_LSA(lsa);
                                for (auto &t : nodes[i].Neighbors) {
                                    int nidx; string nip; int nport; int ncost;
                                    tie(nidx, nip, nport, ncost) = t;
                                    sockaddr_in dest{};
                                    dest.sin_family = AF_INET;
                                    dest.sin_port = htons(nport);
                                    if (inet_pton(AF_INET, nip.c_str(), &dest.sin_addr) <= 0) continue;
                                    sendto(nodes[i].udp_soc, enc.c_str(), enc.size(), 0,
                                           (struct sockaddr*)&dest, sizeof(dest));
                                }
                                break;
                            }
                        }
                    }
                }
            }
            continue;
        }
        for (int fd = 0; fd <= maxfd; ++fd) {
            if (!FD_ISSET(fd, &tempfds)) continue;
            if (sock_to_idx.find(fd) == sock_to_idx.end()) continue;
            int local_idx = sock_to_idx[fd];

            char buffer[1024];
            sockaddr_in src_addr{};
            socklen_t addrlen = sizeof(src_addr);
            ssize_t n = recvfrom(fd, buffer, 1024 - 1, 0, (struct sockaddr*)&src_addr, &addrlen);
            if (n <= 0) continue;
            buffer[n] = '\0';
            string msg(buffer);
            LSA_pkt rlsa = decode_LSA(msg);
            bool shouldStoreAndForward = false;
            bool foundExisting = false;
            for (auto &stored : nodes[local_idx].LSA_database) {
                if (stored.sender_id == rlsa.sender_id) {
                    foundExisting = true;
                    if (rlsa.seq_number > stored.seq_number) {
                        stored = rlsa;
                        shouldStoreAndForward = true;
                    }
                    break;
                }
            }
            if (!foundExisting) {
                nodes[local_idx].LSA_database.push_back(rlsa);
                shouldStoreAndForward = true;
            }

            if (!shouldStoreAndForward) continue;

            rlsa.ttl = rlsa.ttl - 1;
            if (rlsa.ttl <= 0) continue;
            char srcipbuf[INET_ADDRSTRLEN];
            inet_ntop(AF_INET, &(src_addr.sin_addr), srcipbuf, INET_ADDRSTRLEN);
            string src_ip = string(srcipbuf);
            int src_port = ntohs(src_addr.sin_port);

            string out_enc = encode_LSA(rlsa);
            for (auto &t : nodes[local_idx].Neighbors) {
                int nidx; string nip; int nport; int ncost;
                tie(nidx, nip, nport, ncost) = t;
                if (nip == src_ip && nport == src_port) continue; // don't send back
                sockaddr_in dest{};
                dest.sin_family = AF_INET;
                dest.sin_port = htons(nport);
                if (inet_pton(AF_INET, nip.c_str(), &dest.sin_addr) <= 0) continue;
                sendto(nodes[local_idx].udp_soc, out_enc.c_str(), out_enc.size(), 0,
                       (struct sockaddr*)&dest, sizeof(dest));
            }
        }
    }
    cout << "=== Phase 2: Dijkstra routing tables ===\n";
    for (int i = 0; i < NODE_COUNT; ++i) {
        dijkstra_and_print(i, nodes[i], NODE_COUNT);
    }
}

void LSA_old_old(vector<Node>& nodes,int NODE_COUNT) {
    cout<<"[Phase1] Starting Flooding LSA"<<endl;
    //send the encoded LSA packet to all neighbours of each node via UDP
    int done=0;
    while(!done){
        for(auto &node: nodes){
            node.seq_number++;
            LSA_pkt lsa;
            lsa.sender_id = node.id;
            lsa.seq_number = node.seq_number;
            lsa.Neighbors = node.Neighbors;
            lsa.ttl = 64;

            string msg = encode_LSA(lsa);
            for (auto &[nbr_id, nbr_ip, nbr_port, cost] : node.Neighbors) {
                sockaddr_in addr{};
                addr.sin_family = AF_INET;  
                addr.sin_port = htons(nbr_port);
                inet_pton(AF_INET, nbr_ip.c_str(), &addr.sin_addr);
                sendto(node.udp_soc, msg.c_str(), msg.size(), 0,(sockaddr *)&addr, sizeof(addr));
            }
            node.LSA_database.push_back(lsa);
            cout << "Node " << node.id << " flooded its LSP (seq=" << node.seq_number << ")" << endl;
        }

        //Listens to all UDP pakets
        fd_set readfds;
        timeval timeout{};
        timeout.tv_sec = 0.5; //timeout to wait for select
        FD_ZERO(&readfds);

        int max_fd = -1; //maximum UDP fd
        for (auto &n : nodes) {
            FD_SET(n.udp_soc, &readfds);
            if (n.udp_soc > max_fd)
                max_fd = n.udp_soc;
        }

        while(true){
            fd_set tmpfds=readfds;
            int ret=select(max_fd + 1, &tmpfds, NULL, NULL, &timeout);
            if(ret <= 0) break;
            for (auto &node : nodes) {
                if (!FD_ISSET(node.udp_soc, &tmpfds))
                    continue;

                char buffer[1024];
                sockaddr_in sender_addr{};
                socklen_t len = sizeof(sender_addr);
                int n = recvfrom(node.udp_soc, buffer, sizeof(buffer) - 1, 0,(sockaddr *)&sender_addr, &len);
                if (n <= 0)
                    continue;

                buffer[n] = '\0';
                string msg(buffer);
                LSA_pkt recv_lsa = decode_LSA(msg);

                // Reliable flooding logic
                bool newer = true;
                for (auto &l : node.LSA_database) {
                    if (l.sender_id == recv_lsa.sender_id) {
                        if (recv_lsa.seq_number <= l.seq_number)
                            newer = false;
                        break;
                    }
                }
                if (newer) {
                    node.LSA_database.push_back(recv_lsa);
                    node.received_ids.insert(recv_lsa.sender_id);
                    // forward to neighbors (except original sender)
                    for (auto &[nbr_id, nbr_ip, nbr_port, cost] : node.Neighbors) {
                        sockaddr_in addr{};
                        addr.sin_family = AF_INET;
                        addr.sin_port = htons(nbr_port);
                        inet_pton(AF_INET, nbr_ip.c_str(), &addr.sin_addr);
                        sendto(node.udp_soc, msg.c_str(), msg.size(), 0, (sockaddr *)&addr, sizeof(addr));
                    }

                    cout << "Node " << node.id << " accepted new LSP from Node "<< recv_lsa.sender_id << " (seq=" << recv_lsa.seq_number << ")" << endl;
                }
            }
        }
        for(int i=0;i<NODE_COUNT;i++){
            if((int)nodes[i].received_ids.size()<NODE_COUNT){
                done=0;
                break;
            }
            done=1;
        }
    }

    for(auto & node:nodes){
        node.received_ids.clear();
    }

    cout<<"[Phase 2] Running Djisktra's Algorithm"<<endl;
    const int INF = 1e9;
    for(auto &node:nodes){
        vector<vector<int>> cost(NODE_COUNT, vector<int>(NODE_COUNT, INF));
        for (int i = 0; i < NODE_COUNT; i++)
            cost[i][i] = 0;

        for (auto &lsa : node.LSA_database) {
            for (auto &[nbr, ip, port, c] : lsa.Neighbors) {
                cost[lsa.sender_id][nbr] = c;
                cost[nbr][lsa.sender_id] = c;
            }
        }

        vector<int> dist(NODE_COUNT, INF);
        vector<int> visited(NODE_COUNT, 0);
        dist[node.id] = 0;

        for (int cnt = 0; cnt < NODE_COUNT; cnt++) {
            int u = -1;
            for (int i = 0; i < NODE_COUNT; i++)
                if (!visited[i] && (u == -1 || dist[i] < dist[u]))
                    u = i;
            if (u == -1 || dist[u] == INF)
                break;
            visited[u] = 1;

            for (int v = 0; v < NODE_COUNT; v++) {
                if (cost[u][v] < INF && dist[v] > dist[u] + cost[u][v])
                    dist[v] = dist[u] + cost[u][v];
            }
        }

        cout << "\nRouting Table for Node " << node.id << ":\n";
        for (int j = 0; j < NODE_COUNT; j++) {
            if (node.id == j) continue;
            if (dist[j] == INF)
                cout << "  No path to Node " << j << "\n";
            else
                cout << "  Cost to Node " << j << " = " << dist[j] << endl;
        }
    }
}

void LSA_old(vector<Node>& nodes, int NODE_COUNT) {
    cout << "[Phase 1] Starting Reliable Flooding\n";

    // Each node floods its own LSA
    for (auto &node : nodes) {
        node.seq_number++;
        LSA_pkt lsa;
        lsa.sender_id = node.id;
        lsa.seq_number = node.seq_number;
        lsa.Neighbors = node.Neighbors;
        lsa.ttl = 64;

        string msg = encode_LSA(lsa);
        for (auto &[nbr_id, nbr_ip, nbr_port, cost] : node.Neighbors) {
            sockaddr_in addr{};
            addr.sin_family = AF_INET;
            addr.sin_port = htons(nbr_port);
            inet_pton(AF_INET, nbr_ip.c_str(), &addr.sin_addr);
            sendto(node.udp_soc, msg.c_str(), msg.size(), 0, (sockaddr *)&addr, sizeof(addr));
        }

        // Store its own LSA in its database
        node.LSA_database.push_back(lsa);
        cout << "Node " << node.id << " flooded its LSA (seq=" << node.seq_number << ")\n";
    }

    // Each node keeps track of which LSAs it has received
    vector<set<int>> received_ids(NODE_COUNT);
    for (int i = 0; i < NODE_COUNT; i++)
        received_ids[i].insert(nodes[i].id); // each knows itself

    fd_set readfds;
    int max_fd = -1;
    FD_ZERO(&readfds);
    for (auto &n : nodes) {
        FD_SET(n.udp_soc, &readfds);
        if (n.udp_soc > max_fd)
            max_fd = n.udp_soc;
    }

    timeval timeout{};
    timeout.tv_sec = 2; 
    timeout.tv_usec = 0;

    bool converged = false;
    while (!converged) {
        fd_set tmpfds = readfds;
        int ret = select(max_fd + 1, &tmpfds, NULL, NULL, &timeout);
        if (ret <= 0) {
            converged = true;
            for (int i = 0; i < NODE_COUNT; i++) {
                if ((int)received_ids[i].size() < NODE_COUNT) {
                    converged = false;
                    break;
                }
            }
            if (!converged) {
                timeout.tv_sec = 2;
                continue;
            } else break;
        }

        for (auto &node : nodes) {
            if (!FD_ISSET(node.udp_soc, &tmpfds))
                continue;

            char buffer[1024];
            sockaddr_in sender_addr{};
            socklen_t len = sizeof(sender_addr);
            int n = recvfrom(node.udp_soc, buffer, sizeof(buffer) - 1, 0,
                             (sockaddr *)&sender_addr, &len);
            if (n <= 0)
                continue;

            buffer[n] = '\0';
            string msg(buffer);
            LSA_pkt recv_lsa = decode_LSA(msg);

            // Check if LSA is new
            bool newer = true;
            for (auto &l : node.LSA_database) {
                if (l.sender_id == recv_lsa.sender_id) {
                    if (recv_lsa.seq_number <= l.seq_number)
                        newer = false;
                    break;
                }
            }

            if (newer) {
                node.LSA_database.push_back(recv_lsa);
                received_ids[node.id].insert(recv_lsa.sender_id);

                // Forward to all neighbors except origin
                for (auto &[nbr_id, nbr_ip, nbr_port, cost] : node.Neighbors) {
                    sockaddr_in addr{};
                    addr.sin_family = AF_INET;
                    addr.sin_port = htons(nbr_port);
                    inet_pton(AF_INET, nbr_ip.c_str(), &addr.sin_addr);
                    sendto(node.udp_soc, msg.c_str(), msg.size(), 0,
                           (sockaddr *)&addr, sizeof(addr));
                }

                cout << "Node " << node.id << " accepted new LSA from Node "
                     << recv_lsa.sender_id << " (seq=" << recv_lsa.seq_number << ")\n";
            }
        }
        converged = true;
        for (int i = 0; i < NODE_COUNT; i++) {
            if ((int)received_ids[i].size() < NODE_COUNT) {
                converged = false;
                break;
            }
        }
    }

    cout << "\n[Flooding Complete] All nodes have full topology information.\n";

    cout << "[Phase 2] Running Dijkstra...\n";
    const int INF = 1e9;

    for (auto &node : nodes) {
        vector<vector<int>> cost(NODE_COUNT, vector<int>(NODE_COUNT, INF));
        for (int i = 0; i < NODE_COUNT; i++)
            cost[i][i] = 0;

        for (auto &lsa : node.LSA_database) {
            for (auto &[nbr, ip, port, c] : lsa.Neighbors) {
                cost[lsa.sender_id][nbr] = c;
                cost[nbr][lsa.sender_id] = c;
            }
        }

        vector<int> dist(NODE_COUNT, INF);
        vector<int> visited(NODE_COUNT, 0);
        dist[node.id] = 0;

        for (int cnt = 0; cnt < NODE_COUNT; cnt++) {
            int u = -1;
            for (int i = 0; i < NODE_COUNT; i++)
                if (!visited[i] && (u == -1 || dist[i] < dist[u]))
                    u = i;
            if (u == -1 || dist[u] == INF)
                break;
            visited[u] = 1;

            for (int v = 0; v < NODE_COUNT; v++) {
                if (cost[u][v] < INF && dist[v] > dist[u] + cost[u][v])
                    dist[v] = dist[u] + cost[u][v];
            }
        }

        cout << "\nRouting Table for Node " << node.id << ":\n";
        for (int j = 0; j < NODE_COUNT; j++) {
            if (node.id == j) continue;
            if (dist[j] == INF)
                cout << "  No path to Node " << j << "\n";
            else
                cout << "  Cost to Node " << j << " = " << dist[j] << "\n";
        }
    }
}


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
        // int opt = 1;
        // if (setsockopt(sock, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt)) < 0) {
        //     perror("setsockopt failed");
        // }
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

    LSA(nodes,NODE_COUNT);
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
        LSA(nodes,NODE_COUNT);
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