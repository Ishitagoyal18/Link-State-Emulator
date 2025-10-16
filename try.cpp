#include<iostream>
#include<bitset>
#include<string>
using namespace std;
int main(){
        int num = stoi("32");
        string bin = bitset<8>(num).to_string();
        cout<<bin<<endl;
}