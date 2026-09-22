#include "Lua51.h"
#include <algorithm>
#include <cstdint>
#include <cstring>
#include <functional>
#include <iomanip>
#include <limits>
#include <set>
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
    uint64_t totalInstructions = 0;
    uint64_t totalConstants = 0;
    uint64_t totalDebugEntries = 0;
    uint64_t totalPrototypes = 0;
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
struct CaptureInfo { int bindingPc=-1; int sourceIndex=-1; bool fromUpvalue=false; };
struct Instr {
    uint32_t raw=0;
    int pc=0, op=0, a=0,b=0,c=0,bx=0,sbx=0;
    int target=-1;
    int openProducer=-1;
    int setlistBlock=-1;
    // Lua 5.1 stores CLOSURE upvalue bindings as the following N instructions.
    // They are operands of CLOSURE, not independently executed operations.
    int closureBindingFor=-1;
    int captureSlot=-1;
    bool extraWord=false;
    std::vector<CaptureInfo> captures;
};
struct ConstantInfo { std::string type; std::string lua; std::string value; std::string stringBytesHex; };
struct Proto { int id=0,parent=-1,linedefined=0,lastline=0,nups=0,params=0,vararg=0,maxstack=0; std::string source; std::vector<int> lines; std::vector<LocalInfo> locals; std::vector<std::string> upvalues; std::vector<Instr> code; std::vector<std::string> constants; std::vector<ConstantInfo> constantTable; std::vector<Proto> children; };

static const char* ops[] = {"MOVE","LOADK","LOADBOOL","LOADNIL","GETUPVAL","GETGLOBAL","GETTABLE","SETGLOBAL","SETUPVAL","SETTABLE","NEWTABLE","SELF","ADD","SUB","MUL","DIV","MOD","POW","UNM","NOT","LEN","CONCAT","JMP","EQ","LT","LE","TEST","TESTSET","CALL","TAILCALL","RETURN","FORLOOP","FORPREP","TFORLOOP","SETLIST","CLOSE","CLOSURE","VARARG"};
static const char* opName(int op) { return op==-1 ? "EXTRAARG" : op>=0 && op<38 ? ops[op] : "UNKNOWN"; }

static std::string bytesHex(const std::string& value) {
    static const char digits[]="0123456789abcdef"; std::string result; result.reserve(value.size()*2);
    for(unsigned char byte:value){result.push_back(digits[byte>>4]);result.push_back(digits[byte&15]);}
    return result;
}

static std::string luaQuote(const std::string& value) {
    std::ostringstream o; o<<'"';
    for(unsigned char c:value){
        switch(c){
        case '\\':o<<"\\\\";break; case '"':o<<"\\\"";break; case '\a':o<<"\\a";break;
        case '\b':o<<"\\b";break; case '\f':o<<"\\f";break; case '\n':o<<"\\n";break;
        case '\r':o<<"\\r";break; case '\t':o<<"\\t";break; case '\v':o<<"\\v";break;
        default:
            if(c>=32&&c<=126)o<<char(c);
            else o<<'\\'<<std::setw(3)<<std::setfill('0')<<std::dec<<unsigned(c);
            break;
        }
    }
    o<<'"'; return o.str();
}

static std::string jsonEscape(const std::string& value) {
    std::ostringstream out;
    auto escapeByte=[&](unsigned char byte){static const char digits[]="0123456789abcdef";out<<"\\u00"<<digits[byte>>4]<<digits[byte&15];};
    for(size_t index=0;index<value.size();){
        unsigned char c=static_cast<unsigned char>(value[index]);
        if(c=='"'){out<<"\\\"";++index;continue;} if(c=='\\'){out<<"\\\\";++index;continue;}
        if(c=='\b'){out<<"\\b";++index;continue;} if(c=='\f'){out<<"\\f";++index;continue;}
        if(c=='\n'){out<<"\\n";++index;continue;} if(c=='\r'){out<<"\\r";++index;continue;}
        if(c=='\t'){out<<"\\t";++index;continue;} if(c<0x20){escapeByte(c);++index;continue;}
        if(c<0x80){out<<char(c);++index;continue;}
        size_t length=c>=0xc2&&c<=0xdf?2:c>=0xe0&&c<=0xef?3:c>=0xf0&&c<=0xf4?4:0;
        bool valid=length&&index+length<=value.size();
        for(size_t offset=1;valid&&offset<length;++offset)valid=(static_cast<unsigned char>(value[index+offset])&0xc0)==0x80;
        if(valid&&length==3){unsigned char second=static_cast<unsigned char>(value[index+1]);valid=!((c==0xe0&&second<0xa0)||(c==0xed&&second>=0xa0));}
        if(valid&&length==4){unsigned char second=static_cast<unsigned char>(value[index+1]);valid=!((c==0xf0&&second<0x90)||(c==0xf4&&second>=0x90));}
        if(valid){out.write(value.data()+index,std::streamsize(length));index+=length;}else{escapeByte(c);++index;}
    }
    return out.str();
}

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

static ConstantInfo constant(Reader& r) {
    ConstantInfo result; uint8_t tag=r.u8();
    if(tag==0){result.type="nil";result.lua="nil";result.value="nil";return result;}
    if(tag==1){result.type="boolean";result.value=r.u8()?"true":"false";result.lua=result.value;return result;}
    if(tag==3){result.type="number";std::ostringstream o;o<<std::setprecision(17)<<r.number();result.value=o.str();result.lua=result.value;return result;}
    if(tag==4){result.type="string";result.value=r.luaString();result.stringBytesHex=bytesHex(result.value);result.lua=luaQuote(result.value);return result;}
    throw std::runtime_error("unsupported Lua constant tag");
}
static void advanced(std::ostringstream& o,const Proto& p){
    int alias=0, metamethod=0, scope=0; for(const auto&i:p.code){if(i.op==6||i.op==9||i.op==11)alias++; if(i.op==6||i.op==9||i.op==11)metamethod++;}
    for(size_t i=0;i<p.locals.size();i++){for(size_t j=i+1;j<p.locals.size();j++){if(p.locals[i].name==p.locals[j].name&&p.locals[i].start<p.locals[j].end&&p.locals[j].start<p.locals[i].end)scope++;}}
    for(const auto&c:p.constants)if(c.find("__index")!=std::string::npos||c.find("__newindex")!=std::string::npos||c.find("__call")!=std::string::npos)metamethod++;
    std::vector<std::vector<int>> g(p.code.size());for(const auto&i:p.code){if(i.target>=0&&i.target<int(p.code.size()))g[i.pc].push_back(i.target);if(i.pc+1<int(p.code.size())&&(i.op==23||i.op==24||i.op==25||i.op==26||i.op==27||i.op==33))g[i.pc].push_back(i.pc+1);}
    std::vector<int> idx(p.code.size(),-1),low(p.code.size()),st;std::vector<char> on(p.code.size());int seq=0,scc=0;std::function<void(int)> visit=[&](int v){idx[v]=low[v]=seq++;st.push_back(v);on[v]=1;for(int w:g[v]){if(idx[w]<0){visit(w);low[v]=std::min(low[v],low[w]);}else if(on[w])low[v]=std::min(low[v],idx[w]);}if(low[v]==idx[v]){int n=0,w;do{w=st.back();st.pop_back();on[w]=0;n++;}while(w!=v);if(n>1)scc++;}};for(size_t i=0;i<p.code.size();i++)if(idx[i]<0)visit(int(i));
    o<<"\"analysis\":{\"alias_hazards\":"<<alias<<",\"scope_overlaps\":"<<scope<<",\"dynamic_metamethod_sites\":"<<metamethod<<",\"irreducible_or_loop_sccs\":"<<scc<<"},";
}

static void associateClosureCaptures(Proto& p){
    for(int pc=0;pc<int(p.code.size());++pc){
        Instr& closure=p.code[pc];
        if(closure.op!=36) continue;
        // The child index itself is checked by validateProtoData.  Deferring
        // that error keeps all malformed-operand diagnostics in one place.
        if(closure.bx<0||closure.bx>=int(p.children.size())) continue;
        const int expected=p.children[closure.bx].nups;
        if(expected>int(p.code.size())-pc-1)
            throw std::runtime_error("Lua function "+std::to_string(p.id)+" pc "+std::to_string(closure.pc)+": CLOSURE capture bindings truncated");
        closure.captures.clear();
        closure.captures.reserve(size_t(expected));
        for(int slot=0;slot<expected;++slot){
            Instr& binding=p.code[pc+slot+1];
            if(binding.extraWord || (binding.op!=0 && binding.op!=4))
                throw std::runtime_error("Lua function "+std::to_string(p.id)+" pc "+std::to_string(binding.pc)+": CLOSURE capture binding must be MOVE or GETUPVAL");
            binding.closureBindingFor=closure.pc;
            binding.captureSlot=slot;
            closure.captures.push_back({binding.pc,binding.b,binding.op==4});
        }
        // A valid binding cannot itself be CLOSURE, so no later closure is
        // skipped.  Advancing avoids treating the pseudo-instructions as
        // regular bytecode during this association pass.
        pc+=expected;
    }
}

static void validateProtoData(const Proto& p){
    auto failure=[&](const Instr& i,const std::string& message){throw std::runtime_error("Lua function "+std::to_string(p.id)+" pc "+std::to_string(i.pc)+": "+message);};
    auto checkReg=[&](const Instr& i,int reg,const char* field){if(reg<0||reg>=p.maxstack)failure(i,std::string("register ")+field+" out of range");};
    auto checkRange=[&](const Instr& i,int first,int count,const char* field){if(count<0||first<0||first+count>p.maxstack)failure(i,std::string("register range ")+field+" out of range");};
    auto checkRK=[&](const Instr& i,int operand,const char* field){if(operand&256){if((operand&255)>=int(p.constants.size()))failure(i,std::string("constant ")+field+" out of range");}else checkReg(i,operand,field);};
    auto checkConstant=[&](const Instr& i,int index){if(index<0||index>=int(p.constants.size()))failure(i,"constant index out of range");};
    auto checkUpvalue=[&](const Instr& i,int index){if(index<0||index>=p.nups)failure(i,"upvalue index out of range");};
    auto checkTarget=[&](const Instr& i){
        if(i.target<0||i.target>=int(p.code.size())||p.code[i.target].extraWord||p.code[i.target].closureBindingFor>=0)
            failure(i,"jump target is not an instruction boundary");
    };

    for(const Instr& i:p.code){
        if(i.extraWord)continue;
        if(i.op<0||i.op>=38)failure(i,"unknown opcode "+std::to_string(i.op));
        // CLOSURE consumes its following MOVE/GETUPVAL records.  Lua's VM
        // reads only B from those records; validating A as a normal MOVE or
        // GETUPVAL operand would incorrectly reject a semantically valid
        // closure binding and would model it as a separate operation.
        if(i.closureBindingFor>=0){
            if(i.op==0) checkReg(i,i.b,"CLOSURE local capture B");
            else checkUpvalue(i,i.b);
            continue;
        }
        switch(i.op){
        case 0:checkReg(i,i.a,"A");checkReg(i,i.b,"B");break;
        case 1:checkReg(i,i.a,"A");checkConstant(i,i.bx);break;
        case 2:checkReg(i,i.a,"A");if(i.b>1)failure(i,"LOADBOOL boolean operand out of range");if(i.c)checkTarget(i);break;
        case 3:checkReg(i,i.a,"A");checkReg(i,i.b,"B");if(i.b<i.a)failure(i,"LOADNIL range is reversed");break;
        case 4:checkReg(i,i.a,"A");checkUpvalue(i,i.b);break;
        case 5:checkReg(i,i.a,"A");checkConstant(i,i.bx);break;
        case 6:checkReg(i,i.a,"A");checkReg(i,i.b,"B");checkRK(i,i.c,"C");break;
        case 7:checkReg(i,i.a,"A");checkConstant(i,i.bx);break;
        case 8:checkReg(i,i.a,"A");checkUpvalue(i,i.b);break;
        case 9:checkReg(i,i.a,"A");checkRK(i,i.b,"B");checkRK(i,i.c,"C");break;
        case 10:checkReg(i,i.a,"A");break;
        case 11:checkRange(i,i.a,2,"SELF results");checkReg(i,i.b,"B");checkRK(i,i.c,"C");break;
        case 12:case 13:case 14:case 15:case 16:case 17:checkReg(i,i.a,"A");checkRK(i,i.b,"B");checkRK(i,i.c,"C");break;
        case 18:case 19:case 20:checkReg(i,i.a,"A");checkReg(i,i.b,"B");break;
        case 21:checkReg(i,i.a,"A");checkReg(i,i.b,"B");checkReg(i,i.c,"C");if(i.b>i.c)failure(i,"CONCAT range is reversed");break;
        case 22:if(i.a>p.maxstack)failure(i,"JMP close register out of range");checkTarget(i);break;
        case 23:case 24:case 25:if(i.a>1)failure(i,"comparison inversion flag out of range");checkRK(i,i.b,"B");checkRK(i,i.c,"C");checkTarget(i);break;
        case 26:checkReg(i,i.a,"A");if(i.c>1)failure(i,"TEST boolean operand out of range");checkTarget(i);break;
        case 27:checkReg(i,i.a,"A");checkReg(i,i.b,"B");if(i.c>1)failure(i,"TESTSET boolean operand out of range");checkTarget(i);break;
        case 28:case 29:checkReg(i,i.a,"A");if(i.b>1)checkRange(i,i.a+1,i.b-1,"call arguments");if(i.op==28&&i.c>1)checkRange(i,i.a,i.c-1,"call results");break;
        case 30:checkReg(i,i.a,"A");if(i.b>0)checkRange(i,i.a,i.b-1,"return values");break;
        case 31:case 32:checkRange(i,i.a,4,"numeric for");checkTarget(i);break;
        case 33:if(i.c==0)failure(i,"TFORLOOP result count is zero");checkRange(i,i.a,3+i.c,"generic for");checkTarget(i);break;
        case 34:checkReg(i,i.a,"A");if(i.b>0)checkRange(i,i.a+1,i.b,"SETLIST values");if(i.setlistBlock<=0)failure(i,"SETLIST block index is zero");break;
        case 35:if(i.a<0||i.a>p.maxstack)failure(i,"CLOSE register out of range");break;
        case 36:
            checkReg(i,i.a,"A");
            if(i.bx<0||i.bx>=int(p.children.size()))failure(i,"child prototype index out of range");
            if(i.bx>=0&&i.bx<int(p.children.size())&&int(i.captures.size())!=p.children[i.bx].nups)
                failure(i,"CLOSURE capture binding count does not match child upvalues");
            break;
        case 37:checkReg(i,i.a,"A");if(i.b>1)checkRange(i,i.a,i.b-1,"vararg results");break;
        }
    }
    if(!p.lines.empty()&&p.lines.size()!=p.code.size())throw std::runtime_error("Lua function "+std::to_string(p.id)+": lineinfo count does not match code size");
    if(p.upvalues.size()>size_t(p.nups))throw std::runtime_error("Lua function "+std::to_string(p.id)+": upvalue debug count exceeds upvalue count");
    for(const LocalInfo& local:p.locals)if(local.start<0||local.end<local.start||local.end>int(p.code.size()))throw std::runtime_error("Lua function "+std::to_string(p.id)+": invalid local debug range");
}

static void parseProto(Reader& r, Proto& p, int& next, int parent, int depth) {
    if(depth>128) throw std::runtime_error("Lua prototype nesting exceeds 128");
    if(++r.totalPrototypes>100000) throw std::runtime_error("Lua prototype limit exceeded");
    p.id=next++; p.parent=parent; p.source=r.luaString(); p.linedefined=r.i32(); p.lastline=r.i32(); p.nups=r.u8(); p.params=r.u8(); p.vararg=r.u8(); p.maxstack=r.u8();
    if(p.maxstack==0) throw std::runtime_error("Lua prototype has zero maxstack");
    uint32_t ncode=r.u32(); if(ncode>1000000-r.totalInstructions) throw std::runtime_error("Lua instruction limit exceeded"); r.totalInstructions+=ncode; p.code.reserve(ncode);
    std::vector<uint32_t> rawCode; rawCode.reserve(ncode); for(uint32_t pc=0;pc<ncode;pc++)rawCode.push_back(r.u32());
    bool nextIsExtra=false;
    for(uint32_t pc=0;pc<ncode;pc++){
        Instr i; i.pc=int(pc); i.raw=rawCode[pc];
        if(nextIsExtra){i.op=-1;i.extraWord=true;i.setlistBlock=int(i.raw);nextIsExtra=false;p.code.push_back(i);continue;}
        i.op=i.raw&0x3f; i.a=(i.raw>>6)&0xff; i.c=(i.raw>>14)&0x1ff; i.b=(i.raw>>23)&0x1ff; i.bx=(i.raw>>14)&0x3ffff; i.sbx=i.bx-131071;
        if(i.op==22||i.op==31||i.op==32)i.target=i.pc+i.sbx+1;
        else if((i.op>=23&&i.op<=27)||i.op==33)i.target=i.pc+2;
        else if(i.op==2&&i.c)i.target=i.pc+2;
        if(i.op==34){
            if(i.c==0){if(pc+1>=ncode)throw std::runtime_error("Lua SETLIST is missing its extra block word");i.setlistBlock=int(rawCode[pc+1]);nextIsExtra=true;}
            else i.setlistBlock=i.c;
            if(i.b==0)for(int prev=int(pc)-1;prev>=0;--prev)if(p.code[prev].op==28||p.code[prev].op==29||p.code[prev].op==37){i.openProducer=prev;break;}
        }
        p.code.push_back(i);
    }
    uint32_t nk=r.u32(); if(nk>1000000-r.totalConstants) throw std::runtime_error("Lua constant limit exceeded"); r.totalConstants+=nk; for(uint32_t i=0;i<nk;i++){ConstantInfo info=constant(r);p.constants.push_back(info.lua);p.constantTable.push_back(std::move(info));}
    uint32_t np=r.u32(); if(np>100000) throw std::runtime_error("Lua child prototype limit exceeded"); p.children.resize(np); for(auto& c:p.children)parseProto(r,c,next,p.id,depth+1);
    uint32_t lines=r.u32(); if(lines>1000000-r.totalDebugEntries)throw std::runtime_error("Lua lineinfo limit exceeded"); r.totalDebugEntries+=lines; p.lines.reserve(lines); for(uint32_t i=0;i<lines;i++)p.lines.push_back(r.i32());
    uint32_t locals=r.u32(); if(locals>1000000-r.totalDebugEntries)throw std::runtime_error("Lua local debug limit exceeded"); r.totalDebugEntries+=locals; p.locals.reserve(locals); for(uint32_t i=0;i<locals;i++){LocalInfo x; x.name=r.luaString(); x.start=int(r.u32()); x.end=int(r.u32()); p.locals.push_back(std::move(x));}
    uint32_t ups=r.u32(); if(ups>1000000-r.totalDebugEntries)throw std::runtime_error("Lua upvalue debug limit exceeded"); r.totalDebugEntries+=ups; p.upvalues.reserve(ups); for(uint32_t i=0;i<ups;i++)p.upvalues.push_back(r.luaString());
    associateClosureCaptures(p);
    validateProtoData(p);
}
static Proto parse(const std::string& d) { Reader r(d); header(r); Proto p; int next=0; parseProto(r,p,next,-1,0); if(r.pos!=d.size()) throw std::runtime_error("trailing bytes after Lua chunk"); return p; }

static void jsonProto(std::ostringstream& o,const Proto& p){
    o<<"{\"id\":"<<p.id<<",\"parent_id\":"<<p.parent<<",\"source\":\""<<jsonEscape(p.source)
     <<"\",\"source_bytes_hex\":\""<<bytesHex(p.source)<<"\",\"parameters\":"<<p.params<<",\"registers\":"<<p.maxstack<<",";
    advanced(o,p);
    o<<"\"line_defined\":"<<p.linedefined<<",\"last_line\":"<<p.lastline<<",\"lines\":[";
    for(size_t i=0;i<p.lines.size();i++){if(i)o<<',';o<<p.lines[i];}
    o<<"],\"locals\":[";
    for(size_t i=0;i<p.locals.size();i++){
        if(i)o<<',';
        o<<"{\"name\":\""<<jsonEscape(p.locals[i].name)<<"\",\"name_bytes_hex\":\""<<bytesHex(p.locals[i].name)
         <<"\",\"start_pc\":"<<p.locals[i].start<<",\"end_pc\":"<<p.locals[i].end<<"}";
    }
    o<<"],\"upvalues\":[";
    for(size_t i=0;i<p.upvalues.size();i++){if(i)o<<',';o<<"\""<<jsonEscape(p.upvalues[i])<<"\"";}
    o<<"],\"upvalue_bytes_hex\":[";
    for(size_t i=0;i<p.upvalues.size();i++){if(i)o<<',';o<<"\""<<bytesHex(p.upvalues[i])<<"\"";}
    o<<"],\"constants\":[";
    for(size_t i=0;i<p.constants.size();i++){if(i)o<<',';o<<"\""<<jsonEscape(p.constants[i])<<"\"";}
    o<<"],\"constant_table\":[";
    for(size_t i=0;i<p.constantTable.size();i++){
        if(i)o<<',';
        const ConstantInfo& constant=p.constantTable[i];
        o<<"{\"index\":"<<i<<",\"type\":\""<<constant.type<<"\",\"value\":\""<<jsonEscape(constant.value)
         <<"\",\"lua\":\""<<jsonEscape(constant.lua)<<"\",\"string_bytes_hex\":\""<<constant.stringBytesHex<<"\"}";
    }
    o<<"],\"instructions\":[";
    for(size_t n=0;n<p.code.size();n++){
        const auto&i=p.code[n];if(n)o<<',';
        o<<"{\"pc\":"<<i.pc<<",\"line\":"<<(i.pc<int(p.lines.size())?p.lines[i.pc]:-1)<<",\"opcode\":"<<i.op
         <<",\"opcode_name\":\""<<opName(i.op)<<"\",\"a\":"<<i.a<<",\"b\":"<<i.b<<",\"c\":"<<i.c
         <<",\"bx\":"<<i.bx<<",\"sbx\":"<<i.sbx<<",\"jump_target\":"<<i.target
         <<",\"extra_word\":"<<(i.extraWord?"true":"false")<<",\"setlist_block\":"<<i.setlistBlock
         <<",\"open_tail\":"<<(i.op==34&&i.b==0?"true":"false")<<",\"open_producer_pc\":"<<i.openProducer
         <<",\"closure_binding_for_pc\":"<<i.closureBindingFor<<",\"capture_slot\":"<<i.captureSlot<<",\"captures\":[";
        for(size_t capture=0;capture<i.captures.size();++capture){
            if(capture)o<<',';
            const CaptureInfo& info=i.captures[capture];
            o<<"{\"slot\":"<<capture<<",\"binding_pc\":"<<info.bindingPc
             <<",\"kind\":\""<<(info.fromUpvalue?"upvalue":"local")<<"\",\"source_index\":"<<info.sourceIndex<<"}";
        }
        o<<"]}";
    }
    o<<"],\"children\":[";for(size_t i=0;i<p.children.size();i++){if(i)o<<',';jsonProto(o,p.children[i]);}o<<"]}";
}
static void disProto(std::ostringstream& o,const Proto& p){
    o<<"function "<<p.id<<" parent="<<p.parent<<" params="<<p.params<<" registers="<<p.maxstack<<" constants="<<p.constants.size()<<"\n";
    for(const auto&i:p.code){
        o<<"  @"<<i.pc<<" "<<opName(i.op);
        if(i.extraWord)o<<" raw="<<i.raw;
        else { o<<" A="<<i.a<<" B="<<i.b<<" C="<<i.c<<" Bx="<<i.bx<<" sBx="<<i.sbx; if(i.op==34)o<<" block="<<i.setlistBlock; }
        if(i.target>=0)o<<" -> "<<i.target;
        if(i.op==34&&i.b==0)o<<" OPEN_TAIL producer="<<i.openProducer;
        if(i.closureBindingFor>=0)
            o<<" CLOSURE_BINDING owner="<<i.closureBindingFor<<" slot="<<i.captureSlot;
        if(i.op==36){
            o<<" CAPTURES="<<i.captures.size();
            for(size_t slot=0;slot<i.captures.size();++slot){
                const CaptureInfo& capture=i.captures[slot];
                o<<" ["<<slot<<":"<<(capture.fromUpvalue?"upvalue":"local")<<" "<<capture.sourceIndex<<" at "<<capture.bindingPc<<"]";
            }
        }
        o<<"\n";
    }
    for(const auto&c:p.children)disProto(o,c);
}
static void cfgProto(std::ostringstream& o,const Proto& p){ o<<"digraph lua51_cfg_"<<p.id<<" {\n"; std::vector<int> starts{0}; for(const auto&i:p.code)if(i.target>=0){starts.push_back(i.target);if(i.pc+1<int(p.code.size()))starts.push_back(i.pc+1);} std::sort(starts.begin(),starts.end());starts.erase(std::unique(starts.begin(),starts.end()),starts.end()); for(size_t i=0;i<starts.size();i++){int end=i+1<starts.size()?starts[i+1]-1:int(p.code.size())-1;o<<"  b"<<i<<" [label=\""<<starts[i]<<".."<<end<<"\"];\n";} for(size_t i=0;i<starts.size();i++){int end=i+1<starts.size()?starts[i+1]-1:int(p.code.size())-1;const Instr* last=nullptr;for(const auto&ins:p.code)if(ins.pc>=starts[i]&&ins.pc<=end)last=&ins;if(last&&last->target>=0){auto it=std::find(starts.begin(),starts.end(),last->target);if(it!=starts.end())o<<"  b"<<i<<" -> b"<<(it-starts.begin())<<";\n";} if(last&& (last->op==23||last->op==24||last->op==25||last->op==26||last->op==27||last->op==33) && i+1<starts.size())o<<"  b"<<i<<" -> b"<<i+1<<";\n";}o<<"}\n"; }
static void luaProto(std::ostringstream& o,const Proto& p){ o<<"-- ByteVeil Lua 5.1 diagnostic decompilation\n-- function "<<p.id<<" parent="<<p.parent<<" params="<<p.params<<" registers="<<p.maxstack<<" instructions="<<p.code.size()<<"\n";for(const auto&i:p.code)o<<"-- ["<<i.pc<<"] "<<opName(i.op)<<" A="<<i.a<<" B="<<i.b<<" C="<<i.c<<"\n";for(const auto&c:p.children)luaProto(o,c);if(p.id==0)o<<"return nil\n";}
static std::string reg(int a){return "r"+std::to_string(a);}
static bool validIdentifier(const std::string& name){
    if(name.empty()||!((name[0]>='a'&&name[0]<='z')||(name[0]>='A'&&name[0]<='Z')||name[0]=='_'))return false;
    for(unsigned char c:name)if(!((c>='a'&&c<='z')||(c>='A'&&c<='Z')||(c>='0'&&c<='9')||c=='_'))return false;
    static const char* keywords[]={"and","break","do","else","elseif","end","false","for","function","if","in","local","nil","not","or","repeat","return","then","true","until","while"};
    for(const char* keyword:keywords)if(name==keyword)return false;
    return true;
}
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
    return best && validIdentifier(best->name) ? best->name : reg(a);
}
static std::string valueAt(const Proto& p,int regIndex,int pc);
static std::string upvalue(const Proto& p,int index){
    return index>=0 && index<int(p.upvalues.size()) && validIdentifier(p.upvalues[index])
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

struct RenderContext {
    // Entries point at lexical variables in the enclosing emitted function.
    // They are created from the CLOSURE pseudo-instructions, never guessed.
    std::vector<std::string> capturedUpvalues;
};

static std::string renderedLocal(const Proto& p,int registerIndex,int pc,const RenderContext& context){
    (void)context;
    // Root names remain compact for readable diagnostics.  Nested prototypes
    // need their own register namespace so an inner r0 cannot shadow an outer
    // r0 captured by a closure.
    if(p.id==0) return localReg(p,registerIndex,pc);
    return "__byteveil_f"+std::to_string(p.id)+"_r"+std::to_string(registerIndex);
}

static std::string renderedUpvalue(const Proto& p,int index,const RenderContext& context){
    if(index>=0&&index<int(context.capturedUpvalues.size())&&!context.capturedUpvalues[index].empty())
        return context.capturedUpvalues[index];
    return upvalue(p,index);
}

static std::string renderedValue(const Proto& p,int x,int pc,const RenderContext& context){
    if(x&256){ int index=x&255; return index<int(p.constants.size())?p.constants[index]:"nil"; }
    return renderedLocal(p,x,pc,context);
}

static std::string renderedValueAt(const Proto& p,int registerIndex,int pc,const RenderContext& context){
    for(int n=pc-1;n>=0;--n){
        const Instr& definition=p.code[n];
        if(definition.closureBindingFor>=0 || definition.a!=registerIndex) continue;
        if(definition.op==1) return definition.bx<int(p.constants.size())?p.constants[definition.bx]:"nil";
        if(definition.op==0) return renderedValueAt(p,definition.b,n,context);
        if(definition.op==3) return "nil";
        break;
    }
    return renderedLocal(p,registerIndex,pc,context);
}

static std::string renderedArgs(const Proto& p,int a,int b,int pc,const RenderContext& context){
    std::ostringstream result;
    for(int k=1;k<b;k++){ if(k>1) result<<", "; result<<renderedValueAt(p,a+k,pc,context); }
    return result.str();
}

static void emitRegisterDeclarations(std::ostringstream& o,const Proto& p,const RenderContext& context,int indent,int firstRegister){
    std::set<std::string> names;
    for(int regIndex=firstRegister;regIndex<p.maxstack;++regIndex){
        if(p.id==0){
            names.insert(renderedLocal(p,regIndex,0,context));
            for(const Instr& instruction:p.code)
                names.insert(renderedLocal(p,regIndex,instruction.pc,context));
        }else names.insert(renderedLocal(p,regIndex,0,context));
    }
    if(names.empty()) return;
    o<<std::string(size_t(indent),' ')<<"local ";
    bool first=true;
    for(const std::string& name:names){ if(!first)o<<", "; o<<name; first=false; }
    o<<"\n";
}
static void liftFunctionBody(std::ostringstream& o,const Proto& p){
    for(const auto&i:p.code){
        o<<"-- pc "<<i.pc<<" "<<opName(i.op)<<"\n";
        switch(i.op){
        case 0:o<<localReg(p,i.a,i.pc)<<" = "<<localReg(p,i.b,i.pc)<<"\n";break;
        case 1:o<<localReg(p,i.a,i.pc)<<" = "<<(size_t(i.bx)<p.constants.size()?p.constants[i.bx]:"nil")<<"\n";break;
        case 2:o<<localReg(p,i.a,i.pc)<<" = "<<(i.b?"true":"false")<<"\n";if(i.c)o<<"-- LOADBOOL skips pc "<<i.pc+1<<" and resumes at pc "<<i.pc+2<<"\n";break;
        case 3:o<<localReg(p,i.a,i.pc)<<" = nil\n";break;
        case 4:o<<localReg(p,i.a,i.pc)<<" = "<<upvalue(p,i.b)<<"\n";break;
        case 5:o<<localReg(p,i.a,i.pc)<<" = _G["<<(size_t(i.bx)<p.constants.size()?p.constants[i.bx]:"nil")<<"]\n";break;
        case 6:o<<localReg(p,i.a,i.pc)<<" = "<<localReg(p,i.b,i.pc)<<"["<<value(p,i.c,i.pc)<<"]\n";break;
        case 7:o<<"_G["<<(size_t(i.bx)<p.constants.size()?p.constants[i.bx]:"nil")<<"] = "<<localReg(p,i.a,i.pc)<<"\n";break;
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
        case 34:if(i.b==0)o<<"-- recovered open SETLIST at pc "<<i.pc<<" from producer pc "<<i.openProducer<<" block "<<i.setlistBlock<<"\n";else { int base=(i.setlistBlock-1)*50; for(int k=1;k<=i.b;k++) o<<localReg(p,i.a,i.pc)<<"["<<base+k<<"] = "<<localReg(p,i.a+k,i.pc)<<"\n"; } break;
        case 35:o<<"-- CLOSE registers >= "<<i.a<<"; captured upvalues remain represented\n";break;
        case 36:if(size_t(i.bx)<p.children.size()){const Proto& c=p.children[i.bx];o<<reg(i.a)<<" = function(";for(int k=0;k<c.params;k++){if(k)o<<", ";o<<reg(k);}if(c.vararg&2){if(c.params)o<<", ";o<<"...";}o<<")\n";for(int k=0;k<c.nups;k++)o<<"    local __upvalue_"<<k<<" = nil\n";liftFunctionBody(o,c);o<<"end\n";}else o<<"-- CLOSURE child index "<<i.bx<<" unavailable\n";break;
        case 37:if(!(p.vararg&2)){o<<"-- VARARG used by a non-variadic prototype\n";o<<localReg(p,i.a,i.pc)<<" = nil\n";}else if(i.b==0)o<<localReg(p,i.a,i.pc)<<" = ...\n";else{o<<localReg(p,i.a,i.pc);for(int k=1;k<i.b-1;k++)o<<", "<<localReg(p,i.a+k,i.pc);o<<" = ...\n";}break;
        default:o<<"-- unsupported opcode retained: "<<opName(i.op)<<" A="<<i.a<<" B="<<i.b<<" C="<<i.c<<"\n";break;
        }
    }
}

static bool isCondition(int op){ return op==23 || op==24 || op==25 || op==26; }
static std::string conditionExpr(const Proto& p,const Instr& i,const RenderContext& context){
    if(i.op==26){
        std::string test=renderedLocal(p,i.a,i.pc,context);
        return i.c ? "not ("+test+")" : test;
    }
    std::string lhs=renderedValue(p,i.b,i.pc,context), rhs=renderedValue(p,i.c,i.pc,context);
    const char* op=i.op==23 ? " == " : i.op==24 ? " < " : " <= ";
    std::string comparison="("+lhs+op+rhs+")";
    return i.a ? comparison : "not "+comparison;
}
static RenderContext closureContext(const Proto& p,const Instr& closure,const RenderContext& parent){
    RenderContext result;
    result.capturedUpvalues.reserve(closure.captures.size());
    for(const CaptureInfo& capture:closure.captures){
        result.capturedUpvalues.push_back(capture.fromUpvalue
            ? renderedUpvalue(p,capture.sourceIndex,parent)
            : renderedLocal(p,capture.sourceIndex,capture.bindingPc,parent));
    }
    return result;
}
static void emitRange(std::ostringstream& o,const Proto& p,int begin,int end,int indent,const RenderContext& context,bool allowInfiniteLoop=true);
static void emitSimple(std::ostringstream& o,const Proto& p,const Instr& i,int indent,const RenderContext& context){
    std::string pad(indent,' ');
    if(i.closureBindingFor>=0) return;
    switch(i.op){
    case 0:o<<pad<<renderedLocal(p,i.a,i.pc,context)<<" = "<<renderedLocal(p,i.b,i.pc,context)<<"\n";break;
    case 1:o<<pad<<renderedLocal(p,i.a,i.pc,context)<<" = "<<(size_t(i.bx)<p.constants.size()?p.constants[i.bx]:"nil")<<"\n";break;
    case 2:o<<pad<<renderedLocal(p,i.a,i.pc,context)<<" = "<<(i.b?"true":"false")<<"\n";break;
    case 3:for(int r=i.a;r<=i.b;r++)o<<pad<<renderedLocal(p,r,i.pc,context)<<" = nil\n";break;
    case 4:o<<pad<<renderedLocal(p,i.a,i.pc,context)<<" = "<<renderedUpvalue(p,i.b,context)<<"\n";break;
    case 5:o<<pad<<renderedLocal(p,i.a,i.pc,context)<<" = _G["<<(size_t(i.bx)<p.constants.size()?p.constants[i.bx]:"nil")<<"]\n";break;
    case 6:o<<pad<<renderedLocal(p,i.a,i.pc,context)<<" = "<<renderedLocal(p,i.b,i.pc,context)<<"["<<renderedValue(p,i.c,i.pc,context)<<"]\n";break;
    case 7:o<<pad<<"_G["<<(size_t(i.bx)<p.constants.size()?p.constants[i.bx]:"nil")<<"] = "<<renderedLocal(p,i.a,i.pc,context)<<"\n";break;
    case 8:o<<pad<<renderedUpvalue(p,i.b,context)<<" = "<<renderedLocal(p,i.a,i.pc,context)<<"\n";break;
    case 9:o<<pad<<renderedLocal(p,i.a,i.pc,context)<<"["<<renderedValue(p,i.b,i.pc,context)<<"] = "<<renderedValue(p,i.c,i.pc,context)<<"\n";break;
    case 10:o<<pad<<renderedLocal(p,i.a,i.pc,context)<<" = {}\n";break;
    case 11:o<<pad<<renderedLocal(p,i.a+1,i.pc,context)<<" = "<<renderedLocal(p,i.b,i.pc,context)<<"\n";
            o<<pad<<renderedLocal(p,i.a,i.pc,context)<<" = "<<renderedLocal(p,i.b,i.pc,context)<<"["<<renderedValue(p,i.c,i.pc,context)<<"]\n";break;
    case 12: case 13: case 14: case 15: case 16: case 17:{const char* op=i.op==12?"+":i.op==13?"-":i.op==14?"*":i.op==15?"/":i.op==16?"%":"^";o<<pad<<renderedLocal(p,i.a,i.pc,context)<<" = "<<renderedValue(p,i.b,i.pc,context)<<" "<<op<<" "<<renderedValue(p,i.c,i.pc,context)<<"\n";break;}
    case 18:o<<pad<<renderedLocal(p,i.a,i.pc,context)<<" = -"<<renderedLocal(p,i.b,i.pc,context)<<"\n";break;
    case 19:o<<pad<<renderedLocal(p,i.a,i.pc,context)<<" = not "<<renderedLocal(p,i.b,i.pc,context)<<"\n";break;
    case 20:o<<pad<<renderedLocal(p,i.a,i.pc,context)<<" = #"<<renderedLocal(p,i.b,i.pc,context)<<"\n";break;
    case 21:o<<pad<<renderedLocal(p,i.a,i.pc,context)<<" = "<<renderedLocal(p,i.b,i.pc,context)<<" .. "<<renderedLocal(p,i.c,i.pc,context)<<"\n";break;
    case 27:o<<pad<<"-- ByteVeil: TESTSET at pc "<<i.pc<<" conditionally assigns "<<renderedLocal(p,i.a,i.pc,context)<<" from "<<renderedLocal(p,i.b,i.pc,context)<<" (C="<<i.c<<")\n";break;
    case 28:{
        if(i.b==0||i.c==0)o<<pad<<"-- ByteVeil: CALL at pc "<<i.pc<<" has open "<<(i.b==0?"arguments":"results")<<"\n";
        if(i.c==1)o<<pad<<renderedLocal(p,i.a,i.pc,context)<<"("<<renderedArgs(p,i.a,i.b,i.pc,context)<<")\n";
        else {
            int results=i.c==0?1:i.c-1;
            o<<pad;
            for(int r=0;r<results;r++){if(r)o<<", ";o<<renderedLocal(p,i.a+r,i.pc,context);}
            o<<" = "<<renderedLocal(p,i.a,i.pc,context)<<"("<<renderedArgs(p,i.a,i.b,i.pc,context)<<")\n";
        }
        break;
    }
    case 29:if(i.b==0)o<<pad<<"-- ByteVeil: TAILCALL at pc "<<i.pc<<" has open arguments\n";o<<pad<<"return "<<renderedValueAt(p,i.a,i.pc,context)<<"("<<renderedArgs(p,i.a,i.b,i.pc,context)<<")\n";break;
    case 30:{if(i.b==0)o<<pad<<"-- ByteVeil: RETURN at pc "<<i.pc<<" has an open result tail\n";int n=i.b==0?1:i.b-1;if(n==0){o<<pad<<"return\n";break;}o<<pad<<"return ";for(int k=0;k<n;k++){if(k)o<<", ";o<<renderedValueAt(p,i.a+k,i.pc,context);}if(i.b==0&&(p.vararg&2))o<<", ...";o<<"\n";break;}
    case 31:o<<pad<<"-- ByteVeil: FORLOOP at pc "<<i.pc<<" was not paired with FORPREP\n";break;
    case 32:o<<pad<<"-- ByteVeil: FORPREP at pc "<<i.pc<<" was not paired with FORLOOP\n";break;
    case 33:o<<pad<<"-- ByteVeil: TFORLOOP at pc "<<i.pc<<" was not paired with its entry jump\n";break;
    case 34: if(i.b>0){int base=(i.setlistBlock-1)*50;for(int k=1;k<=i.b;k++)o<<pad<<renderedLocal(p,i.a,i.pc,context)<<"["<<base+k<<"] = "<<renderedLocal(p,i.a+k,i.pc,context)<<"\n";}else o<<pad<<"-- ByteVeil: SETLIST at pc "<<i.pc<<" has an open value tail\n";break;
    case 35:o<<pad<<"-- ByteVeil: CLOSE registers >= "<<i.a<<"; captured upvalues remain represented\n";break;
    case 36:
        if(size_t(i.bx)<p.children.size()){
            const Proto& child=p.children[i.bx];
            const RenderContext childContext=closureContext(p,i,context);
            for(size_t slot=0;slot<i.captures.size();++slot){
                const CaptureInfo& capture=i.captures[slot];
                o<<pad<<"-- ByteVeil: CLOSURE pc "<<i.pc<<" captures upvalue "<<slot<<" from "
                 <<(capture.fromUpvalue?"parent upvalue ":"local register ")<<childContext.capturedUpvalues[slot]
                 <<" (binding pc "<<capture.bindingPc<<")\n";
            }
            o<<pad<<renderedLocal(p,i.a,i.pc,context)<<" = function(";
            for(int k=0;k<child.params;k++){if(k)o<<", ";o<<renderedLocal(child,k,0,childContext);}
            if(child.vararg&2){if(child.params)o<<", ";o<<"...";}
            o<<")\n";
            emitRegisterDeclarations(o,child,childContext,indent+4,child.params);
            emitRange(o,child,0,int(child.code.size()),indent+4,childContext);
            o<<pad<<"end\n";
        }else o<<pad<<"-- ByteVeil: CLOSURE child index "<<i.bx<<" unavailable\n";
        break;
    case 37:if(!(p.vararg&2)){o<<pad<<"-- ByteVeil: VARARG used by a non-variadic prototype\n";o<<pad<<renderedLocal(p,i.a,i.pc,context)<<" = nil\n";}else if(i.b==0)o<<pad<<renderedLocal(p,i.a,i.pc,context)<<" = ... -- open vararg results\n";else{o<<pad<<renderedLocal(p,i.a,i.pc,context);for(int k=1;k<i.b-1;k++)o<<", "<<renderedLocal(p,i.a+k,i.pc,context);o<<" = ...\n";}break;
    case -1:o<<pad<<"-- ByteVeil: SETLIST extra block word "<<i.setlistBlock<<" consumed at pc "<<i.pc<<"\n";break;
    default:o<<pad<<"-- ByteVeil: unsupported opcode retained: "<<opName(i.op)<<" A="<<i.a<<" B="<<i.b<<" C="<<i.c<<" at pc "<<i.pc<<"\n";break;
    }
}
static void emitRange(std::ostringstream& o,const Proto& p,int begin,int end,int indent,const RenderContext& context,bool allowInfiniteLoop){
    const std::string pad(indent,' ');
    // A readable reconstruction must never follow an unstructured back-edge
    // forever.  Structured loops below consume their back-edge; anything that
    // remains is reported in the output instead of making the decompiler hang.
    std::vector<unsigned int> visits(p.code.size(),0);
    for(int pc=begin;pc<end;){
        if(pc<begin){
            o<<pad<<"-- ByteVeil: function "<<p.id<<" left reconstructed range at pc "<<pc<<"\n";
            return;
        }
        if(++visits[size_t(pc)]>1){
            o<<pad<<"-- ByteVeil: function "<<p.id<<" stopped at repeated control-flow pc "<<pc<<" (unstructured cycle)\n";
            return;
        }
        const Instr& i=p.code[pc];
        // These records are consumed by the preceding CLOSURE.  Re-emitting
        // them as ordinary MOVE/GETUPVAL instructions would fabricate writes
        // that the Lua 5.1 VM never performs.
        if(i.closureBindingFor>=0){ ++pc; continue; }
        // A closed, entry-anchored back-edge with no jump out of its body is
        // an unconditional source loop.  Treat it structurally; cycles that
        // have an exit or competing latch are deliberately left visible.
        if(allowInfiniteLoop && pc==begin){
            int latch=-1;
            bool closed=true;
            for(int scan=begin+1;scan<end;scan++){
                const Instr& edge=p.code[scan];
                if(edge.op==22 && edge.target==begin){
                    if(latch>=0){closed=false;break;}
                    latch=scan;
                }
            }
            if(latch>=0&&closed){
                for(int scan=begin;scan<latch;scan++){
                    const Instr& edge=p.code[scan];
                    if(edge.target>=0&&(edge.target>=latch+1||edge.target<begin)){closed=false;break;}
                }
            }
            if(latch>=0&&closed){
                o<<pad<<"while true do\n";
                emitRange(o,p,begin,latch,indent+4,context,false);
                o<<pad<<"end\n";
                pc=latch+1;
                continue;
            }
        }
        // GETGLOBAL/MOVE/CALL (or TAILCALL) is the common compiler shape for
        // a direct global call. Emit the source-level call and consume the
        // temporary function/argument registers.
        if(i.op==5 && pc+2<end && p.code[pc+1].op==0 &&
           (p.code[pc+2].op==28 || p.code[pc+2].op==29) &&
           p.code[pc+2].a==i.a){
            const Instr& call=p.code[pc+2];
            std::string fn=i.bx<int(p.constants.size())?p.constants[i.bx]:"_G[\"unknown\"]";
            if(fn.size()>=2 && fn.front()=='"' && fn.back()=='"') fn=fn.substr(1,fn.size()-2);
            std::string arg=renderedValueAt(p,p.code[pc+1].b,pc+1,context);
            o<<pad<<(call.op==29?"return ":"")<<fn<<"("<<arg<<")\n";
            pc+=3; if(call.op==29) break; continue;
        }
        // Numeric for: FORPREP jumps over the body to FORLOOP; the loop variable is A+3.
        if(i.op==32 && i.target>pc && i.target<end && i.target<int(p.code.size()) && p.code[i.target].op==31){
            int bodyBegin=pc+1, bodyEnd=i.target;
            std::string var=renderedLocal(p,i.a+3,bodyBegin,context);
            o<<pad<<"for "<<var<<" = "<<renderedValueAt(p,i.a,pc,context)<<", "<<renderedValueAt(p,i.a+1,pc,context)<<", "<<renderedValueAt(p,i.a+2,pc,context)<<" do\n";
            emitRange(o,p,bodyBegin,bodyEnd,indent+4,context); o<<pad<<"end\n"; pc=i.target+1; continue;
        }
        // Generic for: the initial JMP lands on TFORLOOP.  TFORLOOP skips the
        // following backward JMP on exhaustion; otherwise that JMP enters the
        // body.  Reconstruct the iterator triple and all C loop variables.
        if(i.op==22 && i.target>pc && i.target+1<end &&
           p.code[i.target].op==33 && p.code[i.target+1].op==22 &&
           p.code[i.target+1].target==pc+1){
            const Instr& loop=p.code[i.target];
            o<<pad<<"for ";
            for(int n=0;n<loop.c;n++){
                if(n)o<<", ";
                o<<renderedLocal(p,loop.a+3+n,pc+1,context);
            }
            o<<" in "<<renderedLocal(p,loop.a,pc,context)<<", "<<renderedLocal(p,loop.a+1,pc,context)<<", "<<renderedLocal(p,loop.a+2,pc,context)<<" do\n";
            emitRange(o,p,pc+1,i.target,indent+4,context);
            o<<pad<<"end\n";
            pc=i.target+2;
            continue;
        }
        // Repeat/until: a conditional immediately followed by a backward jump.
        if(isCondition(i.op) && pc+1<end && p.code[pc+1].op==22 && p.code[pc+1].target>=begin && p.code[pc+1].target<pc){
            o<<pad<<"repeat\n"; emitRange(o,p,p.code[pc+1].target,pc,indent+4,context); o<<pad<<"until "<<conditionExpr(p,i,context)<<"\n"; pc+=2; continue;
        }
        // While: condition, forward exit jump, body, backward jump to condition.
        if(isCondition(i.op) && pc+1<end && p.code[pc+1].op==22 && p.code[pc+1].target>pc+1){
            int bodyBegin=pc+2, exit=p.code[pc+1].target;
            if(exit<=end && exit>bodyBegin && p.code[exit-1].op==22 && p.code[exit-1].target==pc){
                o<<pad<<"while "<<conditionExpr(p,i,context)<<" do\n"; emitRange(o,p,bodyBegin,exit-1,indent+4,context); o<<pad<<"end\n"; pc=exit; continue;
            }
        }
        // If/elseif/else: conditional, jump over true branch, optional join jump.
        if(isCondition(i.op) && pc+1<end && p.code[pc+1].op==22 && p.code[pc+1].target>pc+1){
            int trueBegin=pc+2, falseBegin=p.code[pc+1].target;
            int join=falseBegin;
            if(falseBegin-1>=trueBegin && p.code[falseBegin-1].op==22 && p.code[falseBegin-1].target>falseBegin){ join=p.code[falseBegin-1].target; }
            o<<pad<<"if "<<conditionExpr(p,i,context)<<" then\n"; emitRange(o,p,trueBegin,(falseBegin-1>=trueBegin&&p.code[falseBegin-1].op==22?p.code[falseBegin-1].pc:falseBegin),indent+4,context);
            if(join> falseBegin){ o<<pad<<"else\n"; emitRange(o,p,falseBegin,join,indent+4,context); }
            o<<pad<<"end\n"; pc=join; continue;
        }
        if(i.op==22){ pc=i.target>=0?i.target:pc+1; continue; }
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
        if(!loopSetup && !returnSetup) emitSimple(o,p,i,indent,context);
        if(i.op==29 || i.op==30) break;
        if(i.op==2&&i.c){pc=i.target>=0?i.target:pc+2;continue;}
        pc++;
    }
}
static void readableProto(std::ostringstream& o,const Proto& p){
    o<<"-- ByteVeil Lua 5.1 lifted (reconstructed) function "<<p.id<<" (CFG/SSA conservative)\n";
    const RenderContext rootContext;
    emitRegisterDeclarations(o,p,rootContext,0,0);
    emitRange(o,p,0,int(p.code.size()),0,rootContext);
    if(p.id==0 && (p.code.empty()||p.code.back().op!=30)) o<<"return nil\n";
}

[[maybe_unused]] static void liftProto(std::ostringstream& o,const Proto& p){
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
std::string inspect(const std::string& data,const std::string& mode,std::string& error){try{Proto p=parse(data);std::ostringstream o;if(mode=="json"||mode=="ir"){o<<"{\"format\":\"Lua 5.1 bytecode\",\"version\":81,\"root_function\":";jsonProto(o,p);o<<"}\n";}else if(mode=="disassemble")disProto(o,p);else if(mode=="cfg")cfgProto(o,p);else if(mode=="structured")structuredProto(o,p);else if(mode=="constants"){std::function<void(const Proto&)> f=[&](const Proto&x){o<<"function "<<x.id<<" constants: "<<x.constantTable.size()<<"\n";for(size_t i=0;i<x.constantTable.size();i++){const ConstantInfo& constant=x.constantTable[i];o<<"  ["<<i<<"] "<<constant.type<<" value=\""<<jsonEscape(constant.value)<<"\" lua="<<constant.lua;if(!constant.stringBytesHex.empty())o<<" bytes="<<constant.stringBytesHex;o<<"\n";}for(const auto&c:x.children)f(c);};f(p);}else if(mode=="prototypes"){std::function<void(const Proto&)> f=[&](const Proto&x){o<<"function "<<x.id<<" parent="<<x.parent<<" instructions="<<x.code.size()<<" children="<<x.children.size()<<"\n";for(const auto&c:x.children)f(c);};f(p);}else readableProto(o,p);return o.str();}catch(const std::exception&e){error=e.what();return {};}}
}
