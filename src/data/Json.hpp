#pragma once

#include <cmath>
#include <cstddef>
#include <cstdint>
#include <functional>
#include <locale>
#include <map>
#include <sstream>
#include <stdexcept>
#include <string>
#include <string_view>
#include <utility>
#include <variant>
#include <vector>

namespace restoration::data {
constexpr std::size_t MaxJsonDepth=64;

struct JsonValue {
    using Array=std::vector<JsonValue>;
    using Object=std::map<std::string,JsonValue,std::less<>>;
    std::variant<std::nullptr_t,bool,double,std::string,Array,Object> value=nullptr;

    const Object* object()const{return std::get_if<Object>(&value);}
    const JsonValue* get(std::string_view key)const{
        const auto* values=object();if(!values)return nullptr;
        const auto found=values->find(key);return found==values->end()?nullptr:&found->second;
    }
};

namespace detail {
inline bool DecodeUtf8(std::string_view text,std::size_t at,std::size_t& length,std::uint32_t& codepoint){
    if(at>=text.size())return false;
    const auto first=static_cast<unsigned char>(text[at]);
    if(first<0x80){length=1;codepoint=first;return true;}
    if(first>=0xc2&&first<=0xdf){length=2;codepoint=first&0x1f;}
    else if(first>=0xe0&&first<=0xef){length=3;codepoint=first&0x0f;}
    else if(first>=0xf0&&first<=0xf4){length=4;codepoint=first&0x07;}
    else return false;
    if(at+length>text.size())return false;
    for(std::size_t i=1;i<length;++i){
        const auto byte=static_cast<unsigned char>(text[at+i]);
        if((byte&0xc0)!=0x80)return false;
        if(i==1&&((first==0xe0&&byte<0xa0)||(first==0xed&&byte>=0xa0)||
                  (first==0xf0&&byte<0x90)||(first==0xf4&&byte>=0x90)))return false;
        codepoint=(codepoint<<6)|(byte&0x3f);
    }
    return true;
}

inline bool ValidUtf8(std::string_view text,bool rejectControls=false){
    for(std::size_t i=0;i<text.size();){
        std::size_t length=0;std::uint32_t codepoint=0;
        if(!DecodeUtf8(text,i,length,codepoint)||(rejectControls&&(codepoint<0x20||codepoint==0x7f)))return false;
        i+=length;
    }
    return true;
}

inline void AppendUtf8(std::string& target,std::uint32_t value){
    if(value<=0x7f)target.push_back(static_cast<char>(value));
    else if(value<=0x7ff){target.push_back(static_cast<char>(0xc0|(value>>6)));target.push_back(static_cast<char>(0x80|(value&0x3f)));}
    else if(value<=0xffff){target.push_back(static_cast<char>(0xe0|(value>>12)));target.push_back(static_cast<char>(0x80|((value>>6)&0x3f)));target.push_back(static_cast<char>(0x80|(value&0x3f)));}
    else{target.push_back(static_cast<char>(0xf0|(value>>18)));target.push_back(static_cast<char>(0x80|((value>>12)&0x3f)));target.push_back(static_cast<char>(0x80|((value>>6)&0x3f)));target.push_back(static_cast<char>(0x80|(value&0x3f)));}
}
}

class JsonParser {
public:
    explicit JsonParser(std::string_view source):source_(source){}
    JsonValue parse(){
        skipSpace();auto result=parseValue(0);skipSpace();
        if(position_!=source_.size())fail("Trailing JSON data");
        return result;
    }
private:
    [[noreturn]] void fail(const char* message)const{throw std::runtime_error(message);}
    void skipSpace(){while(position_<source_.size()&&(source_[position_]==' '||source_[position_]=='\t'||source_[position_]=='\r'||source_[position_]=='\n'))++position_;}
    bool take(char value){if(position_<source_.size()&&source_[position_]==value){++position_;return true;}return false;}
    void require(char value){if(!take(value))fail("Malformed JSON");}
    JsonValue parseValue(std::size_t depth){
        if(depth>MaxJsonDepth)fail("JSON nesting is too deep");
        skipSpace();if(position_==source_.size())fail("Unexpected end of JSON");
        const char value=source_[position_];
        if(value=='{')return JsonValue{parseObject(depth+1)};
        if(value=='[')return JsonValue{parseArray(depth+1)};
        if(value=='\"')return JsonValue{parseString()};
        if(value=='t'){literal("true");return JsonValue{true};}
        if(value=='f'){literal("false");return JsonValue{false};}
        if(value=='n'){literal("null");return JsonValue{nullptr};}
        if(value=='-'||(value>='0'&&value<='9'))return JsonValue{parseNumber()};
        fail("Invalid JSON value");
    }
    JsonValue::Object parseObject(std::size_t depth){
        require('{');skipSpace();JsonValue::Object result;if(take('}'))return result;
        for(;;){
            skipSpace();if(position_==source_.size()||source_[position_]!='\"')fail("Expected JSON object key");
            auto key=parseString();skipSpace();require(':');
            if(!result.emplace(std::move(key),parseValue(depth)).second)fail("Duplicate JSON object key");
            skipSpace();if(take('}'))return result;require(',');
        }
    }
    JsonValue::Array parseArray(std::size_t depth){
        require('[');skipSpace();JsonValue::Array result;if(take(']'))return result;
        for(;;){result.push_back(parseValue(depth));skipSpace();if(take(']'))return result;require(',');}
    }
    unsigned hex4(){
        unsigned value=0;
        for(int i=0;i<4;++i){
            if(position_==source_.size())fail("Incomplete JSON unicode escape");
            const char c=source_[position_++];unsigned digit;
            if(c>='0'&&c<='9')digit=static_cast<unsigned>(c-'0');
            else if(c>='a'&&c<='f')digit=static_cast<unsigned>(c-'a'+10);
            else if(c>='A'&&c<='F')digit=static_cast<unsigned>(c-'A'+10);
            else fail("Invalid JSON unicode escape");
            value=(value<<4)|digit;
        }
        return value;
    }
    std::string parseString(){
        require('\"');std::string result;
        while(position_<source_.size()){
            const auto c=static_cast<unsigned char>(source_[position_++]);
            if(c=='\"'){
                if(!detail::ValidUtf8(result))fail("Invalid UTF-8 in JSON string");
                return result;
            }
            if(c<0x20)fail("Unescaped control character in JSON string");
            if(c!='\\'){result.push_back(static_cast<char>(c));continue;}
            if(position_==source_.size())fail("Incomplete JSON escape");
            switch(source_[position_++]){
            case '\"':result.push_back('\"');break;case '\\':result.push_back('\\');break;case '/':result.push_back('/');break;
            case 'b':result.push_back('\b');break;case 'f':result.push_back('\f');break;case 'n':result.push_back('\n');break;case 'r':result.push_back('\r');break;case 't':result.push_back('\t');break;
            case 'u':{
                std::uint32_t point=hex4();
                if(point>=0xd800&&point<=0xdbff){
                    if(position_+2>source_.size()||source_[position_]!='\\'||source_[position_+1]!='u')fail("Unpaired JSON surrogate");
                    position_+=2;const auto low=hex4();if(low<0xdc00||low>0xdfff)fail("Unpaired JSON surrogate");
                    point=0x10000+((point-0xd800)<<10)+(low-0xdc00);
                }else if(point>=0xdc00&&point<=0xdfff)fail("Unpaired JSON surrogate");
                detail::AppendUtf8(result,point);break;
            }
            default:fail("Invalid JSON escape");
            }
        }
        fail("Unterminated JSON string");
    }
    double parseNumber(){
        const auto start=position_;take('-');
        if(take('0')){if(position_<source_.size()&&source_[position_]>='0'&&source_[position_]<='9')fail("Leading zero in JSON number");}
        else{if(position_==source_.size()||source_[position_]<'1'||source_[position_]>'9')fail("Invalid JSON number");while(position_<source_.size()&&source_[position_]>='0'&&source_[position_]<='9')++position_;}
        if(take('.')){if(position_==source_.size()||source_[position_]<'0'||source_[position_]>'9')fail("Invalid JSON fraction");while(position_<source_.size()&&source_[position_]>='0'&&source_[position_]<='9')++position_;}
        if(take('e')||take('E')){if(!take('+'))take('-');if(position_==source_.size()||source_[position_]<'0'||source_[position_]>'9')fail("Invalid JSON exponent");while(position_<source_.size()&&source_[position_]>='0'&&source_[position_]<='9')++position_;}
        const std::string token(source_.substr(start,position_-start));
        std::istringstream input(token);input.imbue(std::locale::classic());double result=0;input>>result;
        if(!input||input.peek()!=std::char_traits<char>::eof()||!std::isfinite(result))fail("Invalid JSON number");
        return result;
    }
    void literal(std::string_view word){if(source_.substr(position_,word.size())!=word)fail("Invalid JSON literal");position_+=word.size();}
    std::string_view source_;std::size_t position_=0;
};
}
