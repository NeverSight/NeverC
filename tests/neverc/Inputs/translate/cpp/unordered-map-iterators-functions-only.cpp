#include <unordered_map>
using Map=std::unordered_map<int,int>;
using Iterator=Map::iterator;using ConstIterator=Map::const_iterator;
Iterator first(Map&m){return m.begin();}
ConstIterator first_const(const Map&m){return m.cbegin();}
Iterator lookup(Map&m,int key){return m.find(key);}
ConstIterator lookup_const(const Map&m,int key){return m.find(key);}
int*value(Iterator&it){return &it->second;}
const int*value_const(ConstIterator&it){return &it->second;}
bool same(Iterator a,ConstIterator b){return a==b;}
Iterator step(Iterator&it){return it++;}
Iterator*null_iterator(){return nullptr;}
int main(){return null_iterator()!=nullptr;}
