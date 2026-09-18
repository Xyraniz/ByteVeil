#include "Lua51.h"
#include <algorithm>
#include <cstdint>
#include <cstring>
#include <functional>
#include <iomanip>
#include <limits>
#include <sstream>
#include <stdexcept>
#include <string>
#include <utility>
#include <vector>

namespace ByteVeil::Lua51 {
namespace {

struct Reader {
    const std::string& data;
    size_t pos = 0;
    bool little = true;
    int sizeT = 4;
    int numberSize = 8;
    int maxString = 64 * 1024 * 1024;
    explicit Reader(const std::string& d) : data(d) {}
    void need(size_t n) { if (n > data.size() - pos) throw std::runtime_error("truncated Lua 5.1 chunk"); }
    uint8_t u8() { need(1); return uint8_t(data[pos++]); }
    uint32_t u32() { need(4); uint32_t v=0; if (little) { for(int i=0;i<4;i++) v|=uint32_t(uint8_t(data[pos++]))<<(8*i); } else { for(int i=0;i<4;i++) v=(v<<8)|uint8_t(data[pos++]); } return v; }
    uint64_t u64() { need(8); uint64_t v=0; if (little) { for(int i=0;i<8;i++) v|=uint64_t(uint8_t(data[pos++]))<<(8*i); } else { for(int i=0;i<8;i++) v=(v<<8)|uint8_t(data[pos++]); } return v; }
    int32_t i32() { return int32_t(u32()); }
    double number() { uint64_t bits=u64(); double x; std::memcpy(&x,&bits,sizeof(x)); return x; }
    std::string bytes(size_t n) { need(n); std::string s=data.substr(pos,n); pos+=n; return s; }
    std::string luaString() { uint64_t n=0; if(sizeT==4) n=u32(); else n=u64(); if(n==0) return {}; if(n>uint64_t(maxString)) throw std::runtime_error("invalid Lua 5.1 string length"); std::string s=bytes(size_t(n-1)); u8(); return s; }
};

struct LocalInfo { std::string name; int start=0; int end=0; };
struct Instr { uint32_t raw=0; int pc=0, op=0, a=0,b=0,c=0,bx=0,sbx=0; int target=-1; int openProducer=-1; };
struct Proto { int id=0,parent=-1,linedefined=0,lastline=0,nups=0,params=0,vararg=0,maxstack=0; std::string source; std::vector<int> lines; std::vector<LocalInfo> locals; std::vector<std::string> upvalues; std::vector<Instr> code; std::vector<std::string> constants; std::vector<Proto> children; };

static const char* ops[] = {"MOVE","LOADK","LOADBOOL","LOADNIL","GETUPVAL","GETGLOBAL","GETTABLE","SETGLOBAL","SETUPVAL","SETTABLE","NEWTABLE","SELF","ADD","SUB","MUL","DIV","MOD","POW","UNM","NOT","LEN","CONCAT","JMP","EQ","LT","LE","TEST","TESTSET","CALL","TAILCALL","RETURN","FORLOOP","FORPREP","TFORLOOP","SETLIST","CLOSE","CLOSURE","VARARG"};
static const char* opName(int op) { return op>=0 && op<38 ? ops[op] : "UNKNOWN"; }
static bool isJump(int op) { return op==22||op==31||op==32||op==33; }
static std::string esc(const std::string& s) { std::ostringstream o; for(unsigned char c:s){ if(c=='\\'||c=='"')o<<'\\'; if(c=='\n')o<<"\\n"; else if(c=='\r')o<<"\\r"; else if(c<32)o<<"\\x"<<std::hex<<int(c)<<std::dec; else o<<c; } return o.str(); }

static void header(Reader& r) {
    if(r.bytes(4)!="\x1bLua") throw std::runtime_error("not a Lua 5.1 chunk");
    if(r.u8()!=0x51) throw std::runtime_error("unsupported Lua version; expected 5.1");
    if(r.u8()!=0) throw std::runtime_error("unsupported Lua binary format");
    uint8_t endian=r.u8(); if(endian!=0 && endian!=1) throw std::runtime_error("invalid Lua endianness"); r.little=endian==1;
    if(r.u8()!=4) throw std::runtime_error("unsupported Lua integer width");
    r.sizeT=r.u8(); if(r.sizeT!=4 && r.sizeT!=8) throw std::runtime_error("unsupported Lua size_t width");
    if(r.u8()!=4) throw std::runtime_error("unsupported Lua instruction width");
    r.numberSize=r.u8(); if(r.numberSize!=8) throw std::runtime_error("unsupported Lua number width");
    if(r.u8()!=0) throw std::runtime_error("integral Lua number chunks are unsupported");
}

static std::string constant(Reader& r) { uint8_t tag=r.u8(); if(tag==0)return "nil"; if(tag==1)return r.u8()?"true":"false"; if(tag==3){std::ostringstream o;o<<std::setprecision(17)<<r.number();return o.str();} if(tag==4)return "\""+esc(r.luaString())+"\""; throw std::runtime_error("unsupported Lua constant tag"); }
static void advanced(std::ostringstream& o,const Proto& p){
    int alias=0, metamethod=0, scope=0; for(const auto&i:p.code){if(i.op==6||i.op==9||i.op==11)alias++; if(i.op==6||i.op==9||i.op==11)metamethod++;}
    for(size_t i=0;i<p.locals.size();i++){for(size_t j=i+1;j<p.locals.size();j++){if(p.locals[i].name==p.locals[j].name&&p.locals[i].start<p.locals[j].end&&p.locals[j].start<p.locals[i].end)scope++;}}
    for(const auto&c:p.constants)if(c.find("__index")!=std::string::npos||c.find("__newindex")!=std::string::npos||c.find("__call")!=std::string::npos)metamethod++;
    std::vector<std::vector<int>> g(p.code.size());for(const auto&i:p.code){if(i.target>=0&&i.target<int(p.code.size()))g[i.pc].push_back(i.target);if(i.pc+1<int(p.code.size())&&(i.op==23||i.op==24||i.op==25||i.op==26||i.op==27||i.op==33))g[i.pc].push_back(i.pc+1);}
    std::vector<int> idx(p.code.size(),-1),low(p.code.size()),st;std::vector<char> on(p.code.size());int seq=0,scc=0;std::function<void(int)> visit=[&](int v){idx[v]=low[v]=seq++;st.push_back(v);on[v]=1;for(int w:g[v]){if(idx[w]<0){visit(w);low[v]=std::min(low[v],low[w]);}else if(on[w])low[v]=std::min(low[v],idx[w]);}if(low[v]==idx[v]){int n=0,w;do{w=st.back();st.pop_back();on[w]=0;n++;}while(w!=v);if(n>1)scc++;}};for(size_t i=0;i<p.code.size();i++)if(idx[i]<0)visit(int(i));
    o<<"\"analysis\":{\"alias_hazards\":"<<alias<<",\"scope_overlaps\":"<<scope<<",\"dynamic_metamethod_sites\":"<<metamethod<<",\"irreducible_or_loop_sccs\":"<<scc<<"},";
}
static void parseProto(Reader& r, Proto& p, int& next, int parent, int depth) {
    if(depth>128) throw std::runtime_error("Lua prototype nesting exceeds 128");
    p.id=next++; p.parent=parent; p.source=r.luaString(); p.linedefined=r.i32(); p.lastline=r.i32(); p.nups=r.u8(); p.params=r.u8(); p.vararg=r.u8(); p.maxstack=r.u8();
    if(p.maxstack==0) throw std::runtime_error("Lua prototype has zero maxstack");
    uint32_t ncode=r.u32(); if(ncode>1000000) throw std::runtime_error("Lua instruction limit exceeded"); p.code.reserve(ncode);
    for(uint32_t pc=0;pc<ncode;pc++){ Instr i; i.pc=int(pc); i.raw=r.u32(); i.op=i.raw&0x3f; i.a=(i.raw>>6)&0xff; i.c=(i.raw>>14)&0x1ff; i.b=(i.raw>>23)&0x1ff; i.bx=(i.raw>>14)&0x3ffff; i.sbx=i.bx-131071; if(i.a>=p.maxstack && i.op!=35) throw std::runtime_error("Lua register A out of range"); if(i.op==22||i.op==31||i.op==32) i.target=i.pc+i.sbx+1; else if(i.op==33) i.target=i.pc+i.c+1; if(i.op==34&&i.b==0) for(int prev=int(pc)-1;prev>=0;--prev) if(p.code[prev].op==28||p.code[prev].op==29||p.code[prev].op==37){i.openProducer=prev;break;} p.code.push_back(i); }
    uint32_t nk=r.u32(); if(nk>1000000) throw std::runtime_error("Lua constant limit exceeded"); for(uint32_t i=0;i<nk;i++)p.constants.push_back(constant(r));
    uint32_t np=r.u32(); if(np>100000) throw std::runtime_error("Lua child prototype limit exceeded"); p.children.resize(np); for(auto& c:p.children)parseProto(r,c,next,p.id,depth+1);
    uint32_t lines=r.u32(); if(lines>1000000)throw std::runtime_error("Lua lineinfo limit exceeded"); p.lines.reserve(lines); for(uint32_t i=0;i<lines;i++)p.lines.push_back(r.i32());
    uint32_t locals=r.u32(); if(locals>1000000)throw std::runtime_error("Lua local debug limit exceeded"); p.locals.reserve(locals); for(uint32_t i=0;i<locals;i++){LocalInfo x; x.name=r.luaString(); x.start=int(r.u32()); x.end=int(r.u32()); p.locals.push_back(std::move(x));}
    uint32_t ups=r.u32(); if(ups>1000000)throw std::runtime_error("Lua upvalue debug limit exceeded"); p.upvalues.reserve(ups); for(uint32_t i=0;i<ups;i++)p.upvalues.push_back(r.luaString());
}
static Proto parse(const std::string& d) { Reader r(d); header(r); Proto p; int next=0; parseProto(r,p,next,-1,0); if(r.pos!=d.size()) throw std::runtime_error("trailing bytes after Lua chunk"); return p; }

static void jsonProto(std::ostringstream& o,const Proto& p){ o<<"{\"id\":"<<p.id<<",\"parent_id\":"<<p.parent<<",\"source\":\""<<esc(p.source)<<"\",\"parameters\":"<<p.params<<",\"registers\":"<<p.maxstack<<",";advanced(o,p);o<<"\"line_defined\":"<<p.linedefined<<",\"last_line\":"<<p.lastline<<",\"lines\":[";for(size_t i=0;i<p.lines.size();i++){if(i)o<<',';o<<p.lines[i];}o<<"],\"locals\":[";for(size_t i=0;i<p.locals.size();i++){if(i)o<<',';o<<"{\"name\":\""<<esc(p.locals[i].name)<<"\",\"start_pc\":"<<p.locals[i].start<<",\"end_pc\":"<<p.locals[i].end<<"}";}o<<"],\"upvalues\":[";for(size_t i=0;i<p.upvalues.size();i++){if(i)o<<',';o<<"\""<<esc(p.upvalues[i])<<"\"";}o<<"],\"constants\":["; for(size_t i=0;i<p.constants.size();i++){if(i)o<<',';o<<"\""<<esc(p.constants[i])<<"\"";} o<<"],\"instructions\":["; for(size_t n=0;n<p.code.size();n++){const auto&i=p.code[n];if(n)o<<',';o<<"{\"pc\":"<<i.pc<<",\"line\":"<<(i.pc<int(p.lines.size())?p.lines[i.pc]:-1)<<",\"opcode\":"<<i.op<<",\"opcode_name\":\""<<opName(i.op)<<"\",\"a\":"<<i.a<<",\"b\":"<<i.b<<",\"c\":"<<i.c<<",\"bx\":"<<i.bx<<",\"sbx\":"<<i.sbx<<",\"jump_target\":"<<i.target<<",\"open_tail\":"<<(i.op==34&&i.b==0?"true":"false")<<",\"open_producer_pc\":"<<i.openProducer<<"}";}o<<"],\"children\":[";for(size_t i=0;i<p.children.size();i++){if(i)o<<',';jsonProto(o,p.children[i]);}o<<"]}"; }
static void disProto(std::ostringstream& o,const Proto& p){ o<<"function "<<p.id<<" parent="<<p.parent<<" params="<<p.params<<" registers="<<p.maxstack<<" constants="<<p.constants.size()<<"\n"; for(const auto&i:p.code){ o<<"  @"<<i.pc<<" "<<opName(i.op)<<" A="<<i.a<<" B="<<i.b<<" C="<<i.c<<" Bx="<<i.bx<<" sBx="<<i.sbx; if(i.target>=0)o<<" -> "<<i.target; if(i.op==34&&i.b==0)o<<" OPEN_TAIL producer="<<i.openProducer; o<<"\n"; } for(const auto&c:p.children)disProto(o,c); }
static void cfgProto(std::ostringstream& o,const Proto& p){ o<<"digraph lua51_cfg_"<<p.id<<" {\n"; std::vector<int> starts{0}; for(const auto&i:p.code)if(i.target>=0){starts.push_back(i.target);if(i.pc+1<int(p.code.size()))starts.push_back(i.pc+1);} std::sort(starts.begin(),starts.end());starts.erase(std::unique(starts.begin(),starts.end()),starts.end()); for(size_t i=0;i<starts.size();i++){int end=i+1<starts.size()?starts[i+1]-1:int(p.code.size())-1;o<<"  b"<<i<<" [label=\""<<starts[i]<<".."<<end<<"\"];\n";} for(size_t i=0;i<starts.size();i++){int end=i+1<starts.size()?starts[i+1]-1:int(p.code.size())-1;const Instr* last=nullptr;for(const auto&ins:p.code)if(ins.pc>=starts[i]&&ins.pc<=end)last=&ins;if(last&&last->target>=0){auto it=std::find(starts.begin(),starts.end(),last->target);if(it!=starts.end())o<<"  b"<<i<<" -> b"<<(it-starts.begin())<<";\n";} if(last&& (last->op==23||last->op==24||last->op==25||last->op==26||last->op==27||last->op==33) && i+1<starts.size())o<<"  b"<<i<<" -> b"<<i+1<<";\n";}o<<"}\n"; }
static void luaProto(std::ostringstream& o,const Proto& p){ o<<"-- ByteVeil Lua 5.1 diagnostic decompilation\n-- function "<<p.id<<" parent="<<p.parent<<" params="<<p.params<<" registers="<<p.maxstack<<" instructions="<<p.code.size()<<"\n";for(const auto&i:p.code)o<<"-- ["<<i.pc<<"] "<<opName(i.op)<<" A="<<i.a<<" B="<<i.b<<" C="<<i.c<<"\n";for(const auto&c:p.children)luaProto(o,c);if(p.id==0)o<<"return nil\n";}
static std::string reg(int a){return "r"+std::to_string(a);}
static int debugRegister(const Proto& p,const LocalInfo& local){
    if(local.name.rfind("(",0)==0) return -1;
    if(local.start==0 && local.start<p.params) return local.start;
    for(const auto& ins:p.code){
        if(ins.pc<local.start-1 || ins.pc>=local.end) continue;
        if(ins.op==32 && ins.pc<=local.start && ins.a+3<p.maxstack) return ins.a+3;
        if(ins.pc==local.start || ins.pc==local.start-1){
            if(ins.op==36 || ins.op==1 || ins.op==2 || ins.op==3 || ins.op==10 || ins.op==12 || ins.op==13 || ins.op==14 || ins.op==15 || ins.op==16 || ins.op==17) return ins.a;
        }
    }
    return -1;
}
static std::string localReg(const Proto& p,int a,int pc){
    const LocalInfo* best=nullptr;
    for(const auto& local:p.locals){
        int mapped=debugRegister(p,local);
        if(mapped==a && (!best || (local.start<=pc && pc<local.end) || local.start>best->start)) best=&local;
    }
    return best && !best->name.empty() ? best->name : reg(a);
}
static std::string valueAt(const Proto& p,int regIndex,int pc);
static std::string upvalue(const Proto& p,int index){
    return index>=0 && index<int(p.upvalues.size()) && !p.upvalues[index].empty()
        ? p.upvalues[index] : "__upvalue_"+std::to_string(index);
}
static std::string value(const Proto& p,int x,int pc=0){
    if(x&256){ int index=x&255; return index<int(p.constants.size())?p.constants[index]:"nil"; }
    return localReg(p,x,pc);
}
static std::string valueAt(const Proto& p,int regIndex,int pc){
    for(int n=pc-1;n>=0;--n){
        const Instr& d=p.code[n];
        if(d.a!=regIndex) continue;
        if(d.op==1) return d.bx<int(p.constants.size())?p.constants[d.bx]:"nil";
        if(d.op==0) return valueAt(p,d.b, n);
        if(d.op==3) return "nil";
        break;
    }
    return localReg(p,regIndex,pc);
}
static std::string args(const Proto& p,int a,int b,int pc=0){std::ostringstream s;int n=b-1;for(int k=1;k<=n;k++){if(k>1)s<<", ";s<<valueAt(p,a+k,pc);}return s.str();}
static void liftFunctionBody(std::ostringstream& o,const Proto& p){
    for(const auto&i:p.code){
        o<<"-- pc "<<i.pc<<" "<<opName(i.op)<<"\n";
        switch(i.op){
        case 0:o<<localReg(p,i.a,i.pc)<<" = "<<localReg(p,i.b,i.pc)<<"\n";break;
        case 1:o<<localReg(p,i.a,i.pc)<<" = "<<(i.bx<p.constants.size()?p.constants[i.bx]:"nil")<<"\n";break;
        case 2:o<<localReg(p,i.a,i.pc)<<" = "<<(i.b?"true":"false")<<"\n";if(i.c)o<<"-- LOADBOOL skips pc "<<i.pc+1<<" and resumes at pc "<<i.pc+2<<"\n";break;
        case 3:o<<localReg(p,i.a,i.pc)<<" = nil\n";break;
        case 4:o<<localReg(p,i.a,i.pc)<<" = "<<upvalue(p,i.b)<<"\n";break;
        case 5:o<<localReg(p,i.a,i.pc)<<" = _G["<<(i.bx<p.constants.size()?p.constants[i.bx]:"nil")<<"]\n";break;
        case 6:o<<localReg(p,i.a,i.pc)<<" = "<<localReg(p,i.b,i.pc)<<"["<<value(p,i.c,i.pc)<<"]\n";break;
        case 7:o<<"_G["<<(i.bx<p.constants.size()?p.constants[i.bx]:"nil")<<"] = "<<localReg(p,i.a,i.pc)<<"\n";break;
        case 8:o<<upvalue(p,i.b)<<" = "<<localReg(p,i.a,i.pc)<<"\n";break;
        case 9:o<<localReg(p,i.a,i.pc)<<"["<<value(p,i.b,i.pc)<<"] = "<<value(p,i.c,i.pc)<<"\n";break;
        case 10:o<<localReg(p,i.a,i.pc)<<" = {}\n";break;
        case 11:o<<localReg(p,i.a,i.pc)<<" = ";if(i.b<p.maxstack)o<<localReg(p,i.b,i.pc)<<"["<<value(p,i.c,i.pc)<<"]";else o<<"nil";o<<"\n";break;
        case 12: case 13: case 14: case 15: case 16: case 17:{const char* op=i.op==12?"+":i.op==13?"-":i.op==14?"*":i.op==15?"/":i.op==16?"%":"^";o<<localReg(p,i.a,i.pc)<<" = "<<value(p,i.b,i.pc)<<" "<<op<<" "<<value(p,i.c,i.pc)<<"\n";break;}
        case 18:o<<localReg(p,i.a,i.pc)<<" = -"<<localReg(p,i.b,i.pc)<<"\n";break;
        case 19:o<<localReg(p,i.a,i.pc)<<" = not "<<localReg(p,i.b,i.pc)<<"\n";break;
        case 20:o<<localReg(p,i.a,i.pc)<<" = #"<<localReg(p,i.b,i.pc)<<"\n";break;
        case 21:o<<localReg(p,i.a,i.pc)<<" = "<<localReg(p,i.b,i.pc)<<" .. "<<localReg(p,i.c,i.pc)<<"\n";break;
        case 22:o<<"-- JMP to pc "<<i.target<<"\n";break;
        case 28:o<<localReg(p,i.a,i.pc)<<" = "<<localReg(p,i.a,i.pc)<<"("<<args(p,i.a,i.b,i.pc)<<")";if(i.c==0)o<<" -- open multiple returns";o<<"\n";break;
        case 29:o<<"do return "<<localReg(p,i.a,i.pc)<<"("<<args(p,i.a,i.b,i.pc)<<") end\n";break;
        case 30:{int n=i.b==0?1:i.b-1;o<<"do return ";for(int k=0;k<n;k++){if(k)o<<", ";o<<localReg(p,i.a+k,i.pc);}if(i.b==0 && (p.vararg&2))o<<", ...";o<<" end\n";break;}
        case 34:if(i.b==0)o<<"-- recovered open SETLIST at pc "<<i.pc<<" from producer pc "<<i.openProducer<<"\n";else { int base=(i.c-1)*50; for(int k=1;k<i.b;k++) o<<localReg(p,i.a,i.pc)<<"["<<base+k<<"] = "<<localReg(p,i.a+k,i.pc)<<"\n"; } break;
        case 35:o<<"-- CLOSE registers >= "<<i.a<<"; captured upvalues remain represented\n";break;
        case 36:if(i.bx<p.children.size()){const Proto& c=p.children[i.bx];o<<reg(i.a)<<" = function(";for(int k=0;k<c.params;k++){if(k)o<<", ";o<<reg(k);}if(c.vararg&2){if(c.params)o<<", ";o<<"...";}o<<")\n";for(int k=0;k<c.nups;k++)o<<"    local __upvalue_"<<k<<" = nil\n";liftFunctionBody(o,c);o<<"end\n";}else o<<"-- CLOSURE child index "<<i.bx<<" unavailable\n";break;
        case 37:if(!(p.vararg&2)){o<<"-- VARARG used by a non-variadic prototype\n";o<<localReg(p,i.a,i.pc)<<" = nil\n";}else if(i.b==0)o<<localReg(p,i.a,i.pc)<<" = ...\n";else{o<<localReg(p,i.a,i.pc);for(int k=1;k<i.b-1;k++)o<<", "<<localReg(p,i.a+k,i.pc);o<<" = ...\n";}break;
        default:o<<"-- unsupported opcode retained: "<<opName(i.op)<<" A="<<i.a<<" B="<<i.b<<" C="<<i.c<<"\n";break;
        }
    }
}

static bool isCompare(int op){ return op==23 || op==24 || op==25; }
static std::string compareExpr(const Proto& p,const Instr& i){
    std::string lhs=value(p,i.b,i.pc), rhs=value(p,i.c,i.pc);
    const char* op=i.op==23 ? " == " : i.op==24 ? " < " : " <= ";
    return "("+lhs+op+rhs+")";
}
static void emitRange(std::ostringstream& o,const Proto& p,int begin,int end,int indent);
static void emitSimple(std::ostringstream& o,const Proto& p,const Instr& i,int indent){
    std::string pad(indent,' ');
    switch(i.op){
    case 0:o<<pad<<localReg(p,i.a,i.pc)<<" = "<<localReg(p,i.b,i.pc)<<"\n";break;
    case 1:o<<pad<<localReg(p,i.a,i.pc)<<" = "<<(i.bx<p.constants.size()?p.constants[i.bx]:"nil")<<"\n";break;
    case 2:o<<pad<<localReg(p,i.a,i.pc)<<" = "<<(i.b?"true":"false")<<"\n";break;
    case 3:o<<pad<<localReg(p,i.a,i.pc)<<" = nil\n";break;
    case 5:o<<pad<<localReg(p,i.a,i.pc)<<" = _G["<<(i.bx<p.constants.size()?p.constants[i.bx]:"nil")<<"]\n";break;
    case 6:o<<pad<<localReg(p,i.a,i.pc)<<" = "<<localReg(p,i.b,i.pc)<<"["<<value(p,i.c,i.pc)<<"]\n";break;
    case 7:o<<pad<<"_G["<<(i.bx<p.constants.size()?p.constants[i.bx]:"nil")<<"] = "<<localReg(p,i.a,i.pc)<<"\n";break;
    case 9:o<<pad<<localReg(p,i.a,i.pc)<<"["<<value(p,i.b,i.pc)<<"] = "<<value(p,i.c,i.pc)<<"\n";break;
    case 10:o<<pad<<localReg(p,i.a,i.pc)<<" = {}\n";break;
    case 12: case 13: case 14: case 15: case 16: case 17:{const char* op=i.op==12?"+":i.op==13?"-":i.op==14?"*":i.op==15?"/":i.op==16?"%":"^";o<<pad<<localReg(p,i.a,i.pc)<<" = "<<value(p,i.b,i.pc)<<" "<<op<<" "<<value(p,i.c,i.pc)<<"\n";break;}
    case 18:o<<pad<<localReg(p,i.a,i.pc)<<" = -"<<localReg(p,i.b,i.pc)<<"\n";break;
    case 19:o<<pad<<localReg(p,i.a,i.pc)<<" = not "<<localReg(p,i.b,i.pc)<<"\n";break;
    case 20:o<<pad<<localReg(p,i.a,i.pc)<<" = #"<<localReg(p,i.b,i.pc)<<"\n";break;
    case 21:o<<pad<<localReg(p,i.a,i.pc)<<" = "<<localReg(p,i.b,i.pc)<<" .. "<<localReg(p,i.c,i.pc)<<"\n";break;
    case 28:o<<pad<<localReg(p,i.a,i.pc)<<" = "<<localReg(p,i.a,i.pc)<<"("<<args(p,i.a,i.b,i.pc)<<")\n";break;
    case 29:o<<pad<<"return "<<valueAt(p,i.a,i.pc)<<"("<<args(p,i.a,i.b,i.pc)<<")\n";break;
    case 30:{int n=i.b==0?1:i.b-1;if(n==0){o<<pad<<"return\n";break;}o<<pad<<"return ";for(int k=0;k<n;k++){if(k)o<<", ";o<<valueAt(p,i.a+k,i.pc);}if(i.b==0&&(p.vararg&2))o<<", ...";o<<"\n";break;}
    case 34: if(i.b>0){int base=(i.c-1)*50;for(int k=1;k<i.b;k++)o<<pad<<localReg(p,i.a,i.pc)<<"["<<base+k<<"] = "<<localReg(p,i.a+k,i.pc)<<"\n";}break;
    case 36: if(i.bx<p.children.size()){const Proto& c=p.children[i.bx];o<<pad<<localReg(p,i.a,i.pc)<<" = function(";for(int k=0;k<c.params;k++){if(k)o<<", ";o<<localReg(c,k,0);}if(c.vararg&2){if(c.params)o<<", ";o<<"...";}o<<")\n";emitRange(o,c,0,int(c.code.size()),indent+4);o<<pad<<"end\n";}break;
    default: break;
    }
}
static void emitRange(std::ostringstream& o,const Proto& p,int begin,int end,int indent){
    const std::string pad(indent,' ');
    for(int pc=begin;pc<end;){
        const Instr& i=p.code[pc];
        // GETGLOBAL/MOVE/CALL (or TAILCALL) is the common compiler shape for
        // a direct global call. Emit the source-level call and consume the
        // temporary function/argument registers.
        if(i.op==5 && pc+2<end && p.code[pc+1].op==0 &&
           (p.code[pc+2].op==28 || p.code[pc+2].op==29) &&
           p.code[pc+2].a==i.a){
            const Instr& call=p.code[pc+2];
            std::string fn=i.bx<int(p.constants.size())?p.constants[i.bx]:"_G[\"unknown\"]";
            if(fn.size()>=2 && fn.front()=='"' && fn.back()=='"') fn=fn.substr(1,fn.size()-2);
            std::string arg=valueAt(p,p.code[pc+1].b,pc+1);
            o<<pad<<(call.op==29?"return ":"")<<fn<<"("<<arg<<")\n";
            pc+=3; if(call.op==29) break; continue;
        }
        // Numeric for: FORPREP jumps over the body to FORLOOP; the loop variable is A+3.
        if(i.op==32 && i.target>pc && i.target<end && i.target<int(p.code.size()) && p.code[i.target].op==31){
            const Instr& loop=p.code[i.target]; int bodyBegin=pc+1, bodyEnd=i.target;
            std::string var=localReg(p,i.a+3,bodyBegin);
            o<<pad<<"for "<<var<<" = "<<valueAt(p,i.a,pc)<<", "<<valueAt(p,i.a+1,pc)<<", "<<valueAt(p,i.a+2,pc)<<" do\n";
            emitRange(o,p,bodyBegin,bodyEnd,indent+4); o<<pad<<"end\n"; pc=i.target+1; continue;
        }
        // Repeat/until: a conditional immediately followed by a backward jump.
        if(isCompare(i.op) && pc+1<end && p.code[pc+1].op==22 && p.code[pc+1].target>=begin && p.code[pc+1].target<pc){
            o<<pad<<"repeat\n"; emitRange(o,p,p.code[pc+1].target,pc,indent+4); o<<pad<<"until "<<compareExpr(p,i)<<"\n"; pc+=2; continue;
        }
        // While: condition, forward exit jump, body, backward jump to condition.
        if(isCompare(i.op) && pc+1<end && p.code[pc+1].op==22 && p.code[pc+1].target>pc+1){
            int bodyBegin=pc+2, exit=p.code[pc+1].target;
            if(exit<=end && exit>bodyBegin && p.code[exit-1].op==22 && p.code[exit-1].target==pc){
                o<<pad<<"while "<<compareExpr(p,i)<<" do\n"; emitRange(o,p,bodyBegin,exit-1,indent+4); o<<pad<<"end\n"; pc=exit; continue;
            }
        }
        // If/elseif/else: conditional, jump over true branch, optional join jump.
        if(isCompare(i.op) && pc+1<end && p.code[pc+1].op==22 && p.code[pc+1].target>pc+1){
            int trueBegin=pc+2, falseBegin=p.code[pc+1].target;
            int join=falseBegin;
            if(falseBegin-1>=trueBegin && p.code[falseBegin-1].op==22 && p.code[falseBegin-1].target>falseBegin){ join=p.code[falseBegin-1].target; }
            o<<pad<<"if "<<compareExpr(p,i)<<" then\n"; emitRange(o,p,trueBegin,(falseBegin-1>=trueBegin&&p.code[falseBegin-1].op==22?p.code[falseBegin-1].pc:falseBegin),indent+4);
            if(join> falseBegin){ o<<pad<<"else\n"; emitRange(o,p,falseBegin,join,indent+4); }
            o<<pad<<"end\n"; pc=join; continue;
        }
        if(i.op==22){ pc=i.target>=0?i.target:pc+1; continue; }
        if(i.op==35){ pc++; continue; }
        // FORPREP consumes the three setup registers; do not leak their
        // compiler temporaries into reconstructed source.
        bool loopSetup=false;
        bool returnSetup=(i.op==1 && pc+1<end &&
            (p.code[pc+1].op==30 || p.code[pc+1].op==29) &&
            p.code[pc+1].a==i.a);
        if(i.op==1 || i.op==0 || i.op==3){
            for(int f=pc+1;f<end && f<=pc+4;f++)
                if(p.code[f].op==32 && i.a>=p.code[f].a && i.a<=p.code[f].a+2) loopSetup=true;
        }
        if(!loopSetup && !returnSetup) emitSimple(o,p,i,indent);
        if(i.op==29 || i.op==30) break;
        pc++;
    }
}
static void readableProto(std::ostringstream& o,const Proto& p){
    o<<"-- ByteVeil Lua 5.1 lifted (reconstructed) function "<<p.id<<" (CFG/SSA conservative)\n";
    for(int i=0;i<p.params;i++) o<<"local "<<localReg(p,i,0)<<"\n";
    emitRange(o,p,0,int(p.code.size()),0);
    if(p.id==0 && (p.code.empty()||p.code.back().op!=30)) o<<"return nil\n";
}

static void liftProto(std::ostringstream& o,const Proto& p){
    o<<"-- ByteVeil Lua 5.1 lifted function "<<p.id<<" (unsupported instructions remain annotated)\n";
    liftFunctionBody(o,p);
    if(p.code.empty()||p.code.back().op!=30)o<<"return nil\n";
}
static void structuredProto(std::ostringstream& o,const Proto& p){
    const std::string pc="__pc_"+std::to_string(p.id);
    if(p.id==0) o<<"local __byteveil_functions = {}\n";
    o<<"-- ByteVeil structured CFG state machine for function "<<p.id<<"\n__byteveil_functions["<<p.id<<"] = function()\n    local function truth(v) return not not v end\n    local "<<pc<<" = 0\n    while "<<pc<<" >= 0 do\n";
    for(const auto&i:p.code){
        const int next=i.pc+1, skipped=i.pc+2;
        o<<"        if "<<pc<<" == "<<i.pc<<" then -- "<<opName(i.op)<<"\n";
        if(i.op==2 && i.c)
            o<<"            "<<pc<<" = "<<skipped<<" -- LOADBOOL skips the following instruction\n";
        else if(i.op==23||i.op==24||i.op==25||i.op==26||i.op==27){
            std::string condition;
            if(i.op==23) condition="("+value(p,i.b,i.pc)+" == "+value(p,i.c,i.pc)+")";
            else if(i.op==24) condition="("+value(p,i.b,i.pc)+" < "+value(p,i.c,i.pc)+")";
            else if(i.op==25) condition="("+value(p,i.b,i.pc)+" <= "+value(p,i.c,i.pc)+")";
            else if(i.op==26) condition="truth("+localReg(p,i.a,i.pc)+")";
            else condition="truth("+localReg(p,i.b,i.pc)+")";
            o<<"            if "<<condition<<" == "<<(i.a?"false":"true")<<" then "<<pc<<" = "<<skipped<<" else "<<pc<<" = "<<next<<" end\n";
        } else if(i.op==33){
            o<<"            if "<<localReg(p,i.a+1,i.pc)<<" == nil then "<<pc<<" = "<<skipped<<" else "<<pc<<" = "<<next<<" end\n";
        } else if(i.target>=0){
            if(i.op==31) o<<"            if "<<localReg(p,i.a,i.pc)<<" <= "<<localReg(p,i.a+1,i.pc)<<" then "<<pc<<" = "<<i.target<<" else "<<pc<<" = "<<next<<" end\n";
            else o<<"            "<<pc<<" = "<<i.target<<"\n";
        } else if(i.op==30||i.op==29){
            o<<"            "<<pc<<" = -1\n";
        } else if(next<int(p.code.size())){
            o<<"            "<<pc<<" = "<<next<<"\n";
        } else {
            o<<"            "<<pc<<" = -1\n";
        }
        o<<"            break\n        end\n";
    }
    o<<"    end\nend\n";
    for(const auto&c:p.children)structuredProto(o,c);
    if(p.id==0) o<<"return __byteveil_functions[0]\n";
}
}

bool isChunk(const std::string& data) { return data.size()>=4 && data.compare(0,4,"\x1bLua",4)==0; }
std::string inspect(const std::string& data,const std::string& mode,std::string& error){try{Proto p=parse(data);std::ostringstream o;if(mode=="json"||mode=="ir"){o<<"{\"format\":\"Lua 5.1 bytecode\",\"version\":81,\"root_function\":";jsonProto(o,p);o<<"}\n";}else if(mode=="disassemble")disProto(o,p);else if(mode=="cfg")cfgProto(o,p);else if(mode=="structured")structuredProto(o,p);else if(mode=="constants"){o<<"function 0 constants: "<<p.constants.size()<<"\n";for(size_t i=0;i<p.constants.size();i++)o<<i<<": "<<p.constants[i]<<"\n";}else if(mode=="prototypes"){std::function<void(const Proto&)> f=[&](const Proto&x){o<<"function "<<x.id<<" parent="<<x.parent<<" instructions="<<x.code.size()<<" children="<<x.children.size()<<"\n";for(const auto&c:x.children)f(c);};f(p);}else readableProto(o,p);return o.str();}catch(const std::exception&e){error=e.what();return {};}}
}
