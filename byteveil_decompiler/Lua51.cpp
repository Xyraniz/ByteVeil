#include "Lua51.h"
#include <algorithm>
#include <cstdint>
#include <cstring>
#include <functional>
#include <iomanip>
#include <limits>
#include <map>
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
struct Proto { int id=0,parent=-1,linedefined=0,lastline=0,nups=0,params=0,vararg=0,maxstack=0; std::string source; std::vector<int> lines; std::vector<LocalInfo> locals; std::vector<std::string> upvalues; std::vector<Instr> code; std::vector<std::string> constants; std::vector<ConstantInfo> constantTable; std::vector<Proto> children; std::vector<char> capturedRegisters,managedCapturedRegisters; };

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
    p.capturedRegisters.assign(size_t(p.maxstack),0);
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
            if(binding.op==0&&binding.b>=0&&binding.b<p.maxstack)
                p.capturedRegisters[size_t(binding.b)]=1;
        }
        // A valid binding cannot itself be CLOSURE, so no later closure is
        // skipped.  Advancing avoids treating the pseudo-instructions as
        // regular bytecode during this association pass.
        pc+=expected;
    }
    p.managedCapturedRegisters.assign(size_t(p.maxstack),0);
    for(const Instr& instruction:p.code){
        const int firstClosed=instruction.op==35?instruction.a:-1;
        if(firstClosed<0) continue;
        for(int registerIndex=firstClosed;registerIndex<p.maxstack;++registerIndex)
            if(p.capturedRegisters[size_t(registerIndex)])
                p.managedCapturedRegisters[size_t(registerIndex)]=1;
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
        case 22:checkReg(i,i.a,"A");checkTarget(i);break;
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
        else if((i.op==28||i.op==29||i.op==30)&&i.b==0){
            for(int prev=int(pc)-1;prev>=0;--prev)
                if(p.code[prev].op==28||p.code[prev].op==29||p.code[prev].op==37){i.openProducer=prev;break;}
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
    // Managed upvalues point at a one-slot cell so CLOSE can detach the
    // current register while older closures keep the cell they captured.
    std::vector<std::string> capturedUpvalueCells;
    bool stateMachine=false;
};

static bool managedCapturedLocal(const Proto& p,int registerIndex){
    return registerIndex>=0&&registerIndex<int(p.managedCapturedRegisters.size())&&
        p.managedCapturedRegisters[size_t(registerIndex)]!=0;
}
static std::string renderedRegisterName(const Proto& p,int registerIndex,int pc,const RenderContext& context){
    // Register scopes already isolate functions that have no lexical
    // upvalues.  Keep those dispatcher variables compact; functions with
    // captures retain prototype-qualified names so locals cannot shadow an
    // enclosing captured register.
    if(context.stateMachine&&p.nups==0) return reg(registerIndex);
    if(context.stateMachine) return "__byteveil_f"+std::to_string(p.id)+"_r"+std::to_string(registerIndex);
    // Root names remain compact for readable diagnostics.  Nested prototypes
    // need their own register namespace so an inner r0 cannot shadow an outer
    // r0 captured by a closure.
    if(p.id==0) return localReg(p,registerIndex,pc);
    if(p.nups==0) return reg(registerIndex);
    return "__byteveil_f"+std::to_string(p.id)+"_r"+std::to_string(registerIndex);
}
static std::string capturedCellName(const Proto& p,int registerIndex,const RenderContext& context){
    (void)context;
    std::string name="__byteveil_upcell_f"+std::to_string(p.id)+"_r"+std::to_string(registerIndex);
    auto conflicts=[&](const std::string& candidate){
        for(const LocalInfo& local:p.locals) if(validIdentifier(local.name)&&local.name==candidate) return true;
        return false;
    };
    while(conflicts(name)) name.push_back('_');
    return name;
}
static std::string renderedLocal(const Proto& p,int registerIndex,int pc,const RenderContext& context){
    const std::string name=renderedRegisterName(p,registerIndex,pc,context);
    return managedCapturedLocal(p,registerIndex)?capturedCellName(p,registerIndex,context)+"[1]":name;
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

static bool writesRegister(const Instr& instruction,int registerIndex,int maxstack){
    if(instruction.extraWord||instruction.closureBindingFor>=0) return false;
    auto contains=[&](int first,int last){return registerIndex>=first&&registerIndex<=last;};
    switch(instruction.op){
    case 0: case 1: case 2: case 4: case 5: case 6: case 10:
    case 12: case 13: case 14: case 15: case 16: case 17:
    case 18: case 19: case 20: case 21: case 36:
        return registerIndex==instruction.a;
    case 3:
        return contains(instruction.a,instruction.b);
    case 11:
        return registerIndex==instruction.a||registerIndex==instruction.a+1;
    case 27:
        return registerIndex==instruction.a;
    case 28:
        if(instruction.c==0) return registerIndex>=instruction.a&&registerIndex<maxstack;
        return instruction.c>1&&contains(instruction.a,instruction.a+instruction.c-2);
    case 31:
        return registerIndex==instruction.a||registerIndex==instruction.a+3;
    case 32:
        return registerIndex==instruction.a;
    case 33:
        return contains(instruction.a+3,instruction.a+2+instruction.c);
    case 37:
        if(instruction.b==0) return registerIndex>=instruction.a&&registerIndex<maxstack;
        return instruction.b>1&&contains(instruction.a,instruction.a+instruction.b-2);
    default:
        return false;
    }
}

static std::string renderedValueAt(const Proto& p,int registerIndex,int pc,const RenderContext& context){
    if(context.stateMachine) return renderedLocal(p,registerIndex,pc,context);
    for(int n=pc-1;n>=0;--n){
        const Instr& definition=p.code[n];
        if(definition.closureBindingFor>=0 || definition.a!=registerIndex) continue;
        // A forward edge that bypasses this definition can reach the current
        // use without assigning it.  In that case a local textual fold would
        // turn a path-dependent TESTSET/branch result into a false constant.
        // Keep the register reference until CFG-aware SSA reaches this path.
        for(const Instr& edge:p.code)
            if(edge.pc<n && edge.target>n && edge.target<=pc)
                return renderedLocal(p,registerIndex,pc,context);
        if(definition.op==1) return definition.bx<int(p.constants.size())?p.constants[definition.bx]:"nil";
        if(definition.op==0){
            bool sourceChanged=false;
            for(int between=n+1;between<pc;++between)
                if(writesRegister(p.code[between],definition.b,p.maxstack)){sourceChanged=true;break;}
            if(!sourceChanged) return renderedValueAt(p,definition.b,n,context);
            // MOVE stores the value at its own PC.  If the source register is
            // written before this use, keep the copied destination instead
            // of substituting the source's newer value.
            return renderedLocal(p,registerIndex,pc,context);
        }
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

static bool openResultProducer(const Instr& instruction){
    return (instruction.op==28&&instruction.c==0)||(instruction.op==37&&instruction.b==0);
}
static bool hasControlEntryAt(const Proto& p,int targetPc);
static int openProducerFor(const Proto& p,const Instr& consumer){
    if(consumer.openProducer<0||consumer.openProducer>=consumer.pc||consumer.openProducer+1!=consumer.pc)
        return -1;
    if(hasControlEntryAt(p,consumer.pc)) return -1;
    const Instr& producer=p.code[size_t(consumer.openProducer)];
    return openResultProducer(producer)?consumer.openProducer:-1;
}
static std::string openProducerExpression(const Proto& p,int producerPc,const RenderContext& context,int depth);
static std::string renderedOpenArgs(const Proto& p,const Instr& consumer,const RenderContext& context,int depth){
    if(depth>32) return {};
    const int producerPc=openProducerFor(p,consumer);
    if(producerPc<0||p.code[size_t(producerPc)].a<consumer.a+1) return {};
    std::ostringstream result;
    for(int regIndex=consumer.a+1;regIndex<p.code[size_t(producerPc)].a;regIndex++){
        if(regIndex>consumer.a+1) result<<", ";
        result<<renderedValueAt(p,regIndex,consumer.pc,context);
    }
    const std::string tail=openProducerExpression(p,producerPc,context,depth+1);
    if(tail.empty()) return {};
    if(p.code[size_t(producerPc)].a>consumer.a+1) result<<", ";
    result<<tail;
    return result.str();
}
static bool colonMethodCall(const Proto& p,int selfPc,int callPc);
static bool hasControlEntryAt(const Proto& p,int targetPc);
static std::string colonMethodTarget(const Proto& p,const Instr& call,const RenderContext& context);
static std::string openProducerExpression(const Proto& p,int producerPc,const RenderContext& context,int depth){
    if(depth>32||producerPc<0||producerPc>=int(p.code.size())) return {};
    const Instr& producer=p.code[size_t(producerPc)];
    if(producer.op==37&&producer.b==0) return "...";
    if(producer.op!=28||producer.c!=0) return {};
    const bool colonCall=producerPc>0&&colonMethodCall(p,producerPc-1,producerPc)&&
        !hasControlEntryAt(p,producerPc);
    const std::string fn=colonCall?colonMethodTarget(p,producer,context):renderedValueAt(p,producer.a,producer.pc,context);
    const std::string callArgs=colonCall?std::string():producer.b==0
        ?renderedOpenArgs(p,producer,context,depth+1)
        :renderedArgs(p,producer.a,producer.b,producer.pc,context);
    if(producer.b==0&&callArgs.empty()) return {};
    return fn+"("+callArgs+")";
}
static bool openProducerConsumedInRange(const Proto& p,int producerPc,int end){
    if(producerPc<0||producerPc+1>=end||producerPc+1>=int(p.code.size())) return false;
    const Instr& producer=p.code[size_t(producerPc)];
    const Instr& consumer=p.code[size_t(producerPc+1)];
    if(!openResultProducer(producer)||openProducerFor(p,consumer)!=producerPc) return false;
    return ((consumer.op==28||consumer.op==29)&&consumer.b==0)||
           (consumer.op==30&&consumer.b==0)||(consumer.op==34&&consumer.b==0);
}
static bool colonMethodCall(const Proto& p,int selfPc,int callPc){
    if(selfPc<0||callPc!=selfPc+1||callPc>=int(p.code.size())) return false;
    const Instr& self=p.code[size_t(selfPc)];
    const Instr& call=p.code[size_t(callPc)];
    if(self.op!=11||(call.op!=28&&call.op!=29)||call.a!=self.a||call.b!=2||!(self.c&256)) return false;
    const int keyIndex=self.c&255;
    return keyIndex<int(p.constantTable.size())&&p.constantTable[size_t(keyIndex)].type=="string"&&
           validIdentifier(p.constantTable[size_t(keyIndex)].value);
}
static bool hasControlEntryAt(const Proto& p,int targetPc){
    for(const Instr& instruction:p.code){
        if((instruction.op==22||instruction.op==31||instruction.op==32)&&instruction.target==targetPc)
            return true;
        if((instruction.op>=23&&instruction.op<=27)||instruction.op==33){
            const int jumpPc=instruction.pc+1;
            if(jumpPc>=0&&jumpPc<int(p.code.size())&&p.code[size_t(jumpPc)].op==22&&
               p.code[size_t(jumpPc)].target==targetPc) return true;
        }
        if(instruction.op==2&&instruction.c&&instruction.target==targetPc) return true;
    }
    return false;
}
static std::string colonMethodTarget(const Proto& p,const Instr& call,const RenderContext& context){
    const Instr& self=p.code[size_t(call.pc-1)];
    const int keyIndex=self.c&255;
    return renderedLocal(p,self.b,self.pc,context)+":"+p.constantTable[size_t(keyIndex)].value;
}
static std::string renderedOpenReturnValues(const Proto& p,const Instr& consumer,const RenderContext& context){
    const int producerPc=openProducerFor(p,consumer);
    if(producerPc<0||p.code[size_t(producerPc)].a<consumer.a) return {};
    std::ostringstream result;
    for(int regIndex=consumer.a;regIndex<p.code[size_t(producerPc)].a;regIndex++){
        if(regIndex>consumer.a) result<<", ";
        result<<renderedValueAt(p,regIndex,consumer.pc,context);
    }
    const std::string tail=openProducerExpression(p,producerPc,context,0);
    if(tail.empty()) return {};
    if(p.code[size_t(producerPc)].a>consumer.a) result<<", ";
    result<<tail;
    return result.str();
}

static void emitRegisterDeclarations(std::ostringstream& o,const Proto& p,const RenderContext& context,int indent,int firstRegister){
    std::set<std::string> names;
    for(int regIndex=firstRegister;regIndex<p.maxstack;++regIndex){
        if(managedCapturedLocal(p,regIndex)) continue;
        if(p.id==0){
            names.insert(renderedLocal(p,regIndex,0,context));
            for(const Instr& instruction:p.code)
                names.insert(renderedLocal(p,regIndex,instruction.pc,context));
        }else names.insert(renderedLocal(p,regIndex,0,context));
    }
    if(!names.empty()){
        o<<std::string(size_t(indent),' ')<<"local ";
        bool first=true;
        for(const std::string& name:names){ if(!first)o<<", "; o<<name; first=false; }
        o<<"\n";
    }
    for(int regIndex=0;regIndex<p.maxstack;++regIndex){
        if(!managedCapturedLocal(p,regIndex)) continue;
        o<<std::string(size_t(indent),' ')<<"local "<<capturedCellName(p,regIndex,context)<<" = {";
        if(regIndex<p.params) o<<renderedRegisterName(p,regIndex,0,context);
        else o<<"nil";
        o<<"}\n";
    }
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
    // The structured body is the fallthrough after the comparison skips its
    // following JMP. Lua 5.1 skips that JMP when the comparison differs from
    // A, so A=1 requires the negated expression here.
    return i.a ? "not "+comparison : comparison;
}
static std::string negateConditionExpr(const std::string& condition){
    if(condition.rfind("not (",0)==0&&condition.size()>6&&condition.back()==')')
        return condition.substr(5,condition.size()-6);
    return "not ("+condition+")";
}
static bool straightLineConditionGap(const Instr& instruction){
    if(instruction.op>=0&&instruction.op<=21) return true;
    if(instruction.op==28) return instruction.b>0&&instruction.c>0;
    if(instruction.op==34) return instruction.b>0;
    if(instruction.op==35||instruction.op==36) return true;
    if(instruction.op==37) return instruction.b>0;
    return false;
}
static bool separatedConditionGap(const Instr& instruction){
    if(instruction.op==2&&instruction.c!=0) return false;
    return straightLineConditionGap(instruction)||instruction.op==28;
}
static std::string conditionChainFlagName(const Proto& p,int pc,const RenderContext& context){
    std::set<std::string> names;
    for(const LocalInfo& local:p.locals) if(validIdentifier(local.name)) names.insert(local.name);
    for(const std::string& name:p.upvalues) if(validIdentifier(name)) names.insert(name);
    for(int registerIndex=0;registerIndex<p.maxstack;++registerIndex){
        names.insert(renderedLocal(p,registerIndex,0,context));
        for(const Instr& instruction:p.code)
            names.insert(renderedLocal(p,registerIndex,instruction.pc,context));
    }
    std::string name="__byteveil_condition_f"+std::to_string(p.id)+"_pc"+std::to_string(pc);
    while(names.count(name)) name.push_back('_');
    return name;
}
static std::string loopBreakFlagName(const Proto& p,int pc,const RenderContext& context){
    std::set<std::string> names;
    for(const LocalInfo& local:p.locals) if(validIdentifier(local.name)) names.insert(local.name);
    for(const std::string& name:p.upvalues) if(validIdentifier(name)) names.insert(name);
    for(int registerIndex=0;registerIndex<p.maxstack;++registerIndex){
        names.insert(renderedLocal(p,registerIndex,0,context));
        for(const Instr& instruction:p.code)
            names.insert(renderedLocal(p,registerIndex,instruction.pc,context));
    }
    std::string name="__byteveil_loopbreak_f"+std::to_string(p.id)+"_pc"+std::to_string(pc);
    while(names.count(name)) name.push_back('_');
    return name;
}
static std::string closureCaptureCellSource(const Proto& p,const CaptureInfo& capture,const RenderContext& parent){
    if(capture.fromUpvalue){
        if(capture.sourceIndex>=0&&capture.sourceIndex<int(parent.capturedUpvalueCells.size()))
            return parent.capturedUpvalueCells[size_t(capture.sourceIndex)];
        return {};
    }
    return managedCapturedLocal(p,capture.sourceIndex)?capturedCellName(p,capture.sourceIndex,parent):std::string();
}
static std::string closureCaptureParameterName(const Proto& child,int slot,const RenderContext& context){
    std::string name="__byteveil_capture_f"+std::to_string(child.id)+"_u"+std::to_string(slot);
    auto conflicts=[&](const std::string& candidate){
        for(const LocalInfo& local:child.locals) if(validIdentifier(local.name)&&local.name==candidate) return true;
        for(int regIndex=0;regIndex<child.maxstack;++regIndex){
            if(renderedRegisterName(child,regIndex,0,context)==candidate) return true;
            if(managedCapturedLocal(child,regIndex)&&capturedCellName(child,regIndex,context)==candidate) return true;
        }
        return false;
    };
    while(conflicts(name)) name.push_back('_');
    return name;
}
static bool emitCapturedCellClose(std::ostringstream& o,const Proto& p,int firstRegister,int indent,const RenderContext& context){
    bool detached=false;
    const std::string pad(size_t(indent),' ');
    for(int registerIndex=std::max(0,firstRegister);registerIndex<p.maxstack;++registerIndex){
        if(!managedCapturedLocal(p,registerIndex)) continue;
        const std::string cell=capturedCellName(p,registerIndex,context);
        o<<pad<<cell<<" = {"<<cell<<"[1]}\n";
        detached=true;
    }
    return detached;
}
static RenderContext closureContext(const Proto& p,const Instr& closure,const RenderContext& parent,const std::vector<std::string>& captureCellParameters){
    RenderContext result;
    result.capturedUpvalues.reserve(closure.captures.size());
    result.capturedUpvalueCells.reserve(closure.captures.size());
    for(size_t slot=0;slot<closure.captures.size();++slot){
        const CaptureInfo& capture=closure.captures[slot];
        if(slot<captureCellParameters.size()&&!captureCellParameters[slot].empty()){
            result.capturedUpvalues.push_back(captureCellParameters[slot]+"[1]");
            result.capturedUpvalueCells.push_back(captureCellParameters[slot]);
        }else{
            result.capturedUpvalues.push_back(capture.fromUpvalue
                ? renderedUpvalue(p,capture.sourceIndex,parent)
                : renderedLocal(p,capture.sourceIndex,capture.bindingPc,parent));
            result.capturedUpvalueCells.emplace_back();
        }
    }
    return result;
}
static void emitReadableBody(std::ostringstream& o,const Proto& p,RenderContext& context,int indent,int firstRegister);
static bool hasLoopEdgeTo(const Proto& p,int begin,int end,int target){
    for(int pc=begin;pc<end;++pc){
        const Instr& instruction=p.code[size_t(pc)];
        if(instruction.op==22&&instruction.target==target) return true;
        if((isCondition(instruction.op)||instruction.op==27)&&pc+1<end&&
           p.code[size_t(pc+1)].op==22&&p.code[size_t(pc+1)].target==target) return true;
    }
    return false;
}
static void emitRange(std::ostringstream& o,const Proto& p,int begin,int end,int indent,const RenderContext& context,bool allowInfiniteLoop=true,int loopBreakTarget=-1,int loopContinueTarget=-1,const std::string& loopBreakFlag=std::string());
static int followingControlTarget(const Proto& p,const Instr& i);
static bool emitOpenSetList(std::ostringstream& o,const Proto& p,const Instr& setlist,int indent,const RenderContext& context){
    const int producerPc=openProducerFor(p,setlist);
    if(producerPc<0) return false;
    const Instr& producer=p.code[size_t(producerPc)];
    const std::string tail=openProducerExpression(p,producerPc,context,0);
    if(tail.empty()) return false;
    const int firstRegister=setlist.a+1;
    const int prefixEnd=std::max(firstRegister,producer.a);
    const int skipped=std::max(0,firstRegister-producer.a);
    const int base=(setlist.setlistBlock-1)*50;
    const std::string pad(size_t(indent),' ');
    o<<pad<<"(function(__table, __base, __skip, ...)\n"
     <<pad<<"    local __values = { n = _G[\"select\"](\"#\", ...), ... }\n"
     <<pad<<"    for __index = __skip + 1, __values.n do\n"
     <<pad<<"        __table[__base + __index - __skip] = __values[__index]\n"
     <<pad<<"    end\n"
     <<pad<<"end)("<<renderedLocal(p,setlist.a,setlist.pc,context)<<", "<<base<<", "<<skipped;
    for(int regIndex=firstRegister;regIndex<prefixEnd;regIndex++)
        o<<", "<<renderedValueAt(p,regIndex,setlist.pc,context);
    o<<", "<<tail<<")\n";
    return true;
}
static void emitSimple(std::ostringstream& o,const Proto& p,const Instr& i,int indent,const RenderContext& context,bool colonCall=false){
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
        const std::string callArgs=colonCall?std::string():i.b==0?renderedOpenArgs(p,i,context,0):renderedArgs(p,i.a,i.b,i.pc,context);
        const std::string target=colonCall?colonMethodTarget(p,i,context):renderedLocal(p,i.a,i.pc,context);
        if(i.b==0&&callArgs.empty())o<<pad<<"-- ByteVeil: CALL at pc "<<i.pc<<" has unresolved open arguments\n";
        if(i.c==0)o<<pad<<"-- ByteVeil: CALL at pc "<<i.pc<<" has open results not consumed by a supported open operation\n";
        if(i.c==1)o<<pad<<target<<"("<<callArgs<<")\n";
        else {
            int results=i.c==0?1:i.c-1;
            o<<pad;
            for(int r=0;r<results;r++){if(r)o<<", ";o<<renderedLocal(p,i.a+r,i.pc,context);}
            o<<" = "<<target<<"("<<callArgs<<")\n";
        }
        break;
    }
    case 29:{
        const std::string callArgs=colonCall?std::string():i.b==0?renderedOpenArgs(p,i,context,0):renderedArgs(p,i.a,i.b,i.pc,context);
        if(i.b==0&&callArgs.empty())o<<pad<<"-- ByteVeil: TAILCALL at pc "<<i.pc<<" has unresolved open arguments\n";
        const std::string target=colonCall?colonMethodTarget(p,i,context):renderedValueAt(p,i.a,i.pc,context);
        o<<pad<<"return "<<target<<"("<<callArgs<<")\n";break;
    }
    case 30:{
        if(i.b==0){
            const std::string values=renderedOpenReturnValues(p,i,context);
            if(!values.empty()){o<<pad<<"return "<<values<<"\n";break;}
            o<<pad<<"-- ByteVeil: RETURN at pc "<<i.pc<<" has an unresolved open result tail\n";
        }
        int n=i.b==0?1:i.b-1;if(n==0){o<<pad<<"return\n";break;}o<<pad<<"return ";for(int k=0;k<n;k++){if(k)o<<", ";o<<renderedValueAt(p,i.a+k,i.pc,context);}if(i.b==0&&(p.vararg&2))o<<", ...";o<<"\n";break;
    }
    case 31:o<<pad<<"-- ByteVeil: FORLOOP at pc "<<i.pc<<" was not paired with FORPREP\n";break;
    case 32:o<<pad<<"-- ByteVeil: FORPREP at pc "<<i.pc<<" was not paired with FORLOOP\n";break;
    case 33:o<<pad<<"-- ByteVeil: TFORLOOP at pc "<<i.pc<<" was not paired with its entry jump\n";break;
    case 34: if(i.b>0){int base=(i.setlistBlock-1)*50;for(int k=1;k<=i.b;k++)o<<pad<<renderedLocal(p,i.a,i.pc,context)<<"["<<base+k<<"] = "<<renderedLocal(p,i.a+k,i.pc,context)<<"\n";}else if(!emitOpenSetList(o,p,i,indent,context))o<<pad<<"-- ByteVeil: SETLIST at pc "<<i.pc<<" has an open value tail\n";break;
    case 35:{
        const bool detached=emitCapturedCellClose(o,p,i.a,indent,context);
        if(!detached)o<<pad<<"-- ByteVeil: CLOSE registers >= "<<i.a<<"; no captured local cell was found\n";
        break;
    }
    case 36:
        if(size_t(i.bx)<p.children.size()){
            const Proto& child=p.children[i.bx];
            std::vector<std::string> captureCellSources(i.captures.size());
            std::vector<std::string> captureCellParameters(i.captures.size());
            for(size_t slot=0;slot<i.captures.size();++slot){
                captureCellSources[slot]=closureCaptureCellSource(p,i.captures[slot],context);
                if(!captureCellSources[slot].empty())
                    captureCellParameters[slot]=closureCaptureParameterName(child,int(slot),context);
            }
            const bool wrapCaptures=std::any_of(captureCellParameters.begin(),captureCellParameters.end(),
                [](const std::string& name){return !name.empty();});
            RenderContext childContext=closureContext(p,i,context,captureCellParameters);
            std::ostringstream childBody;
            emitReadableBody(childBody,child,childContext,indent+(wrapCaptures?8:4),child.params);
            o<<pad<<renderedLocal(p,i.a,i.pc,context)<<" = ";
            if(wrapCaptures){
                o<<"(function(";
                bool first=true;
                for(const std::string& parameter:captureCellParameters) if(!parameter.empty()){
                    if(!first)o<<", ";o<<parameter;first=false;
                }
                o<<")\n"<<pad<<"    return function(";
            }else o<<"function(";
            for(int k=0;k<child.params;k++){if(k)o<<", ";o<<renderedRegisterName(child,k,0,childContext);}
            if(child.vararg&2){if(child.params)o<<", ";o<<"...";}
            o<<")\n";
            o<<childBody.str();
            if(wrapCaptures){
                o<<pad<<"    end\n"<<pad<<"end)(";
                bool first=true;
                for(size_t slot=0;slot<captureCellParameters.size();++slot) if(!captureCellParameters[slot].empty()){
                    if(!first)o<<", ";o<<captureCellSources[slot];first=false;
                }
                o<<")\n";
            }else o<<pad<<"end\n";
        }else o<<pad<<"-- ByteVeil: CLOSURE child index "<<i.bx<<" unavailable\n";
        break;
    case 37:if(!(p.vararg&2)){o<<pad<<"-- ByteVeil: VARARG used by a non-variadic prototype\n"<<pad<<renderedLocal(p,i.a,i.pc,context)<<" = nil\n";}else if(i.b==0)o<<pad<<"-- ByteVeil: VARARG at pc "<<i.pc<<" has open results not consumed by a supported open operation\n";else{o<<pad<<renderedLocal(p,i.a,i.pc,context);for(int k=1;k<i.b-1;k++)o<<", "<<renderedLocal(p,i.a+k,i.pc,context);o<<" = ...\n";}break;
    case -1:o<<pad<<"-- ByteVeil: SETLIST extra block word "<<i.setlistBlock<<" consumed at pc "<<i.pc<<"\n";break;
    default:o<<pad<<"-- ByteVeil: unsupported opcode retained: "<<opName(i.op)<<" A="<<i.a<<" B="<<i.b<<" C="<<i.c<<" at pc "<<i.pc<<"\n";break;
    }
}
static void emitRange(std::ostringstream& o,const Proto& p,int begin,int end,int indent,const RenderContext& context,bool allowInfiniteLoop,int loopBreakTarget,int loopContinueTarget,const std::string& loopBreakFlag){
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
        // An unconditional jump to its own instruction never observes the
        // following bytecode. Preserve that behavior as an empty infinite loop
        // instead of rejecting it as a repeated, unstructured PC.
        if(i.op==22&&i.target==pc){
            o<<pad<<"while true do end\n";
            return;
        }
        // A `repeat` body starts before its condition and backward jump, so
        // recognize the latch from the range entry before emitting the body
        // linearly. This also gives nested `break` jumps their loop exit PC.
        if(pc==begin){
            int repeatStart=-1;
            int repeatLatch=-1;
            bool uniqueLatch=true;
            for(int scan=begin;scan+1<end;scan++){
                const int target=p.code[scan+1].target;
                if(isCondition(p.code[scan].op)&&p.code[scan+1].op==22&&target>=begin&&target<scan){
                    if(repeatStart<0||target<repeatStart){repeatStart=target;repeatLatch=scan;uniqueLatch=true;}
                    else if(target==repeatStart)uniqueLatch=false;
                }
            }
            if(repeatStart>=0&&uniqueLatch){
                for(int edge=repeatStart+1;edge<end;++edge)
                    if(p.code[edge].op==22&&edge!=repeatLatch+1&&p.code[edge].target==repeatStart){
                        uniqueLatch=false;
                        break;
                    }
            }
            if(repeatStart>=0&&repeatLatch>=0&&uniqueLatch){
                bool containedPrefix=true;
                for(int prefix=begin;prefix<repeatStart;++prefix){
                    const Instr& prior=p.code[prefix];
                    int takenTarget=-1;
                    if(prior.op==22) takenTarget=prior.target;
                    else if(isCondition(prior.op)||prior.op==27||prior.op==33){
                        if(prefix+1>=int(p.code.size())){containedPrefix=false;break;}
                        const Instr& skipped=p.code[prefix+1];
                        takenTarget=skipped.pc+skipped.sbx+1;
                    }else if(prior.op==31||prior.op==32){
                        takenTarget=prior.target;
                    }
                    const bool skipsPastStart=(prior.op==2&&prior.c&&prefix+2>=repeatStart)||
                        (prior.op==34&&prior.c==0&&prefix+2>=repeatStart);
                    if(takenTarget>=repeatStart||skipsPastStart||prior.op==29||prior.op==30){
                        containedPrefix=false;
                        break;
                    }
                }
                if(containedPrefix){
                    if(repeatStart>begin)
                    emitRange(o,p,begin,repeatStart,indent,context,allowInfiniteLoop,loopBreakTarget,loopContinueTarget,loopBreakFlag);
                    o<<pad<<"repeat\n";
                    emitRange(o,p,repeatStart,repeatLatch,indent+4,context,false,repeatLatch+2);
                    o<<pad<<"until "<<conditionExpr(p,p.code[repeatLatch],context)<<"\n";
                    pc=repeatLatch+2;
                    continue;
                }
            }
        }
        // A loop can have several explicit edges back to its header. Accept
        // only regions whose edges stay inside the loop or share one exit,
        // then render the back-edges as natural flow in `while true`.
        if(allowInfiniteLoop){
            auto branchTarget=[&](const Instr& edge){
                if(edge.op==22||edge.op==31||edge.op==32) return edge.target;
                if((isCondition(edge.op)||edge.op==27)&&edge.pc+1<int(p.code.size())&&p.code[edge.pc+1].op==22)
                    return p.code[edge.pc+1].target;
                if(edge.op==33) return followingControlTarget(p,edge);
                return -1;
            };
            int backEdges=0;
            bool singleUnconditionalBackEdge=false;
            int lastBackEdge=pc;
            bool closed=true;
            for(int scan=pc;scan<end;++scan){
                if(p.code[scan].op==22&&scan>pc&&
                   (isCondition(p.code[scan-1].op)||p.code[scan-1].op==27||p.code[scan-1].op==33)) continue;
                const int target=branchTarget(p.code[scan]);
                if(target==pc&&scan>pc){
                    ++backEdges;
                    lastBackEdge=std::max(lastBackEdge,scan+(p.code[scan].op==22?0:1));
                    singleUnconditionalBackEdge=p.code[scan].op==22;
                }
                else if(target>=0&&target<scan) closed=false;
            }
            int loopEnd=end;
            if((backEdges>=2||(backEdges==1&&(pc==begin||singleUnconditionalBackEdge)))&&closed){
                for(int scan=pc;scan<end;++scan){
                    const int target=branchTarget(p.code[scan]);
                    if(target>lastBackEdge) loopEnd=std::min(loopEnd,target);
                }
                if(loopEnd>lastBackEdge){
                    for(int scan=pc;scan<loopEnd;++scan){
                        const int target=branchTarget(p.code[scan]);
                        if(target>=0&&target!=pc&&(target<=pc||target>loopEnd)){closed=false;break;}
                    }
                    if(closed){
                        for(const Instr& edge:p.code){
                            if(edge.pc>=pc&&edge.pc<loopEnd) continue;
                            const int target=branchTarget(edge);
                            if(target>pc&&target<loopEnd){closed=false;break;}
                        }
                    }
                    if(closed){
                        o<<pad<<"while true do\n";
                        emitRange(o,p,pc,loopEnd,indent+4,context,false,loopEnd,pc);
                        o<<pad<<"end\n";
                        pc=loopEnd;
                        continue;
                    }
                }
            }
        }
        // GETGLOBAL/MOVE/CALL is safe to collapse only for a one-argument
        // tail call or a call whose results are explicitly discarded. Calls
        // producing iterator triples and other results must keep their
        // destination registers.
        if(i.op==5 && pc+2<end && p.code[pc+1].op==0 &&
           (p.code[pc+2].op==28 || p.code[pc+2].op==29) &&
           p.code[pc+2].a==i.a && p.code[pc+1].a==p.code[pc+2].a+1 && p.code[pc+2].b==2 &&
           (p.code[pc+2].op==29 || p.code[pc+2].c==1)){
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
            const int loopRegister=i.a+3;
            std::string var=renderedRegisterName(p,loopRegister,bodyBegin,context);
            o<<pad<<"for "<<var<<" = "<<renderedValueAt(p,i.a,pc,context)<<", "<<renderedValueAt(p,i.a+1,pc,context)<<", "<<renderedValueAt(p,i.a+2,pc,context)<<" do\n";
            // Lua 5.1 closes captured loop registers before reuse. Give each
            // source iteration a fresh cell so closures retain that value.
            if(managedCapturedLocal(p,loopRegister))
                o<<std::string(size_t(indent+4),' ')<<capturedCellName(p,loopRegister,context)<<" = {"<<var<<"}\n";
            // Lua 5.1 may route a continue through CLOSE immediately before
            // FORLOOP so closures keep the current iteration's cell. The
            // emitted loop already gives captured variables fresh cells, so
            // let that edge exit the synthetic repeat wrapper at CLOSE.
            const int continueTarget=i.target>bodyBegin&&p.code[i.target-1].op==35?i.target-1:i.target;
            const bool needsContinueWrapper=hasLoopEdgeTo(p,bodyBegin,bodyEnd,continueTarget);
            const std::string breakFlag=needsContinueWrapper?loopBreakFlagName(p,i.pc,context):std::string();
            if(needsContinueWrapper)
                o<<std::string(size_t(indent+4),' ')<<"local "<<breakFlag<<" = false\n"
                 <<std::string(size_t(indent+4),' ')<<"repeat\n";
            emitRange(o,p,bodyBegin,bodyEnd,indent+(needsContinueWrapper?8:4),context,true,i.target+1,continueTarget,breakFlag);
            if(needsContinueWrapper)
                o<<std::string(size_t(indent+4),' ')<<"until true\n"
                 <<std::string(size_t(indent+4),' ')<<"if "<<breakFlag<<" then break end\n";
            o<<pad<<"end\n"; pc=i.target+1; continue;
        }
        // Generic for: the initial JMP lands on TFORLOOP.  TFORLOOP skips the
        // following backward JMP on exhaustion; otherwise that JMP enters the
        // body.  Reconstruct the iterator triple and all C loop variables.
        if(i.op==22 && i.target>pc && i.target+1<int(p.code.size()) &&
           p.code[i.target].op==33 && p.code[i.target+1].op==22 &&
           p.code[i.target+1].target==pc+1){
            const Instr& loop=p.code[i.target];
            o<<pad<<"for ";
            for(int n=0;n<loop.c;n++){
                if(n)o<<", ";
                o<<renderedRegisterName(p,loop.a+3+n,pc+1,context);
            }
            o<<" in "<<renderedLocal(p,loop.a,pc,context)<<", "<<renderedLocal(p,loop.a+1,pc,context)<<", "<<renderedLocal(p,loop.a+2,pc,context)<<" do\n";
            for(int n=0;n<loop.c;n++){
                const int loopRegister=loop.a+3+n;
                if(managedCapturedLocal(p,loopRegister)){
                    // Each generic-for result is a new captured register
                    // value in Lua 5.1, even though the source loop is lexical.
                    const std::string var=renderedRegisterName(p,loopRegister,pc+1,context);
                    o<<std::string(size_t(indent+4),' ')<<capturedCellName(p,loopRegister,context)<<" = {"<<var<<"}\n";
                }
            }
            const int bodyBegin=pc+1, bodyEnd=i.target;
            const int continueTarget=i.target>bodyBegin&&p.code[i.target-1].op==35?i.target-1:i.target;
            const bool needsContinueWrapper=hasLoopEdgeTo(p,bodyBegin,bodyEnd,continueTarget);
            const std::string breakFlag=needsContinueWrapper?loopBreakFlagName(p,i.pc,context):std::string();
            if(needsContinueWrapper)
                o<<std::string(size_t(indent+4),' ')<<"local "<<breakFlag<<" = false\n"
                 <<std::string(size_t(indent+4),' ')<<"repeat\n";
            emitRange(o,p,bodyBegin,bodyEnd,indent+(needsContinueWrapper?8:4),context,true,i.target+2,continueTarget,breakFlag);
            if(needsContinueWrapper)
                o<<std::string(size_t(indent+4),' ')<<"until true\n"
                 <<std::string(size_t(indent+4),' ')<<"if "<<breakFlag<<" then break end\n";
            o<<pad<<"end\n";
            pc=i.target+2;
            continue;
        }
        if(loopBreakTarget>=0){
            if(i.op==27 && pc+1<end && p.code[pc+1].op==22 && p.code[pc+1].target==loopBreakTarget){
                const std::string tested=renderedLocal(p,i.b,i.pc,context);
                const std::string condition=i.c ? tested : "not ("+tested+")";
                o<<pad<<"if "<<condition<<" then\n"
                 <<std::string(size_t(indent+4),' ')<<renderedLocal(p,i.a,i.pc,context)<<" = "<<tested<<"\n"
                 <<(loopBreakFlag.empty()?std::string():std::string(size_t(indent+4),' ')+loopBreakFlag+" = true\n")
                 <<std::string(size_t(indent+4),' ')<<"break\n"<<pad<<"end\n";
                pc+=2;
                continue;
            }
            if(isCondition(i.op) && pc+1<end && p.code[pc+1].op==22 && p.code[pc+1].target==loopBreakTarget){
                o<<pad<<"if "<<negateConditionExpr(conditionExpr(p,i,context))<<" then\n";
                if(!loopBreakFlag.empty()) o<<std::string(size_t(indent+4),' ')<<loopBreakFlag<<" = true\n";
                o<<std::string(size_t(indent+4),' ')<<"break\n"<<pad<<"end\n";
                pc+=2;
                continue;
            }
            if(isCondition(i.op) && pc+2<end && p.code[pc+1].op==22 && p.code[pc+1].target==pc+3 &&
               p.code[pc+2].op==22 && p.code[pc+2].target==loopBreakTarget){
                o<<pad<<"if "<<conditionExpr(p,i,context)<<" then\n";
                if(!loopBreakFlag.empty()) o<<std::string(size_t(indent+4),' ')<<loopBreakFlag<<" = true\n";
                o<<std::string(size_t(indent+4),' ')<<"break\n"<<pad<<"end\n";
                pc+=3;
                continue;
            }
        }
        if(loopContinueTarget>=0&&isCondition(i.op)&&pc+1<end&&p.code[pc+1].op==22&&
           p.code[pc+1].target==loopContinueTarget){
            o<<pad<<"if "<<conditionExpr(p,i,context)<<" then\n";
            if(pc+2<end)
                emitRange(o,p,pc+2,end,indent+4,context,false,loopBreakTarget,loopContinueTarget,loopBreakFlag);
            o<<pad<<(loopBreakFlag.empty()?"end\n":"else\n") ;
            if(!loopBreakFlag.empty()) o<<std::string(size_t(indent+4),' ')<<"break\n"<<pad<<"end\n";
            return;
        }
        if(loopContinueTarget>=0&&i.op==27&&pc+1<end&&p.code[pc+1].op==22&&
           p.code[pc+1].target==loopContinueTarget){
            const std::string tested=renderedLocal(p,i.b,i.pc,context);
            const std::string condition=i.c?tested:"not ("+tested+")";
            o<<pad<<"if "<<condition<<" then\n"
             <<std::string(size_t(indent+4),' ')<<renderedLocal(p,i.a,i.pc,context)<<" = "<<tested<<"\n";
            if(!loopBreakFlag.empty()) o<<std::string(size_t(indent+4),' ')<<"break\n";
            o<<pad<<"else\n";
            if(pc+2<end) emitRange(o,p,pc+2,end,indent+4,context,false,loopBreakTarget,loopContinueTarget,loopBreakFlag);
            o<<pad<<"end\n";
            return;
        }
        // Lua 5.1 lowers `a or b or ...` conditions to comparison/test and
        // JMP pairs. Earlier pairs jump to the shared true body; the final
        // pair jumps past it when false. Fold that shape before recursively
        // splitting the ranges, which otherwise mistakes the shared body for
        // a branch escaping its parent range.
        if(isCondition(i.op)){
            struct ConditionJump { int pc; int target; std::string fallthrough; };
            struct ConditionGap { int begin; int end; };
            std::vector<ConditionJump> chain;
            std::vector<ConditionGap> gaps;
            std::vector<ConditionJump> separatedChain;
            std::vector<ConditionGap> separatedGaps;
            int cursor=pc;
            while(cursor+1<end && isCondition(p.code[cursor].op) && p.code[cursor+1].op==22){
                const Instr& predicate=p.code[cursor];
                const Instr& jump=p.code[cursor+1];
                chain.push_back({cursor,jump.target,conditionExpr(p,predicate,context)});
                cursor+=2;
            }
            bool emittedChain=false;
            int guardedJoin=-1;
            // Some compilers leave short-circuit operand setup between each
            // TEST/JMP pair (for example SELF, LOADK, CALL). Keep those
            // instructions in the false path so calls remain lazy and ordered.
            // A repeated `if gate then value = load(); if value then goto join`
            // shape is another common short-circuit form. Each primary test jumps
            // over its payload and secondary test to the next candidate, while
            // each secondary test jumps to one shared join on success. Preserve
            // that order with a local success flag instead of flattening the
            // condition into an eager expression.
            struct GuardedConditionCandidate {
                int primaryPc;
                int prefixBegin;
                int payloadBegin;
                int secondaryPc;
                int nextBegin;
            };
            if(isCondition(i.op) && pc+1<end && p.code[pc+1].op==22){
                std::vector<GuardedConditionCandidate> guarded;
                int candidatePc=pc;
                int prefixBegin=pc;
                int join=-1;
                int fallbackBegin=-1;
                bool validGuardedChain=true;
                auto guardedGapSafe=[](const Instr& instruction){
                    return straightLineConditionGap(instruction)&&
                        !(instruction.op==2&&instruction.c!=0);
                };
                auto seekCondition=[&](int begin,int& found){
                    int cursor=begin;
                    while(cursor<end&&cursor-begin<16&&guardedGapSafe(p.code[cursor])) ++cursor;
                    if(cursor+1<end&&isCondition(p.code[cursor].op)&&p.code[cursor+1].op==22){
                        found=cursor;
                        return true;
                    }
                    found=-1;
                    return false;
                };
                for(size_t candidateCount=0;candidateCount<128;++candidateCount){
                    if(candidatePc+1>=end||!isCondition(p.code[candidatePc].op)||p.code[candidatePc+1].op!=22){
                        validGuardedChain=false;
                        break;
                    }
                    int secondaryPc=-1;
                    if(!seekCondition(candidatePc+2,secondaryPc)){
                        validGuardedChain=false;
                        break;
                    }
                    const int nextBegin=secondaryPc+2;
                    const int primaryTarget=p.code[candidatePc+1].target;
                    const int secondaryTarget=p.code[secondaryPc+1].target;
                    if(primaryTarget!=nextBegin||secondaryTarget<=nextBegin||secondaryTarget>end||
                       (join>=0&&join!=secondaryTarget)){
                        validGuardedChain=false;
                        break;
                    }
                    join=secondaryTarget;
                    guarded.push_back({candidatePc,prefixBegin,candidatePc+2,secondaryPc,nextBegin});

                    int nextPrimary=-1;
                    if(seekCondition(nextBegin,nextPrimary)){
                        prefixBegin=nextBegin;
                        candidatePc=nextPrimary;
                        continue;
                    }
                    // Once candidates end, the no-match path may contain a
                    // straight-line fallback assignment before the shared join.
                    fallbackBegin=nextBegin;
                    if(join-fallbackBegin>16){
                        validGuardedChain=false;
                        break;
                }
                for(int fallbackPc=fallbackBegin;fallbackPc<join;++fallbackPc){
                    if(!guardedGapSafe(p.code[fallbackPc])) validGuardedChain=false;
                    }
                    break;
                }
                if(guarded.empty()||join<0||fallbackBegin<0) validGuardedChain=false;
                if(validGuardedChain){
                    std::set<int> allowedConditions;
                    std::map<int,int> allowedJumps;
                    for(const GuardedConditionCandidate& candidate:guarded){
                        allowedConditions.insert(candidate.primaryPc);
                        allowedConditions.insert(candidate.secondaryPc);
                        allowedJumps[candidate.primaryPc+1]=candidate.nextBegin;
                        allowedJumps[candidate.secondaryPc+1]=join;
                    }
                    for(const Instr& edge:p.code){
                        if(edge.op==22||edge.op==31||edge.op==32){
                            if(edge.pc>=pc&&edge.pc<join){
                                if(edge.op!=22||!allowedJumps.count(edge.pc)||allowedJumps[edge.pc]!=edge.target)
                                    validGuardedChain=false;
                            }else if(edge.target>pc&&edge.target<join){
                                validGuardedChain=false;
                            }
                        }else if(isCondition(edge.op)&&edge.pc>=pc&&edge.pc<join&&
                                 !allowedConditions.count(edge.pc)){
                            validGuardedChain=false;
                        }else if((edge.op==27||edge.op==33)&&edge.pc>=pc&&edge.pc<join){
                            validGuardedChain=false;
                        }
                    }
                }
                if(validGuardedChain){
                    const std::string flag=conditionChainFlagName(p,pc,context);
                    const std::string scopePad(size_t(indent+4),' ');
                    o<<pad<<"do\n"<<scopePad<<"local "<<flag<<" = false\n";
                    for(size_t candidateIndex=0;candidateIndex<guarded.size();++candidateIndex){
                        const GuardedConditionCandidate& candidate=guarded[candidateIndex];
                        const int candidateIndent=indent+8;
                        const std::string candidatePad(size_t(candidateIndent),' ');
                        if(candidateIndex>0){
                            o<<scopePad<<"if not "<<flag<<" then\n";
                            if(candidate.prefixBegin<candidate.primaryPc)
                                emitRange(o,p,candidate.prefixBegin,candidate.primaryPc,candidateIndent,context,true,
                                          loopBreakTarget,loopContinueTarget,loopBreakFlag);
                        }
                        o<<candidatePad<<"if "<<conditionExpr(p,p.code[candidate.primaryPc],context)<<" then\n";
                        if(candidate.payloadBegin<candidate.secondaryPc)
                            emitRange(o,p,candidate.payloadBegin,candidate.secondaryPc,candidateIndent+4,context,true,
                                      loopBreakTarget,loopContinueTarget,loopBreakFlag);
                        o<<std::string(size_t(candidateIndent+4),' ')<<"if "
                         <<negateConditionExpr(conditionExpr(p,p.code[candidate.secondaryPc],context))<<" then\n"
                         <<std::string(size_t(candidateIndent+8),' ')<<flag<<" = true\n"
                         <<std::string(size_t(candidateIndent+4),' ')<<"end\n"
                         <<candidatePad<<"end\n";
                        if(candidateIndex>0) o<<scopePad<<"end\n";
                    }
                    o<<scopePad<<"if not "<<flag<<" then\n";
                    if(fallbackBegin<join)
                        emitRange(o,p,fallbackBegin,join,indent+8,context,true,
                                  loopBreakTarget,loopContinueTarget,loopBreakFlag);
                    o<<scopePad<<"end\n"<<pad<<"end\n";
                    guardedJoin=join;
                    emittedChain=true;
                }
            }
            const int firstTarget=pc+1<end&&p.code[pc+1].op==22?p.code[pc+1].target:-1;
            separatedChain.push_back({pc,firstTarget,conditionExpr(p,p.code[pc],context)});
            cursor=pc+2;
            bool sawGap=false;
            while(firstTarget>=0&&cursor<end&&separatedChain.size()<9){
                const int gapBegin=cursor;
                while(cursor<end&&cursor-gapBegin<16&&straightLineConditionGap(p.code[cursor])) ++cursor;
                if((cursor==gapBegin&&!sawGap)||cursor+1>=end||!isCondition(p.code[cursor].op)||p.code[cursor+1].op!=22) break;
                sawGap=sawGap||cursor>gapBegin;
                separatedGaps.push_back({gapBegin,cursor});
                separatedChain.push_back({cursor,p.code[cursor+1].target,conditionExpr(p,p.code[cursor],context)});
                cursor+=2;
            }
            if(!emittedChain&&sawGap&&separatedChain.size()>=2){
                const int bodyBegin=separatedChain.back().pc+2;
                const int join=separatedChain.back().target;
                bool commonTrueBody=join>bodyBegin&&join<=end;
                bool commonOrPrefix=commonTrueBody;
                bool commonOrAnd=commonTrueBody&&separatedChain.front().target==bodyBegin;
                for(size_t test=0;test+1<separatedChain.size();++test)
                    commonOrPrefix=commonOrPrefix&&separatedChain[test].target==bodyBegin;
                for(size_t test=1;test<separatedChain.size();++test)
                    commonOrAnd=commonOrAnd&&separatedChain[test].target==join;
                int resume=join;
                bool hasElse=false;
                bool bodyEndsInReturn=false;
                if((commonOrPrefix||commonOrAnd)&&join>bodyBegin&&p.code[join-1].op==22&&p.code[join-1].target>join){
                    resume=p.code[join-1].target;
                    hasElse=resume<=end;
                }
                if((commonOrPrefix||commonOrAnd)&&join>bodyBegin&&!hasElse&&
                   (p.code[join-1].op==29||p.code[join-1].op==30)){
                    bool jumpsToJoin=false;
                    for(int bodyPc=bodyBegin;bodyPc<join-1;++bodyPc)
                        if(p.code[bodyPc].op==22&&p.code[bodyPc].target==join) jumpsToJoin=true;
                    if(!jumpsToJoin){resume=end;hasElse=true;bodyEndsInReturn=true;}
                }
                commonTrueBody=commonOrPrefix||commonOrAnd;
                bool callSeparatedBooleanTail=false;
                if(commonOrPrefix&&separatedChain.size()>=2&&bodyBegin>=0&&bodyBegin+2<end&&
                   separatedChain.front().target==bodyBegin&&separatedChain.back().target==bodyBegin+1&&
                   p.code[bodyBegin].op==2&&p.code[bodyBegin].b==0&&p.code[bodyBegin].c==1&&
                   p.code[bodyBegin+1].op==2&&p.code[bodyBegin+1].a==p.code[bodyBegin].a&&
                   p.code[bodyBegin+1].b==1&&p.code[bodyBegin+1].c==0&&
                   p.code[bodyBegin+2].op==30&&p.code[bodyBegin+2].a==p.code[bodyBegin].a&&
                   p.code[bodyBegin+2].b==2){
                    for(const ConditionGap& gap:separatedGaps)
                        for(int gapPc=gap.begin;gapPc<gap.end;++gapPc)
                            if(p.code[gapPc].op==28) callSeparatedBooleanTail=true;
                }
                if(callSeparatedBooleanTail) commonTrueBody=false;
                if(commonTrueBody){
                    // A branch from outside this expression into one of its
                    // gaps or arms would bypass the generated flag protocol.
                    for(const Instr& edge:p.code){
                        if(edge.pc>=pc&&edge.pc<resume) continue;
                        int target=-1;
                        if(edge.op==22||edge.op==31||edge.op==32) target=edge.target;
                        else if(isCondition(edge.op)||edge.op==27||edge.op==33)
                            target=followingControlTarget(p,edge);
                        if(target>pc&&target<resume) commonTrueBody=false;
                    }
                }
                if(commonTrueBody){
                    const std::string flag=conditionChainFlagName(p,pc,context);
                    const std::string scopePad(size_t(indent+4),' ');
                    o<<pad<<"do\n"<<scopePad<<"local "<<flag<<" = false\n";
                    if(commonOrAnd){
                        const int outerIndent=indent+4;
                        o<<std::string(size_t(outerIndent),' ')<<"if "<<negateConditionExpr(separatedChain.front().fallthrough)<<" then\n"
                         <<std::string(size_t(outerIndent+4),' ')<<flag<<" = true\n"
                         <<std::string(size_t(outerIndent),' ')<<"else\n";
                        const ConditionGap& firstGap=separatedGaps.front();
                        emitRange(o,p,firstGap.begin,firstGap.end,outerIndent+4,context,true,loopBreakTarget,loopContinueTarget,loopBreakFlag);
                        int nestedIndent=outerIndent+4;
                        for(size_t test=1;test<separatedChain.size();++test){
                            o<<std::string(size_t(nestedIndent),' ')<<"if "<<separatedChain[test].fallthrough<<" then\n";
                            if(test+1==separatedChain.size()){
                                o<<std::string(size_t(nestedIndent+4),' ')<<flag<<" = true\n";
                            }else{
                                const ConditionGap& gap=separatedGaps[test];
                                emitRange(o,p,gap.begin,gap.end,nestedIndent+4,context,true,loopBreakTarget,loopContinueTarget,loopBreakFlag);
                                nestedIndent+=4;
                            }
                        }
                        for(size_t test=1;test<separatedChain.size();++test){
                            nestedIndent-=4;
                            o<<std::string(size_t(nestedIndent),' ')<<"end\n";
                        }
                        o<<std::string(size_t(outerIndent),' ')<<"end\n";
                    }else{
                        int nestedIndent=indent+4;
                        for(size_t test=0;test+1<separatedChain.size();++test){
                            const std::string nestedPad(size_t(nestedIndent),' ');
                            o<<nestedPad<<"if "<<negateConditionExpr(separatedChain[test].fallthrough)<<" then\n"
                             <<std::string(size_t(nestedIndent+4),' ')<<flag<<" = true\n"
                             <<nestedPad<<"else\n";
                            const ConditionGap& gap=separatedGaps[test];
                            emitRange(o,p,gap.begin,gap.end,nestedIndent+4,context,true,loopBreakTarget,loopContinueTarget,loopBreakFlag);
                            nestedIndent+=4;
                        }
                        o<<std::string(size_t(nestedIndent),' ')<<flag<<" = "<<separatedChain.back().fallthrough<<"\n";
                        for(size_t test=0;test+1<separatedChain.size();++test){
                            nestedIndent-=4;
                            o<<std::string(size_t(nestedIndent),' ')<<"end\n";
                        }
                    }
                    o<<scopePad<<"if "<<flag<<" then\n";
                    const int bodyEnd=hasElse&&!bodyEndsInReturn?join-1:join;
                    if(bodyBegin<bodyEnd) emitRange(o,p,bodyBegin,bodyEnd,indent+8,context,true,loopBreakTarget,loopContinueTarget,loopBreakFlag);
                    if(hasElse){
                        o<<scopePad<<"else\n";
                        if(join<resume) emitRange(o,p,join,resume,indent+8,context,true,loopBreakTarget,loopContinueTarget,loopBreakFlag);
                    }
                    o<<scopePad<<"end\n"<<pad<<"end\n";
                    pc=resume;
                    emittedChain=true;
                }
            }
            // A comparison chain can stay lazy even when its operand setup
            // contains calls (including open-result call chains). Keep those
            // gaps inside nested `if`s when every failed test reaches one
            // shared join and the successful body has no other control edges.
            if(!emittedChain&&isCondition(i.op)&&pc+1<end&&p.code[pc+1].op==22){
                std::vector<ConditionJump> exitTests;
                std::vector<ConditionGap> exitGaps;
                int sharedJoin=-1;
                int testPc=pc;
                auto directReturnTarget=[&](int conditionPc){
                    if(conditionPc+1>=end||!isCondition(p.code[conditionPc].op)||p.code[conditionPc+1].op!=22) return -1;
                    const int target=p.code[conditionPc+1].target;
                    return target>sharedJoin&&target<end&&p.code[target].op==30?target:-1;
                };
                for(size_t testIndex=0;testIndex<128;++testIndex){
                    if(testPc+1>=end||!isCondition(p.code[testPc].op)||p.code[testPc+1].op!=22) break;
                    const int target=p.code[testPc+1].target;
                    if(sharedJoin<0) sharedJoin=target;
                    if(target!=sharedJoin) break;
                    exitTests.push_back({testPc,target,conditionExpr(p,p.code[testPc],context)});
                    int cursor=testPc+2;
                    const int gapBegin=cursor;
                    while(cursor<end&&cursor<sharedJoin&&cursor-gapBegin<64){
                        if(directReturnTarget(cursor)>=0){cursor+=2;continue;}
                        if(separatedConditionGap(p.code[cursor])){++cursor;continue;}
                        break;
                    }
                    if(cursor>gapBegin&&cursor+1<end&&isCondition(p.code[cursor].op)&&
                       p.code[cursor+1].op==22&&p.code[cursor+1].target==sharedJoin){
                        exitGaps.push_back({gapBegin,cursor});
                        testPc=cursor;
                        continue;
                    }
                    break;
                }
                const int bodyBegin=exitTests.empty()?-1:exitTests.back().pc+2;
                const int booleanTestPc=sharedJoin-2;
                const bool booleanResultTail=sharedJoin+2<end&&booleanTestPc>=bodyBegin&&
                    isCondition(p.code[booleanTestPc].op)&&p.code[booleanTestPc+1].op==22&&
                    p.code[booleanTestPc+1].target==sharedJoin+1&&
                    p.code[sharedJoin].op==2&&p.code[sharedJoin].b==0&&p.code[sharedJoin].c==1&&
                    p.code[sharedJoin+1].op==2&&p.code[sharedJoin+1].a==p.code[sharedJoin].a&&
                    p.code[sharedJoin+1].b==1&&p.code[sharedJoin+1].c==0&&
                    p.code[sharedJoin+2].op==30&&p.code[sharedJoin+2].a==p.code[sharedJoin].a&&
                    p.code[sharedJoin+2].b==2;
                const int guardBodyEnd=booleanResultTail?booleanTestPc:sharedJoin;
                const int handledEnd=booleanResultTail?sharedJoin+3:sharedJoin;
                bool commonGuardExit=exitTests.size()>=2&&exitGaps.size()+1==exitTests.size()&&
                    guardBodyEnd>=bodyBegin&&handledEnd<=end;
                if(commonGuardExit){
                    std::set<int> allowedConditions,allowedJumps;
                    for(const ConditionJump& test:exitTests){
                        allowedConditions.insert(test.pc);
                        allowedJumps.insert(test.pc+1);
                    }
                    std::map<int,int> earlyReturnTargets;
                    for(const ConditionGap& gap:exitGaps)
                        for(int scan=gap.begin;scan+1<gap.end;++scan){
                            const int target=directReturnTarget(scan);
                            if(target>=0){
                                earlyReturnTargets[scan]=target;
                                allowedConditions.insert(scan);
                                allowedJumps.insert(scan+1);
                            }
                        }
                    if(booleanResultTail){
                        allowedConditions.insert(booleanTestPc);
                        allowedJumps.insert(booleanTestPc+1);
                    }
                    auto branchEntersMiddle=[](const Proto& proto,const Instr& edge,int first,int last){
                        int target=-1;
                        if(edge.op==22||edge.op==31||edge.op==32) target=edge.target;
                        else if(isCondition(edge.op)||edge.op==27||edge.op==33)
                            target=followingControlTarget(proto,edge);
                        return target>first&&target<last;
                    };
                    for(const Instr& edge:p.code){
                        if(edge.pc>=pc&&edge.pc<sharedJoin){
                            if(isCondition(edge.op)&&allowedConditions.count(edge.pc)) continue;
                            const auto earlyReturn=earlyReturnTargets.find(edge.pc-1);
                            if(edge.op==22&&allowedJumps.count(edge.pc)&&
                               (edge.target==sharedJoin||(booleanResultTail&&edge.pc==booleanTestPc+1&&edge.target==sharedJoin+1)||
                                (earlyReturn!=earlyReturnTargets.end()&&edge.target==earlyReturn->second))) continue;
                            if(edge.op==22||isCondition(edge.op)||edge.op==27||edge.op==31||edge.op==32||edge.op==33||
                               (edge.op==2&&edge.c!=0)) commonGuardExit=false;
                        }else if(branchEntersMiddle(p,edge,pc,handledEnd)){
                            commonGuardExit=false;
                        }
                    }
                    for(int scan=pc;commonGuardExit&&scan<handledEnd;++scan){
                        const Instr& instruction=p.code[scan];
                        if(openResultProducer(instruction)&&!openProducerConsumedInRange(p,scan,sharedJoin))
                            commonGuardExit=false;
                        if(instruction.op==28&&instruction.b==0&&openProducerFor(p,instruction)<0)
                            commonGuardExit=false;
                    }
                }
                if(commonGuardExit){
                    const std::string flag=booleanResultTail?conditionChainFlagName(p,pc,context):std::string();
                    const std::string scopePad(size_t(indent+4),' ');
                    if(booleanResultTail) o<<pad<<"do\n"<<scopePad<<"local "<<flag<<" = false\n";
                    int nestedIndent=booleanResultTail?indent+4:indent;
                    auto emitGuardGap=[&](const ConditionGap& gap,int gapIndent){
                        int cursor=gap.begin;
                        for(int scan=gap.begin;scan+1<gap.end;++scan){
                            const int returnTarget=directReturnTarget(scan);
                            if(returnTarget<0) continue;
                            if(cursor<scan) emitRange(o,p,cursor,scan,gapIndent,context,true,
                                                      loopBreakTarget,loopContinueTarget,loopBreakFlag);
                            o<<std::string(size_t(gapIndent),' ')<<"if "
                             <<negateConditionExpr(conditionExpr(p,p.code[scan],context))<<" then\n";
                            emitRange(o,p,returnTarget,returnTarget+1,gapIndent+4,context,false,
                                      loopBreakTarget,loopContinueTarget,loopBreakFlag);
                            o<<std::string(size_t(gapIndent),' ')<<"end\n";
                            cursor=scan+2;
                            scan++;
                        }
                        if(cursor<gap.end) emitRange(o,p,cursor,gap.end,gapIndent,context,true,
                                                      loopBreakTarget,loopContinueTarget,loopBreakFlag);
                    };
                    for(size_t testIndex=0;testIndex<exitTests.size();++testIndex){
                        o<<std::string(size_t(nestedIndent),' ')<<"if "<<exitTests[testIndex].fallthrough<<" then\n";
                        nestedIndent+=4;
                        if(testIndex<exitGaps.size()){
                            const ConditionGap& gap=exitGaps[testIndex];
                            if(gap.begin<gap.end) emitGuardGap(gap,nestedIndent);
                        }
                    }
                    emitRange(o,p,bodyBegin,guardBodyEnd,nestedIndent,context,true,
                              loopBreakTarget,loopContinueTarget,loopBreakFlag);
                    if(booleanResultTail){
                        o<<std::string(size_t(nestedIndent),' ')<<"if "
                         <<conditionExpr(p,p.code[booleanTestPc],context)<<" then\n"
                         <<std::string(size_t(nestedIndent+4),' ')<<flag<<" = false\n"
                         <<std::string(size_t(nestedIndent),' ')<<"else\n"
                         <<std::string(size_t(nestedIndent+4),' ')<<flag<<" = true\n"
                         <<std::string(size_t(nestedIndent),' ')<<"end\n";
                    }
                    for(size_t testIndex=exitTests.size();testIndex>0;--testIndex){
                        nestedIndent-=4;
                        o<<std::string(size_t(nestedIndent),' ')<<"end\n";
                    }
                    if(booleanResultTail){
                        o<<scopePad<<"return "<<flag<<"\n"<<pad<<"end\n";
                        pc=handledEnd;
                    }else pc=sharedJoin;
                    emittedChain=true;
                }
            }
            // Lua lowers `a and b and ...` to consecutive conditions whose
            // failed branches all jump to one shared join. When every test
            // falls through, the source body begins after the final pair.
            if(!emittedChain&&chain.size()>=2){
                const int bodyBegin=chain.back().pc+2;
                const int join=chain.front().target;
                const bool loopLatch=join>0 && join<=end && p.code[join-1].op==22 &&
                    p.code[join-1].target==pc;
                const bool commonFalseExit=!loopLatch && join>bodyBegin && join<=end &&
                    std::all_of(chain.begin(),chain.end(),[&](const ConditionJump& test){return test.target==join;});
                if(commonFalseExit){
                    o<<pad<<"if ";
                    for(size_t n=0;n<chain.size();++n){
                        if(n)o<<" and ";
                        o<<chain[n].fallthrough;
                    }
                    o<<" then\n";
                    emitRange(o,p,bodyBegin,join,indent+4,context,true,loopBreakTarget,loopContinueTarget,loopBreakFlag);
                    o<<pad<<"end\n";
                    pc=join;
                    emittedChain=true;
                }
            }
            if(emittedChain){
                if(guardedJoin>=0) pc=guardedJoin;
                continue;
            }
            for(size_t final=1;final<chain.size();++final){
                const int bodyBegin=chain[final].pc+2;
                const int join=chain[final].target;
                const bool commonTrueBody=join>bodyBegin && join<=end &&
                    std::all_of(chain.begin(),chain.begin()+final,[&](const ConditionJump& prefix){return prefix.target==bodyBegin;});
                if(commonTrueBody){
                    o<<pad<<"if ";
                    for(size_t n=0;n<=final;++n){
                        if(n)o<<" or ";
                        if(n<final)o<<negateConditionExpr(chain[n].fallthrough);
                        else o<<chain[n].fallthrough;
                    }
                    o<<" then\n";
                    emitRange(o,p,bodyBegin,join,indent+4,context,true,loopBreakTarget,loopContinueTarget,loopBreakFlag);
                    o<<pad<<"end\n";
                    pc=join;
                    emittedChain=true;
                    break;
                }
            }
            if(emittedChain)continue;
        }
        // A branch target outside this recursive range is a shared CFG edge,
        // not the end of a larger nested source block. Expanding it here
        // duplicates the same suffix in every enclosing branch (some MoonSec
        // samples grow to tens of megabytes). Emit the in-range path once and
        // retain the escaping edge as an explicit boundary diagnostic.
        if(pc+1<end && p.code[pc+1].op==22 && p.code[pc+1].target>end &&
           (i.op==27 || isCondition(i.op))){
            const int target=p.code[pc+1].target;
            const int fallthrough=pc+2;
            if(i.op==27){
                const std::string tested=renderedLocal(p,i.b,i.pc,context);
                const std::string condition=i.c ? tested : "not ("+tested+")";
                o<<pad<<"if "<<condition<<" then\n";
                o<<std::string(size_t(indent+4),' ')<<renderedLocal(p,i.a,i.pc,context)<<" = "<<tested<<"\n";
                o<<std::string(size_t(indent+4),' ')<<"-- ByteVeil: branch at pc "<<i.pc<<" exits current structured range to pc "<<target<<"\n";
                o<<pad<<"else\n";
                if(fallthrough<end) emitRange(o,p,fallthrough,end,indent+4,context,false,loopBreakTarget,loopContinueTarget,loopBreakFlag);
                o<<pad<<"end\n";
            }else{
                o<<pad<<"if "<<conditionExpr(p,i,context)<<" then\n";
                if(fallthrough<end) emitRange(o,p,fallthrough,end,indent+4,context,false,loopBreakTarget,loopContinueTarget,loopBreakFlag);
                o<<pad<<"else\n";
                o<<std::string(size_t(indent+4),' ')<<"-- ByteVeil: branch at pc "<<i.pc<<" exits current structured range to pc "<<target<<"\n";
                o<<pad<<"end\n";
            }
            return;
        }
        // TESTSET consumes the following JMP.  Lua 5.1 copies B into A only
        // when truth(B) equals C, then takes that JMP; otherwise it skips the
        // JMP and evaluates the fallthrough range.  This is the usual shape
        // for `and`/`or` assignment lowering.  Reconstruct it only when the
        // branch is forward and therefore has a proven join; unusual or
        // cyclic shapes retain the explicit TESTSET diagnostic below.
        if(i.op==27 && pc+1<end && p.code[pc+1].op==22 && p.code[pc+1].target>pc+1){
            const int fallthrough=pc+2;
            const int join=p.code[pc+1].target;
            const std::string tested=renderedLocal(p,i.b,i.pc,context);
            const std::string condition=i.c ? tested : "not ("+tested+")";
            o<<pad<<"if "<<condition<<" then\n";
            o<<std::string(size_t(indent+4),' ')<<renderedLocal(p,i.a,i.pc,context)<<" = "<<tested<<"\n";
            if(fallthrough<join){
                o<<pad<<"else\n";
                emitRange(o,p,fallthrough,join,indent+4,context,true,loopBreakTarget,loopContinueTarget,loopBreakFlag);
            }
            o<<pad<<"end\n";
            pc=join;
            continue;
        }
        // Repeat/until: a conditional immediately followed by a backward jump.
        if(isCondition(i.op) && pc+1<end && p.code[pc+1].op==22 && p.code[pc+1].target>=begin && p.code[pc+1].target<pc){
            o<<pad<<"repeat\n"; emitRange(o,p,p.code[pc+1].target,pc,indent+4,context,true,pc+2); o<<pad<<"until "<<conditionExpr(p,i,context)<<"\n"; pc+=2; continue;
        }
        // While: condition, forward exit jump, body, backward jump to condition.
        if(isCondition(i.op) && pc+1<end && p.code[pc+1].op==22 && p.code[pc+1].target>pc+1){
            int bodyBegin=pc+2, exit=p.code[pc+1].target;
            if(exit<=end && exit>bodyBegin && p.code[exit-1].op==22 && p.code[exit-1].target==pc){
                o<<pad<<"while "<<conditionExpr(p,i,context)<<" do\n"; emitRange(o,p,bodyBegin,exit-1,indent+4,context,true,exit,pc); o<<pad<<"end\n"; pc=exit; continue;
            }
        }
        // If/elseif/else: conditional, jump over true branch, optional join jump.
        if(isCondition(i.op) && pc+1<end && p.code[pc+1].op==22 && p.code[pc+1].target>pc+1){
            int trueBegin=pc+2, falseBegin=p.code[pc+1].target;
            int join=falseBegin;
            int trueEnd=falseBegin;
            if(falseBegin-1>=trueBegin && p.code[falseBegin-1].op==22){
                const Instr& tailJump=p.code[falseBegin-1];
                const bool repeatLatch=falseBegin-2>=trueBegin && isCondition(p.code[falseBegin-2].op) &&
                    tailJump.target>=trueBegin && tailJump.target<falseBegin-2;
                if(tailJump.target>falseBegin){
                    join=tailJump.target;
                    trueEnd=falseBegin-1;
                }else if(tailJump.target==falseBegin&&!repeatLatch){
                    trueEnd=falseBegin-1;
                }
            }
            o<<pad<<"if "<<conditionExpr(p,i,context)<<" then\n"; emitRange(o,p,trueBegin,trueEnd,indent+4,context,true,loopBreakTarget,loopContinueTarget,loopBreakFlag);
            if(join> falseBegin){ o<<pad<<"else\n"; emitRange(o,p,falseBegin,join,indent+4,context,true,loopBreakTarget,loopContinueTarget,loopBreakFlag); }
            o<<pad<<"end\n"; pc=join; continue;
        }
        if(i.op==22 && loopBreakTarget>=0 && i.target==loopBreakTarget){
            if(!loopBreakFlag.empty()) o<<pad<<loopBreakFlag<<" = true\n";
            o<<pad<<"break\n";return;
        }
        if(i.op==22&&loopContinueTarget>=0&&i.target==loopContinueTarget){
            if(!loopBreakFlag.empty()) o<<pad<<"break\n";
            return;
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
        const bool inlineOpenResults=openProducerConsumedInRange(p,pc,end);
        const bool inlineSelf=pc+1<end&&colonMethodCall(p,pc,pc+1)&&
            !hasControlEntryAt(p,pc+1);
        const bool colonCall=pc>begin&&colonMethodCall(p,pc-1,pc)&&
            !hasControlEntryAt(p,pc);
        if(!loopSetup && !returnSetup && !inlineOpenResults && !inlineSelf)
            emitSimple(o,p,i,indent,context,colonCall);
        if(i.op==29 || i.op==30) break;
        if(i.op==2&&i.c){pc=i.target>=0?i.target:pc+2;continue;}
        pc++;
    }
}
static bool needsPcDispatcher(const std::string& source){
    static const char* markers[]={
        "-- ByteVeil: branch at pc ",
        "-- ByteVeil: unsupported opcode retained: EQ ",
        "-- ByteVeil: unsupported opcode retained: LT ",
        "-- ByteVeil: unsupported opcode retained: LE ",
        "-- ByteVeil: unsupported opcode retained: TEST ",
        "-- ByteVeil: TESTSET at pc ",
        "stopped at repeated control-flow pc ",
        "left reconstructed range at pc ",
        "FORLOOP at pc ",
        "FORPREP at pc ",
        "TFORLOOP at pc "
    };
    for(const char* marker:markers) if(source.find(marker)!=std::string::npos) return true;
    return false;
}
static std::string dispatcherPcName(const Proto& p,const RenderContext& context){
    std::set<std::string> names;
    for(const LocalInfo& local:p.locals) if(validIdentifier(local.name)) names.insert(local.name);
    for(int regIndex=0;regIndex<p.maxstack;regIndex++){
        names.insert(renderedLocal(p,regIndex,0,context));
        for(const Instr& instruction:p.code)
            names.insert(renderedLocal(p,regIndex,instruction.pc,context));
    }
    std::string name="__byteveil_pc_f"+std::to_string(p.id);
    while(names.count(name)) name.push_back('_');
    return name;
}
static int followingControlTarget(const Proto& p,const Instr& i){
    const int following=i.pc+1;
    if(following<0||following>=int(p.code.size())) return i.pc+2;
    return p.code[size_t(following)].pc+p.code[size_t(following)].sbx+1;
}
struct PcBlock { int start=0,end=0; };
static void emitPcBlock(std::ostringstream& o,const Proto& p,const PcBlock& block,int indent,const RenderContext& context,const std::string& pcName){
    const std::string pad(size_t(indent),' ');
    for(int pc=block.start;pc<block.end;){
        const Instr& i=p.code[size_t(pc)];
        if(i.closureBindingFor>=0||i.extraWord){++pc;continue;}
        if(i.op==22){
            o<<pad<<pcName<<" = "<<i.target<<"\n";
            return;
        }
        if(isCondition(i.op)||i.op==27){
            const int target=followingControlTarget(p,i);
            if(i.op==23||i.op==24||i.op==25){
                const std::string lhs=renderedValue(p,i.b,i.pc,context),rhs=renderedValue(p,i.c,i.pc,context);
                const char* op=i.op==23?" == ":i.op==24?" < ":" <= ";
                o<<pad<<"if ("<<lhs<<op<<rhs<<") == "<<(i.a?"true":"false")<<" then "<<pcName<<" = "<<target
                 <<" else "<<pcName<<" = "<<i.pc+2<<" end\n";
            }else{
                const int testRegister=i.op==27?i.b:i.a;
                o<<pad<<"if (not "<<renderedLocal(p,testRegister,i.pc,context)<<") ~= "<<(i.c?"true":"false")<<" then\n";
                if(i.op==27)o<<std::string(size_t(indent+4),' ')<<renderedLocal(p,i.a,i.pc,context)<<" = "<<renderedLocal(p,i.b,i.pc,context)<<"\n";
                o<<std::string(size_t(indent+4),' ')<<pcName<<" = "<<target<<"\n";
                o<<pad<<"else "<<pcName<<" = "<<i.pc+2<<" end\n";
            }
            return;
        }
        if(i.op==31){
            const std::string index=renderedLocal(p,i.a,i.pc,context);
            const std::string step=renderedLocal(p,i.a+2,i.pc,context);
            o<<pad<<index<<" = "<<index<<" + "<<step<<"\n";
            o<<pad<<"if (("<<step<<" > 0 and "<<index<<" <= "<<renderedLocal(p,i.a+1,i.pc,context)<<") or ("<<step<<" <= 0 and "<<index<<" >= "<<renderedLocal(p,i.a+1,i.pc,context)<<")) then\n";
            o<<std::string(size_t(indent+4),' ')<<renderedLocal(p,i.a+3,i.pc,context)<<" = "<<index<<"\n";
            o<<std::string(size_t(indent+4),' ')<<pcName<<" = "<<i.target<<"\n";
            o<<pad<<"else "<<pcName<<" = "<<i.pc+1<<" end\n";
            return;
        }
        if(i.op==32){
            for(int offset=0;offset<3;offset++)
                o<<pad<<renderedLocal(p,i.a+offset,i.pc,context)<<" = tonumber("<<renderedLocal(p,i.a+offset,i.pc,context)<<")\n";
            o<<pad<<renderedLocal(p,i.a,i.pc,context)<<" = "<<renderedLocal(p,i.a,i.pc,context)<<" - "<<renderedLocal(p,i.a+2,i.pc,context)<<"\n";
            o<<pad<<pcName<<" = "<<i.target<<"\n";
            return;
        }
        if(i.op==33){
            o<<pad;
            for(int result=0;result<i.c;result++){if(result)o<<", ";o<<renderedLocal(p,i.a+3+result,i.pc,context);}
            o<<" = "<<renderedLocal(p,i.a,i.pc,context)<<"("<<renderedLocal(p,i.a+1,i.pc,context)<<", "<<renderedLocal(p,i.a+2,i.pc,context)<<")\n";
            o<<pad<<"if "<<renderedLocal(p,i.a+3,i.pc,context)<<" ~= nil then\n";
            o<<std::string(size_t(indent+4),' ')<<renderedLocal(p,i.a+2,i.pc,context)<<" = "<<renderedLocal(p,i.a+3,i.pc,context)<<"\n";
            o<<std::string(size_t(indent+4),' ')<<pcName<<" = "<<followingControlTarget(p,i)<<"\n";
            o<<pad<<"else "<<pcName<<" = "<<i.pc+2<<" end\n";
            return;
        }
        const bool inlineOpenResults=openProducerConsumedInRange(p,i.pc,block.end);
        const bool inlineSelf=i.pc+1<block.end&&colonMethodCall(p,i.pc,i.pc+1)&&
            !hasControlEntryAt(p,i.pc+1);
        const bool colonCall=i.pc>block.start&&colonMethodCall(p,i.pc-1,i.pc)&&
            !hasControlEntryAt(p,i.pc);
        if(!inlineOpenResults&&!inlineSelf){
            Instr blockInstruction=i;
            if(openProducerFor(p,i)>=0&&openProducerFor(p,i)<block.start) blockInstruction.openProducer=-1;
            emitSimple(o,p,blockInstruction,indent,context,colonCall);
        }
        if(i.op==29||i.op==30) return;
        if(i.op==2&&i.c){o<<pad<<pcName<<" = "<<i.pc+2<<"\n";return;}
        if(i.op==34&&i.c==0){pc+=2;continue;}
        if(i.op==36){pc+=1+int(i.captures.size());continue;}
        ++pc;
    }
    o<<pad<<pcName<<" = "<<block.end<<"\n";
}
static void emitPcDispatcher(std::ostringstream& o,const Proto& p,int indent,const RenderContext& context){
    if(p.code.empty()) return;
    const std::string pad(size_t(indent),' '),pcName=dispatcherPcName(p,context);
    std::set<int> leaders{0};
    auto addLeader=[&](int pc){
        if(pc>=0&&pc<int(p.code.size())&&!p.code[size_t(pc)].extraWord&&p.code[size_t(pc)].closureBindingFor<0) leaders.insert(pc);
    };
    for(const Instr& i:p.code){
        if(i.closureBindingFor>=0||i.extraWord) continue;
        if(i.op==22){addLeader(i.pc+1);addLeader(i.target);}
        else if(isCondition(i.op)||i.op==27){addLeader(i.pc+1);addLeader(i.pc+2);addLeader(followingControlTarget(p,i));}
        else if(i.op==31){addLeader(i.pc+1);addLeader(i.target);}
        else if(i.op==32){addLeader(i.pc+1);addLeader(i.target);}
        else if(i.op==33){addLeader(i.pc+1);addLeader(i.pc+2);addLeader(followingControlTarget(p,i));}
        else if(i.op==2&&i.c){addLeader(i.pc+1);addLeader(i.pc+2);}
        else if(i.op==30){addLeader(i.pc+1);}
        else if(i.op==34&&i.c==0){addLeader(i.pc+2);}
        else if(i.op==36){addLeader(i.pc+1+int(i.captures.size()));}
    }
    std::vector<int> starts(leaders.begin(),leaders.end());
    std::vector<PcBlock> blocks;
    for(size_t n=0;n<starts.size();n++) blocks.push_back({starts[n],n+1<starts.size()?starts[n+1]:int(p.code.size())});
    o<<pad<<"-- ByteVeil: PC dispatcher preserves Lua 5.1 control flow in function "<<p.id<<"\n";
    o<<pad<<"local "<<pcName<<" = 0\n";
    o<<pad<<"while "<<pcName<<" >= 0 and "<<pcName<<" < "<<p.code.size()<<" do\n";
    std::function<void(size_t,size_t,int)> dispatch=[&](size_t first,size_t last,int level){
        const std::string branchPad(size_t(level),' ');
        if(last-first==1){
            emitPcBlock(o,p,blocks[first],level,context,pcName);
            return;
        }
        const size_t middle=first+(last-first)/2;
        o<<branchPad<<"if "<<pcName<<" < "<<blocks[middle].start<<" then\n";
        dispatch(first,middle,level+4);
        o<<branchPad<<"else\n";
        dispatch(middle,last,level+4);
        o<<branchPad<<"end\n";
    };
    dispatch(0,blocks.size(),indent+4);
    o<<pad<<"end\n";
}
static void emitReadableBody(std::ostringstream& o,const Proto& p,RenderContext& context,int indent,int firstRegister){
    if(context.stateMachine){
        emitRegisterDeclarations(o,p,context,indent,firstRegister);
        emitPcDispatcher(o,p,indent,context);
        return;
    }
    std::ostringstream structured;
    emitRange(structured,p,0,int(p.code.size()),indent,context);
    if(needsPcDispatcher(structured.str())){
        context.stateMachine=true;
        emitRegisterDeclarations(o,p,context,indent,firstRegister);
        emitPcDispatcher(o,p,indent,context);
    }else{
        emitRegisterDeclarations(o,p,context,indent,firstRegister);
        o<<structured.str();
    }
}
static void readableProto(std::ostringstream& o,const Proto& p){
    o<<"-- ByteVeil Lua 5.1 lifted (reconstructed) function "<<p.id<<" (CFG/SSA conservative)\n";
    RenderContext rootContext;
    emitReadableBody(o,p,rootContext,0,0);
    if(p.id==0 && (p.code.empty()||(p.code.back().op!=29&&p.code.back().op!=30))) o<<"return nil\n";
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
