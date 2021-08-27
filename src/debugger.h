#ifndef _DEBUGGER_H_
#define _DEBUGGER_H_

#include <bits/stdc++.h>
using namespace std;

#define debug(args...) \
cout << "#" << __LINE__ << ": "; \
__dbg(__split(#args, ',').begin(), args);
vector<string> __split(const string& s, char c) {
    vector<string> v;
    stringstream ss(s);
    string x;
    while (getline(ss, x, c))
        if (x!="")
            v.emplace_back(x);
    return move(v);
}
template<typename T, typename... Args>
inline string __to_str(T x) {
    stringstream ss; ss << "[";
    for (auto it = x.begin(); it != x.end(); it++) {
        if (it != x.begin()) ss << " ";
        ss << (*it);
    }
    ss << "]";
    return ss.str();
}
template<typename T>
inline string __to_str(stack<T> x) {
    stringstream ss; ss << "[";
    bool first = 1;
    while (!x.empty()) {
        if (!first)
            ss << " ";
        ss << x.top();
        x.pop();
        first = 0;
    }
    ss << "]";
    return ss.str();
}
template<typename T1, typename T2>
ostream &operator<<(ostream &ostr, const pair<T1,T2> p) {
    ostr << "(" << p.first << "," << p.second << ")";
    return ostr;
}
template<typename T> inline void __dbg_var(vector<T> x) { cout << __to_str(x); }
template<typename T> inline void __dbg_var(list<T> x) { cout << __to_str(x); }
template<typename T> inline void __dbg_var(set<T> x) { cout << __to_str(x); }
template<typename T> inline void __dbg_var(unordered_set<T> x) { cout << __to_str(x); }
template<typename T> inline void __dbg_var(stack<T> x) { cout << __to_str(x); }
template<typename T> inline void __dbg_var(T val) { cout << val; }
inline void __dbg(vector<string>::iterator it) { cout << endl; }
template<typename T, typename... Args>
inline void __dbg(vector<string>::iterator it, T a, Args... args) {
    cout << it->substr((*it)[0] == ' ', it->length()) << "=";
    __dbg_var(a);
    cout << " ";
    __dbg(++it, args...);
}
//----------------the actual program starts here----------------

#endif