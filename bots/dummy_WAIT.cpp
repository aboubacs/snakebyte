#include <iostream>
#include <string>
#include <vector>
using namespace std;

int main() {
    int myId, w, h;
    cin >> myId >> w >> h; cin.ignore();
    for (int i = 0; i < h; i++) { string l; getline(cin, l); }
    int n; cin >> n;
    vector<int> my_ids(n), opp_ids(n);
    for (int i = 0; i < n; i++) cin >> my_ids[i];
    for (int i = 0; i < n; i++) cin >> opp_ids[i];

    while (true) {
        int ec; cin >> ec;
        for (int i = 0; i < ec; i++) { int x, y; cin >> x >> y; }
        int sc; cin >> sc; cin.ignore();
        for (int i = 0; i < sc; i++) { string l; getline(cin, l); }
        cout << "WAIT" << endl;
    }
}
