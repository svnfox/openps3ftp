#include <string>
#include <map>

#include "client.h"

using namespace std;

typedef void (*cmdfunc)(Client*, string);

#define register_cmd(a,b,c) a->insert(make_pair(b,c));
void register_cmds(map<string, cmdfunc>*);