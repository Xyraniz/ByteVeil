#include "Protectors.h"
#include <sstream>
#include <string>
namespace ByteVeil::Protectors {
namespace { bool has(const std::string& d,const char* s){return d.find(s)!=std::string::npos;} std::string esc(const std::string&s){std::string o;for(char c:s){if(c=='"'||c=='\\')o+='\\';o+=c;}return o;} }
std::string analyze(const std::string& data){
    bool moon=has(data,"MoonSec")||has(data,"moonsec")||has(data,"MoonSecV3");
    bool luraph=has(data,"Luraph")||has(data,"LuraphScript");
    bool loader=has(data,"loadstring")||has(data,"string.dump")||has(data,"getfenv")||has(data,"setfenv")||has(data,"load(");
    bool roblox=has(data,"HttpGet")||has(data,"request")||has(data,"writefile")||has(data,"getgenv");
    bool vm=has(data,"while true do")&& (has(data,"local function")||has(data,"dispatch")||has(data,"opcode"));
    bool encoded=data.size()>4096 && data.find("\\255")!=std::string::npos;
    std::ostringstream o; o<<"{\"format\":\"protector-analysis\",\"static_only\":true,\"families\":[";
    bool first=true; auto add=[&](const char*n,bool yes,const char*conf,const char*reason){if(!yes)return;if(!first)o<<',';first=false;o<<"{\"name\":\""<<n<<"\",\"confidence\":\""<<conf<<"\",\"evidence\":\""<<esc(reason)<<"\"}";};
    add("MoonSec V3",moon,"high","MoonSec marker visible"); add("Luraph",luraph,"high","Luraph marker visible"); add("dynamic-loader",loader,"medium","dynamic loader primitives visible"); add("Roblox/executor-api",roblox,"medium","executor or network names visible"); add("virtual-machine-pattern",vm,"low","dispatcher-like loop pattern"); add("encoded-blob",encoded,"low","large escaped binary-looking string");
    o<<"],\"recommendation\":\"detector-only; use a family-specific unpacker only when independently validated\"}\n"; return o.str();
}
}
