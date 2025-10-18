// vn_node.cpp
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
#include <sys/select.h>
#include <netinet/in.h>
#include <chrono>

using namespace std;

struct LSA_pkt{
    int sender_id = -1;
    int seq_number = 0;
    vector<tuple<int,string, int, int>> Neighbors;
    int ttl = 0;
};

struct Node{
    int id = -1;
    string ip;
    int port = 0;
    int udp_soc = -1;
    int tcp_soc = -1;
    vector<tuple<int,string, int, int>> Neighbors;
    vector<LSA_pkt> LSA_database;
    int seq_number = 0;
    vector<vector<int>> routing_table; // [dest]{ nextHop, cost }
};

int BUF_SIZE = 8192;
int TIMEOUT = 1; // select timeout seconds
int PERIOD = 10; // seconds between periodic routing table prints

string str_ip(const string &bin){
    string ip;
    for (int i = 0; i+8 <= (int)bin.size() && i < 32; i += 8) {
        string byte = bin.substr(i, 8);
        int Val = bitset<8>(byte).to_ulong();
        ip += to_string(Val);
        if (i < 24) ip += ".";
    }
    return ip;
}

string ip_str(const string &ip){
    stringstream ss(ip);
    string part, bin = "";
    while (getline(ss, part, '.')) {
        int num = stoi(part);
        bin += bitset<8>(num).to_string();
    }
    return bin;
}

vector<tuple<int,string, int,int>> decode_msg(const string &msg){
    vector<tuple<int,string,int,int>> res;
    // each block 66 bits
    for (size_t i = 0; i + 66 <= msg.size(); i += 66) {
        int index = stoi(msg.substr(i, 5), nullptr, 2);
        string ip = str_ip(msg.substr(i+5, 32));
        int port = stoi(msg.substr(i+37, 13), nullptr, 2);
        int cost = stoi(msg.substr(i+50, 16), nullptr, 2);
        res.push_back(make_tuple(index, ip, port, cost));
    }
    return res;
}

string encode_msg(const string &ip, int port){
    string msg = "";
    msg += ip_str(ip);
    msg += bitset<13>(port).to_string();
    return msg;
}

string encode_LSA(const LSA_pkt &lsa){
    string msg="";
    msg += bitset<5>(lsa.sender_id).to_string();
    msg += bitset<8>(lsa.seq_number).to_string();
    msg += bitset<8>(lsa.ttl).to_string();
    for (auto &t : lsa.Neighbors) {
        int index; string ip; int port; int cost;
        tie(index, ip, port, cost) = t;
        msg += bitset<5>(index).to_string();
        msg += ip_str(ip);
        msg += bitset<13>(port).to_string();
        msg += bitset<16>(cost).to_string();
    }
    return msg;
}

LSA_pkt decode_LSA(const string &msg){
    LSA_pkt lsa;
    if (msg.size() < 21) return lsa;
    lsa.sender_id = stoi(msg.substr(0,5), nullptr, 2);
    lsa.seq_number = stoi(msg.substr(5,8), nullptr, 2);
    lsa.ttl = stoi(msg.substr(13,8), nullptr, 2);
    for (size_t i = 21; i + 66 <= msg.size(); i += 66) {
        int index = stoi(msg.substr(i, 5), nullptr, 2);
        string ip = str_ip(msg.substr(i+5, 32));
        int port = stoi(msg.substr(i+37, 13), nullptr, 2);
        int cost = stoi(msg.substr(i+50, 16), nullptr, 2);
        lsa.Neighbors.push_back(make_tuple(index, ip, port, cost));
    }
    return lsa;
}

bool node_has_complete_db(const Node &node, int total_nodes) {
    if (total_nodes <= 0) return false;
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

void print_routing_table(const Node &node, int total_nodes) {
    const int INF = numeric_limits<int>::max() / 4;
    cout << "---- Routing table for node " << node.id << " ----\n";
    if (node.routing_table.size() != (size_t)total_nodes) {
        cout << "Routing table not fully computed yet. Known LSAs: " << node.LSA_database.size() << "\n";
        // show what we have
        for (int d = 0; d < total_nodes; ++d) {
            cout << d << "\t-\t?\n";
        }
        cout << "-----------------------------------------\n";
        return;
    }
    cout << "Dest\tNextHop\tCost\n";
    for (int dest = 0; dest < total_nodes; ++dest) {
        auto entry = node.routing_table[dest];
        int nextHop = entry[0];
        int cost = entry[1];
        if (cost >= INF) {
            cout << dest << "\t-\tINF\n";
        } else {
            if (nextHop == -1) cout << dest << "\t-\t" << cost << "\n";
            else cout << dest << "\t" << nextHop << "\t" << cost << "\n";
        }
    }
    cout << "-----------------------------------------\n";
}

void dijkstra(int src, Node &node, int total_nodes) {
    const int INF = numeric_limits<int>::max() / 4;
    vector<vector<pair<int,int>>> adj(total_nodes);
    for (const auto &lsa : node.LSA_database) {
        int u = lsa.sender_id;
        for (const auto &t : lsa.Neighbors) {
            int v; string vip; int vport; int cost;
            tie(v, vip, vport, cost) = t;
            if (u >= 0 && u < total_nodes && v >= 0 && v < total_nodes) {
                adj[u].push_back({v, cost});
                adj[v].push_back({u, cost}); // undirected representation
            }
        }
    }

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

    node.routing_table.assign(total_nodes, vector<int>(2, -1));
    for (int dest = 0; dest < total_nodes; ++dest) {
        if (dest == src) {
            node.routing_table[dest] = {-1, 0};
            continue;
        }
        if (dist[dest] >= INF) {
            node.routing_table[dest] = {-1, INF};
            continue;
        }
        int p = dest;
        while (prev[p] != -1 && prev[p] != src) p = prev[p];
        int next_hop;
        if (prev[p] == src) next_hop = p;
        else if (prev[dest] == src) next_hop = dest;
        else next_hop = p;
        node.routing_table[dest] = {next_hop, dist[dest]};
    }
}

void run_link_state(Node &local_node, int total_nodes) {
    if (local_node.seq_number < 0 || local_node.seq_number > 1000000) local_node.seq_number = 0;
    local_node.seq_number += 1;

    LSA_pkt own;
    own.sender_id = local_node.id;
    own.seq_number = local_node.seq_number;
    own.ttl = 26;
    own.Neighbors = local_node.Neighbors;

    bool replaced = false;
    for (auto &e : local_node.LSA_database) {
        if (e.sender_id == own.sender_id) {
            e = own;
            replaced = true;
            break;
        }
    }
    if (!replaced) local_node.LSA_database.push_back(own);

    string enc = encode_LSA(own);
    for (auto &[nidx, nip, nport, ncost] : local_node.Neighbors) {
        sockaddr_in dest{};
        dest.sin_family = AF_INET;
        dest.sin_port = htons(nport);
        if (inet_pton(AF_INET, nip.c_str(), &dest.sin_addr) <= 0) continue;
        sendto(local_node.udp_soc, enc.c_str(), enc.size(), 0, (struct sockaddr*)&dest, sizeof(dest));
    }

    // Listen and forward until we have LSAs for all nodes
    auto last_print = chrono::steady_clock::now();
    while (!node_has_complete_db(local_node, total_nodes)) {
        fd_set readfds;
        FD_ZERO(&readfds);
        FD_SET(local_node.udp_soc, &readfds);
        int maxfd = local_node.udp_soc;
        timeval tv;
        tv.tv_sec = TIMEOUT;
        tv.tv_usec = 0;
        fd_set tempfds = readfds;
        int rv = select(maxfd + 1, &tempfds, nullptr, nullptr, &tv);
        if (rv <= 0) {
            // timeout: retransmit our own LSA to neighbors to improve reliability
            string enc2 = encode_LSA(own);
            for (auto &[nidx, nip, nport, ncost] : local_node.Neighbors) {
                sockaddr_in dest{};
                dest.sin_family = AF_INET;
                dest.sin_port = htons(nport);
                if (inet_pton(AF_INET, nip.c_str(), &dest.sin_addr) <= 0) continue;
                sendto(local_node.udp_soc, enc2.c_str(), enc2.size(), 0, (struct sockaddr*)&dest, sizeof(dest));
            }
        } else {
            if (FD_ISSET(local_node.udp_soc, &tempfds)) {
                char buffer[BUF_SIZE];
                sockaddr_in src_addr{};
                socklen_t addrlen = sizeof(src_addr);
                ssize_t n = recvfrom(local_node.udp_soc, buffer, BUF_SIZE - 1, 0, (struct sockaddr*)&src_addr, &addrlen);
                if (n <= 0) continue;
                buffer[n] = '\0';
                string msg(buffer, n);
                LSA_pkt rlsa = decode_LSA(msg);
                if (rlsa.Neighbors.empty() && rlsa.sender_id < 0) continue;
                bool shouldStoreAndForward = false;
                bool foundExisting = false;
                for (auto &stored : local_node.LSA_database) {
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
                    local_node.LSA_database.push_back(rlsa);
                    shouldStoreAndForward = true;
                }
                if (!shouldStoreAndForward) { /* no newer LSA */ }
                else {
                    rlsa.ttl = rlsa.ttl - 1;
                    if (rlsa.ttl <= 0) continue;
                    char srcipbuf[INET_ADDRSTRLEN];
                    inet_ntop(AF_INET, &(src_addr.sin_addr), srcipbuf, INET_ADDRSTRLEN);
                    string src_ip = string(srcipbuf);
                    int src_port = ntohs(src_addr.sin_port);
                    string out_enc = encode_LSA(rlsa);
                    for (auto &[nidx, nip, nport, ncost] : local_node.Neighbors) {
                        if (nip == src_ip && nport == src_port) continue;
                        sockaddr_in dest{};
                        dest.sin_family = AF_INET;
                        dest.sin_port = htons(nport);
                        if (inet_pton(AF_INET, nip.c_str(), &dest.sin_addr) <= 0) continue;
                        sendto(local_node.udp_soc, out_enc.c_str(), out_enc.size(), 0, (struct sockaddr*)&dest, sizeof(dest));
                    }
                }
            }
        }

        auto now = chrono::steady_clock::now();
        if (chrono::duration_cast<chrono::seconds>(now - last_print).count() >= PERIOD) {
            cout << "[Periodic] during flooding: ";
            print_routing_table(local_node, total_nodes);
            last_print = now;
        }
    } // end flooding loop

    cout << "Node " << local_node.id << " has complete LSA DB (" << local_node.LSA_database.size() << " LSAs)\n";
    dijkstra(local_node.id, local_node, total_nodes);
    cout << "Computed routing table after flooding.\n";
    print_routing_table(local_node, total_nodes);
}

int main(int argc, char* argv[]) {
    if (argc < 5) {
        cerr << "Usage: " << argv[0] << " <node_id> <total_nodes> <oracle_ip> <local_ip>\n";
        return 1;
    }
    int node_id = stoi(argv[1]);
    int total_nodes = stoi(argv[2]);
    string SERVER_IP = argv[3];
    string LOCAL_IP  = argv[4];

    if (node_id < 0) { cerr<<"node_id must be >=0\n"; return 1; }
    if (total_nodes <= 0) { cerr<<"total_nodes must be >0\n"; return 1; }

    Node local;
    local.id = node_id;
    local.ip = LOCAL_IP;
    local.port = 5000 + node_id;
    local.seq_number = 0;

    // create & bind UDP
    int usock = socket(AF_INET, SOCK_DGRAM, 0);
    if (usock < 0) { perror("socket creation failed"); return 1; }
    sockaddr_in uaddr{};
    uaddr.sin_family = AF_INET;
    uaddr.sin_port = htons(local.port);
    if (inet_pton(AF_INET, local.ip.c_str(), &uaddr.sin_addr) <= 0) {
        perror("Invalid local IP");
        close(usock);
        return 1;
    }
    if (bind(usock, (struct sockaddr*)&uaddr, sizeof(uaddr)) < 0) {
        perror("bind failed");
        close(usock);
        return 1;
    }
    local.udp_soc = usock;
    cout << "UDP socket bound to " << local.ip << ":" << local.port << "\n";

    // connect to oracle via TCP and register
    int tsock = socket(AF_INET, SOCK_STREAM, 0);
    if (tsock < 0) { perror("TCP socket creation failed"); close(usock); return 1; }
    sockaddr_in server_addr{};
    server_addr.sin_family = AF_INET;
    server_addr.sin_port = htons(5000);
    if (inet_pton(AF_INET, SERVER_IP.c_str(), &server_addr.sin_addr) <= 0) {
        perror("Invalid server IP address");
        close(usock); close(tsock); return 1;
    }
    if (connect(tsock, (struct sockaddr*)&server_addr, sizeof(server_addr)) < 0) {
        perror("Connection to server failed");
        close(usock); close(tsock); return 1;
    }
    local.tcp_soc = tsock;
    string encoded_msg = encode_msg(local.ip, local.port);
    if (send(tsock, encoded_msg.c_str(), encoded_msg.size(), 0) < 0) {
        perror("Send registration failed (but continuing)");
    } else {
        cout << "Sent registration to Oracle\n";
    }

    // receive initial neighbors (blocking)
    {
        vector<char> buffer(BUF_SIZE);
        ssize_t n = recv(tsock, buffer.data(), BUF_SIZE - 1, 0);
        if (n <= 0) { perror("recv initial neighbors failed"); close(usock); close(tsock); return 1; }
        string msg(buffer.data(), n);
        cout << "Received neighbors from Oracle\n";
        local.Neighbors = decode_msg(msg);
    }

    cout << "Node " << local.id << " neighbors:\n";
    for (auto &[idx,ip,port,cost] : local.Neighbors) {
        cout << "  (Index: " << idx << ", IP: " << ip << ", Port: " << port << ", Cost: " << cost << ")\n";
    }

    // initial flooding
    cout << "Starting initial link-state flooding...\n";
    run_link_state(local, total_nodes);

    // main loop: monitor oracle TCP for updates and periodically print routing table
    cout << "Will print routing table every " << PERIOD << "s and print on Oracle updates.\n";
    auto last_print = chrono::steady_clock::now();
    while (true) {
        fd_set rset;
        FD_ZERO(&rset);
        FD_SET(local.tcp_soc, &rset);
        int maxfd = local.tcp_soc;
        timeval tv;
        tv.tv_sec = TIMEOUT;
        tv.tv_usec = 0;
        int rv = select(maxfd + 1, &rset, nullptr, nullptr, &tv);
        if (rv > 0 && FD_ISSET(local.tcp_soc, &rset)) {
            vector<char> buffer(BUF_SIZE);
            ssize_t n = recv(local.tcp_soc, buffer.data(), BUF_SIZE - 1, 0);
            if (n <= 0) {
                if (n == 0) cerr << "Oracle closed connection\n";
                else perror("recv from oracle failed");
                break;
            }
            string msg(buffer.data(), n);
            cout << "Received updated neighbors from Oracle\n";
            auto new_neighbors = decode_msg(msg);
            bool changed = false;
            if (new_neighbors.size() != local.Neighbors.size()) changed = true;
            else {
                for (size_t i = 0; i < new_neighbors.size() && !changed; ++i) {
                    if (get<0>(new_neighbors[i]) != get<0>(local.Neighbors[i]) ||
                        get<1>(new_neighbors[i]) != get<1>(local.Neighbors[i]) ||
                        get<2>(new_neighbors[i]) != get<2>(local.Neighbors[i]) ||
                        get<3>(new_neighbors[i]) != get<3>(local.Neighbors[i])) {
                        changed = true;
                    }
                }
            }
            if (changed) {
                cout << "[Oracle update detected] Graph changed for this node -> re-flooding LSAs\n";
                local.Neighbors = new_neighbors;
                // re-run flooding (this will print routing table at the end and periodically during)
                run_link_state(local, total_nodes);
                cout << "[After Oracle update] new routing table:\n";
                print_routing_table(local, total_nodes);
                last_print = chrono::steady_clock::now();
            } else {
                cout << "Graph unchanged for this node\n";
            }
        }

        // periodic print (outside of flooding)
        auto now = chrono::steady_clock::now();
        if (chrono::duration_cast<chrono::seconds>(now - last_print).count() >= PERIOD) {
            cout << "[Periodic] printing routing table:\n";
            print_routing_table(local, total_nodes);
            last_print = now;
        }
    }

    close(local.udp_soc);
    close(local.tcp_soc);
    return 0;
}
