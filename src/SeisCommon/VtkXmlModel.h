#pragma once
#include <algorithm>
#include <array>
#include <cctype>
#include <cmath>
#include <cstring>
#include <fstream>
#include <locale>
#include <map>
#include <sstream>
#include <stdexcept>
#include <string>
namespace SeisCommon::VtkXml {
inline constexpr std::size_t maxModelPoints=36000000;
inline void require(bool ok, const std::string& message) {
    if(!ok) throw std::runtime_error("VTI: " + message);
}
struct Tag {
    std::string name;
    std::map<std::string,std::string> attributes;
    bool close=false, empty=false;
    std::string attr(const std::string& key, const std::string& fallback="") const {
        auto it=attributes.find(key); return it==attributes.end()?fallback:it->second;
    }
};
// A bounded XML subset: no entities/DTD, no compression or binary payloads.
// Unknown numeric arrays are skipped so Curvilinear metadata/metrics remain optional.
inline Tag nextTag(std::istream& in) {
    for(;;) {
        in>>std::ws;
        if(in.peek()==std::char_traits<char>::eof()) return {};
        require(in.get()=='<',"Unexpected text outside a DataArray.");
        if(in.peek()=='!') {
            require(in.get()=='!'&&in.get()=='-'&&in.get()=='-',"DTD and XML declarations are unsupported.");
            std::string tail; bool done=false;
            for(std::size_t i=0;i<1024*1024;++i) {
                const int c=in.get();require(c!=std::char_traits<char>::eof(),"Truncated comment.");
                tail.push_back(char(c)); if(tail.size()>3) tail.erase(tail.begin());
                if(tail=="-->") {done=true;break;}
            }
            require(done,"Oversized XML comment.");continue;
        }
        std::string text; char quote=0;
        for(;;) {
            const int value=in.get();require(value!=std::char_traits<char>::eof(),"Truncated XML tag.");
            const char c=char(value);
            if(!quote&&c=='>') break;
            if(c=='\''||c=='"') {if(!quote) quote=c;else if(quote==c) quote=0;}
            text.push_back(c);require(text.size()<65536,"Oversized XML tag.");
        }
        if(text.starts_with("?xml ")&&text.ends_with('?')) continue;
        Tag tag; std::size_t i=0;
        if(!text.empty()&&text[0]=='/') {tag.close=true;++i;}
        auto space=[&]{while(i<text.size()&&std::isspace(static_cast<unsigned char>(text[i]))) ++i;};
        const auto begin=i;
        while(i<text.size()&&!std::isspace(static_cast<unsigned char>(text[i]))&&text[i]!='/') ++i;
        tag.name=text.substr(begin,i-begin);require(!tag.name.empty(),"Empty XML tag.");
        for(;;) {
            space();if(i==text.size()) break;
            if(text[i]=='/') {++i;space();require(i==text.size()&&!tag.close,"Invalid empty tag.");tag.empty=true;break;}
            require(!tag.close,"Attributes on a closing tag.");
            const auto start=i;
            while(i<text.size()&&text[i]!='='&&!std::isspace(static_cast<unsigned char>(text[i]))) ++i;
            const auto key=text.substr(start,i-start);space();
            require(!key.empty()&&i<text.size()&&text[i++]=='=',"Invalid XML attribute.");space();
            require(i<text.size()&&(text[i]=='"'||text[i]=='\''),"Unquoted XML attribute.");
            const char delimiter=text[i++];const auto valueStart=i;
            while(i<text.size()&&text[i]!=delimiter) ++i;
            require(i<text.size(),"Unterminated XML attribute.");
            const auto value=text.substr(valueStart,i-valueStart);++i;
            require(value.find('&')==std::string::npos,"XML entities are unsupported.");
            require(tag.attributes.emplace(key,value).second,"Duplicate XML attribute.");
        }
        return tag;
    }
}
struct Array {std::streamoff begin=0,end=0;int components=1;std::string type,tuples;};
inline std::streamoff skipArray(std::ifstream& in) {
    std::array<char,65536> buffer{};
    for(;;) {
        const auto start=in.tellg();in.read(buffer.data(),buffer.size());const auto got=in.gcount();
        const auto* found=static_cast<const char*>(std::memchr(buffer.data(),'<',std::size_t(got)));
        if(found) {const auto position=std::streamoff(start)+(found-buffer.data());in.clear();in.seekg(position);return position;}
        require(bool(in),"Truncated DataArray.");
    }
}
inline int integer(const std::string& text,int low,int high) {
    std::istringstream stream(text);stream.imbue(std::locale::classic());long long value;
    require(bool(stream>>value),"Invalid integer.");stream>>std::ws;
    require(stream.eof()&&value>=low&&value<=high,"Integer outside supported limits.");return int(value);
}
inline std::array<int,6> extent(const std::string& text) {
    std::istringstream stream(text);std::array<int,6> result{};
    for(int& v:result) require(bool(stream>>v)&&v>=0&&v<=int(maxModelPoints),"Invalid grid extent.");
    stream>>std::ws;require(stream.eof(),"Invalid grid extent suffix.");return result;
}
inline void seek(std::ifstream& in,const Array& a) {in.clear();in.seekg(a.begin);require(bool(in),"Cannot seek model array.");}
inline double number(std::istream& in) {
    double value=0;require(bool(in>>value)&&std::isfinite(value),"Missing, invalid or nonfinite numeric value.");return value;
}
inline void ended(std::ifstream& in,const Array& a) {
    in>>std::ws;require(in.tellg()==a.end,"DataArray length does not match the grid.");
}
inline void material(float vp,float vs,float rho) {
    require(std::isfinite(vp)&&std::isfinite(vs)&&std::isfinite(rho)&&vp>0&&vp<=30000&&vs>=0&&vs<vp
        &&double(vp)*vp>(4.0/3.0)*vs*vs&&rho>0&&rho<=50000,"Invalid Vp/Vs/Density material.");
}

}
