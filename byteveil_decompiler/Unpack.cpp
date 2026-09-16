#include "Unpack.h"
#include <sstream>
#include <string>
#include <vector>
namespace ByteVeil::Unpack {
namespace { std::string esc(const std::string&s){std::string o;for(unsigned char c:s){if(c=='"'||c=='\\')o+='\\';if(c=='\n')o+="\\n";else if(c=='\r')o+="\\r";else if(c<32){o+="?";}else o+=char(c);}return o;} bool has(const std::string&d,const char*s){return d.find(s)!=std::string::npos;} }
std::string inspect(const std::string& data){
    std::string family=has(data,"MoonSec")||has(data,"moonsec")?"MoonSec V3":has(data,"Luraph")||has(data,"LuraphScript")?"Luraph":"generic-loader";
    std::vector<std::string> payloads;
    for(size_t p=0;(p=data.find("loadstring",p))!=std::string::npos;){size_t q=data.find_first_of("\"'",p+10);if(q==std::string::npos)break;char quote=data[q++];std::string x;bool ok=false;for(;q<data.size();q++){char c=data[q];if(c==quote){ok=true;break;}if(c=='\\'&&q+1<data.size()){char n=data[++q];x+=n=='n'?'\n':n=='r'?'\r':n;}else x+=c;}if(ok&&!x.empty())payloads.push_back(x);p=q+1;}
    std::ostringstream o;o<<"{\"format\":\"unpack-analysis\",\"family\":\""<<family<<"\",\"executed\":false,\"payloads\":[";for(size_t i=0;i<payloads.size();i++){if(i)o<<',';o<<"{\"kind\":\"literal-loadstring\",\"bytes\":"<<payloads[i].size()<<",\"source\":\""<<esc(payloads[i])<<"\"}";}o<<"],\"status\":\""<<(payloads.empty()?"no-literal-payload":"literal-payload-extracted")<<"\",\"note\":\"Family adapters extract only literal payloads and never execute loaders or virtual machines.\"}\n";return o.str();
}
}
