#include <iostream>
#include <string>
#include <vector>
#include <sstream>
#include <set>
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
        set<int> alive;
        for (int i = 0; i < sc; i++) {
            string l; getline(cin, l);
            int id; istringstream(l) >> id;
            alive.insert(id);
        }
        string out;
        for (int id : my_ids) {
            if (alive.count(id)) {
                if (!out.empty()) out += ";";
                out += to_string(id) + " RIGHT";
            }
        }
        cout << (out.empty() ? "WAIT" : out) << endl;
    }
}
