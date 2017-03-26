//source: http://quartushelp.altera.com/15.0/mergedProjects/reference/glossary/def_mif.htm

/*
ECE342 Lab6 Assembler
command line argument: optional DEPTH (number of words in total)
width is hardcoded to be 16
encoding is hardcoded according to lab6 document; IR is assumed to use upper 9 bits from DIN

Features:
	able to define constants using "#define name value"
	able to hardcode data using "#data value"
	support single line comments (starting with "//")
	support for labels (value is the address of next instructions)
	support addition and subtraction when evaluating expressions (for mvi)

Note:
	1.	This program reads from stdin and write mif to stdout. Errors or other information goes to stderr.
		Use redirection to read from / write to files.
		
	2.	If the starting address of ROM is not zero (not the case if you follow lab6 suggestion),
		please define a constant as the starting address and add it to all expressions with labels
		e.g.	#define ROM_ADDRESS 0xf000
				LABEL_START:
				mvi pc, LABEL_START+ROM_ADDRESS
				
	3.	Please do not put whitespace inside one expression; everything after the third field will be discarded
		e.g.	don't write: "mvi R0, Constant1+ Constant2", which becomes "mvi R0, Constant1+" and gives you error
				write it as: "mvi R0, Constant1+Constant2"
	
	4.	All ',' will be substituted by whitespace before analyzing the instruction.
		You can use comma or any whitespace to split fields
	
	5.	Compile using at least C++11
	
	6.	Please prevent constants and labels having the same name.
		If it happens, then if the constant is defined before using it, evaluation treat it as constant,
			otherwise it is treated as a label
		Constants cannot have same name, and same to labels (treated as error).
		You can have multiple labels labeling the same address.
	
	6.	Please forgive for misuse of variable names...
	
*/

//options
//if you want to see something in stderr even if nothing goes wrong
#ifdef VERBOSE
#define INFO_SHOW_LABEL_WHEN_PARSED
#define INFO_SHOW_CONSTANT_WHEN_PARSED
#define INFO_SHOW_ERRORCOUNT
#endif

//the field indicating definition of constants
#ifndef INSTR_DEFINE_CONSTANT_STR
#define INSTR_DEFINE_CONSTANT_STR "#define"
#endif

//output all constants and labels at beginning of mif file
//#define OUTPUT_WRITE_SYMBOL_TABLE

//explicitly zero fill the rest of memory
#define OUTPUT_ZERO_FILL

//includes
#include <iostream>
#include <iomanip>
#include <string>
#include <sstream>
#include <unordered_map>
#include <vector>
#include <locale>

#ifdef OUTPUT_WRITE_SYMBOL_TABLE
#include <map>
#endif

//constants
const std::string WHITESPACE=" \t";

typedef unsigned content_type;
typedef int offset_type;

constexpr content_type INSTR_MV		=0;
constexpr content_type INSTR_MVI	=1;
constexpr content_type INSTR_ADD	=2;
constexpr content_type INSTR_SUB	=3;
constexpr content_type INSTR_LD		=4;
constexpr content_type INSTR_ST		=5;
constexpr content_type INSTR_MVNZ	=6;

constexpr content_type INSTR_DATA	=8;//used to hardcode data in ROM

constexpr unsigned OFFSET_RIGHT_PADDING		=7;
constexpr unsigned OFFSET_OPCODE			=OFFSET_RIGHT_PADDING+6;
constexpr unsigned OFFSET_RX				=OFFSET_RIGHT_PADDING+3;
constexpr unsigned OFFSET_RY				=OFFSET_RIGHT_PADDING;

constexpr unsigned long long PADD_NOOP=0;

const std::string INSTR_DEFINE_CONSTANT=INSTR_DEFINE_CONSTANT_STR;

const std::unordered_map<std::string,content_type> OPCODE_MAP={
		{"#data",INSTR_DATA},//use it if you want to store constants in ROM
		{"mv",INSTR_MV},
		{"mvi",INSTR_MVI},
		{"add",INSTR_ADD},
		{"sub",INSTR_SUB},
		{"ld",INSTR_LD},
		{"st",INSTR_ST},
		{"mvnz",INSTR_MVNZ}
};
const std::unordered_map<std::string,content_type> registerMap={
		{"r0",0},
		{"r1",1},
		{"r2",2},
		{"r3",3},
		{"r4",4},
		{"r5",5},
		{"r6",6},
		{"r7",7},
		{"pc",7}
};

//global variables
std::unordered_map<
		std::string,	//name of constant
		content_type	//value
> constantMap;//put constants

std::unordered_map<
		std::string,	//name of label
		content_type	//value
> labelMap;//put labels

std::vector<unsigned long long> assembly;//store outputs

std::vector<std::string> comment_code;//used to output source code as comment in mif

std::vector<std::pair<std::string,content_type>> comment_label;//used to put label as comment in mif ([address,labelName])
	
std::unordered_map<
		std::string,			//name of needed label
		std::vector<
			std::pair<
				content_type,	//where is the immediate need updating
				offset_type		//the offset wrt label address
			>
		>
> pendingLabelMap;//during the first pass, all dependency on labels will be stored here

unsigned lineCount=1;

//check if a string is a valid symbol name
//alphanumeric characters or underscore '_', and first character cannot be a number
bool isNameValid(const std::string& str){
	std::locale loc;
	std::size_t i=0;
	while((i<str.length())&&((std::isalnum(str[i],loc))||str[i]=='_')){
		++i;
	}
	return !((str.length()==0)||(i<str.length())||(std::isdigit(str[0],loc)));
}

//remove whitespace in both ends
void trim(std::string& str){
	std::size_t strStart=str.find_first_not_of(WHITESPACE);
	if(strStart==std::string::npos){
		str.clear();
	}else{
		str.erase(0,strStart);
		std::size_t strEnd=str.find_last_not_of(WHITESPACE);
		if(strEnd!=std::string::npos) str.resize(strEnd+1);
	}
}

//get lower case string
std::string getLowerCase(const std::string& str){
	std::string result;
	std::locale loc;
	result.reserve(str.length());
	for(std::size_t i=0;i<str.length();++i){
		result.push_back(std::tolower(str[i],loc));
	}
	return result;
}

//lookup register name
bool convert2Reg(const std::string& arg, content_type& result){
	auto iter=registerMap.find(getLowerCase(arg));
	if(iter==registerMap.end()){
		return false;
	}else{
		result=iter->second;
		return true;
	}
}

//evaluate single expression (without operator) (either a constant name or a number)
//return false if things goes wrong
//currently only unsigned integer type supported
bool convert2Value(const std::string& arg, content_type& result){
	std::locale loc;
	if(arg.empty()) return false;
	
	//test if the expression looks like a register; give a warning if yes
	{
		content_type tmp2=0;
		if(convert2Reg(arg,tmp2)){
			std::cerr<<"Warning: at line "<<lineCount<<": immediate expression \""<<arg<<"\" looks like a register"<<std::endl;
		}
	}
	content_type tmp=0;
	unsigned radix=10;
	if(std::isdigit(arg[0],loc)){
		//input is a number
		if(arg[0]=='0'&&arg.length()>2){
			switch(arg[1]){
				case 'x':
				case 'X':
					radix=16;
					break;
				case 'b':
				case 'B':
					radix=2;
					break;
				case 'd'://no one will use this, right?
				case 'D':
					radix=10;
					break;
				case 'o':
				case 'O':
					radix=8;
					break;
				default:
					return false;
			}
			for(std::size_t i=2;i<arg.length();++i){
				unsigned digit=0;
				if((arg[i]>='0')&&(arg[i]<='9')){
					digit=arg[i]-'0';
				}else if((arg[i]>='A')&&(arg[i]<='F')){
					digit=arg[i]-'A'+10;
				}else if((arg[i]>='a')&&(arg[i]<='f')){
					digit=arg[i]-'a'+10;
				}else{
					return false;
				}
				if(digit>=radix) return false;
				tmp*=radix;
				tmp+=digit;
			}
			result=tmp;
			return true;
		}else{
			//std::stoul sometimes do not work with MinGW on Windows
			/*
			try{
				tmp=std::stoul(arg,nullptr);
			}catch(...){
				return false;
			}*/
			std::stringstream buf(arg);
			buf>>tmp;
			if(buf.fail()) return false;
			
			result=tmp;
			return true;
		}
	}else{
		//input is a constant
		auto iter_const=constantMap.find(arg);
		if(iter_const!=constantMap.end()){
			result=iter_const->second;
			return true;
		}else{
			return false;
		}
	}
}

//evaluate expression, and supports simple arithmetic expression (add and sub)
//if the expression has no label, then only result will be set
//if the expression has label, then offset and label will be set
//also, you cannot subtract a value by a label
bool convert2Value_Expression(const std::string& arg, content_type& result,offset_type& offset, std::string& label){
	offset_type tmpOffset=0;
	std::string unresolvedLabel;
	if(arg.empty()) return false;
	std::size_t curSubExpressionStart=0;
	bool isThisEntryPlus=true;
	for(std::size_t i=0;i<=arg.length();++i){
		if((arg[i]=='+')||(arg[i]=='-')||(i==arg.length())){
			std::string subExpression=arg.substr(curSubExpressionStart,i-curSubExpressionStart);
			//std::cerr<<"Debug: evaluating sub expression "<<subExpression<<",i="<<i<<std::endl;
			curSubExpressionStart=i+1;
			content_type curValue=0;
			if(!(convert2Value(subExpression,curValue))){
				if(unresolvedLabel.empty()&&isNameValid(subExpression)&&isThisEntryPlus){
					unresolvedLabel=subExpression;
				}else{
					return false;//expression depends on more than one label / invalid expression
				}
			}else{
				if(isThisEntryPlus){
					tmpOffset+=curValue;
				}else{
					tmpOffset-=curValue;
				}
			}
			isThisEntryPlus=(arg[i]=='+');
		}
	}
	if(unresolvedLabel.empty()){
		result=tmpOffset;
	}else{
		offset=tmpOffset;
		label=unresolvedLabel;
	}
	return true;
}

//function that does main job
int process(unsigned depth,unsigned width){
	assembly.reserve(depth);
	
	unsigned errorCount=0;
	
	//warning if a label is labeling a non-instruction (constants definition)
	bool isThisAddressLabelled=false;
	
	for(lineCount=1;(std::cin.good());++lineCount){
		std::string line;
		std::getline(std::cin,line);
		if(line.back()=='\n') line.pop_back();
		if(line.back()=='\r') line.pop_back();
		
		//ignore comment
		std::size_t comment_start=line.find("//");
		if(comment_start!=std::string::npos) line.resize(comment_start);
		
		//find if any labels are defined here
		std::size_t label_end=line.find(':');
		while(label_end!=std::string::npos){
			std::string labelName=line.substr(0,label_end);
			line.erase(0,label_end+1);//remove the ':' as well
			
			//trim labelName
			trim(labelName);
			
			//check if the label is valid
			if(!(isNameValid(labelName))){
				std::cerr<<"Error: at line "<<lineCount<<": invalid labelName \""<<labelName<<'"'<<std::endl;
				++errorCount;
			}else{
				auto iter_label=labelMap.find(labelName);
				if(iter_label!=labelMap.end()){
					std::cerr<<"Error: at line "<<lineCount<<": label \""<<labelName<<"\" is already defined (value="<<iter_label->second<<')'<<std::endl;
					++errorCount;
				}else{
					const std::pair<std::string,content_type> tmpPair(labelName,assembly.size());
					labelMap.insert(tmpPair);
					comment_label.push_back(tmpPair);
					isThisAddressLabelled=true;
#ifdef INFO_SHOW_LABEL_WHEN_PARSED
					std::cerr<<"Info: at line "<<lineCount<<": label \""<<labelName<<"\" = "<<assembly.size()<<std::endl;
#endif
				}
			}
			//find next one, if any
			label_end=line.find(':');
		}
		
		//separate fields
		for(std::size_t i=0;i<line.length();++i){
			if(line[i]==',') line[i]=' ';
		}
		
		//trim this line
		trim(line);
		if(line.empty()) continue;
		
		std::stringstream lineBuffer(line);
		std::string instr;
		std::string arg1;
		std::string arg2;
		std::string shouldBeEmpty;
		lineBuffer>>instr>>arg1>>arg2>>shouldBeEmpty;
		instr=getLowerCase(instr);
		
		if(!(shouldBeEmpty.empty())){
			std::cerr<<"Warning: at line "<<lineCount<<": everything after \""<<arg2<<"\" is ignored"<<std::endl;
		}
		
		if(instr==INSTR_DEFINE_CONSTANT){
			//check if the label is valid
			if(isNameValid(arg1)){
				auto iter_const=constantMap.find(arg1);
				if(iter_const!=constantMap.end()){
					std::cerr<<"Error: at line "<<lineCount<<": constant \""<<arg1<<"\" is already defined"<<std::endl;
					++errorCount;
				}else{
					content_type value=0;
					std::string label;
					offset_type offset=0;
					if(convert2Value_Expression(arg2,value,offset,label)&&label.empty()){
						constantMap.insert(std::pair<std::string,content_type>(arg1,value));
#ifdef INFO_SHOW_CONSTANT_WHEN_PARSED
						std::cerr<<"Info: at line "<<lineCount<<": constant \""<<arg1<<"\" = "<<value<<std::endl;
#endif
						if(isThisAddressLabelled){
							std::cerr<<"Warning: at line "<<lineCount<<": constant definition after a label"<<std::endl;
						}
					}else{
						std::cerr<<"Error: at line "<<lineCount<<": constant \""<<arg1<<"\" has invalid expression (\""<<arg2<<"\")"<<std::endl;
						++errorCount;
					}
				}
			}else{
				std::cerr<<"Error: at line "<<lineCount<<": constant name \""<<arg1<<"\" is invalid"<<std::endl;
				++errorCount;
			}
		}else{
			auto iter_instr=OPCODE_MAP.find(instr);
			if(iter_instr==OPCODE_MAP.end()){
				std::cerr<<"Error: at line "<<lineCount<<": invalid opcode \""<<instr<<'"'<<std::endl;
				++errorCount;
			}else{
				std::string codeComment=instr;
				codeComment+='\t';
				codeComment+=arg1;
				codeComment+=",\t";
				codeComment+=arg2;
				comment_code.push_back(codeComment);
				
				switch(iter_instr->second){
					case INSTR_MV:
					case INSTR_ADD:
					case INSTR_SUB:
					case INSTR_LD:
					case INSTR_ST:
					case INSTR_MVNZ:
					{
						//opcode followed by rx and ry
						content_type rx=0;
						content_type ry=0;
						bool rxGood=convert2Reg(arg1,rx);
						bool ryGood=convert2Reg(arg2,ry);
						if(rxGood&&ryGood){
							unsigned long long content=((iter_instr->second)<<OFFSET_OPCODE)
														+(rx<<OFFSET_RX)
														+(ry<<OFFSET_RY);
							assembly.push_back(content);
							//std::cerr<<"Debug: content="<<content<<std::endl;
						}else{
							assembly.push_back(PADD_NOOP);
							std::cerr<<"Error: at line "<<lineCount<<": failed to interpret \""<<arg1<<"\" or \""<<arg2<<"\" as register"<<std::endl;
							++errorCount;
						}
					}break;
					case INSTR_MVI:
					{
						//opcode followed by rx and immediate
						comment_code.push_back(std::string());
						content_type rx=0;
						content_type immediate=0;
						offset_type offset=0;
						std::string label;
						bool rxGood=convert2Reg(arg1,rx);
						bool immGood=convert2Value_Expression(arg2,immediate,offset,label);
						if(rxGood){
							unsigned long long content=((iter_instr->second)<<OFFSET_OPCODE)
														+(rx<<OFFSET_RX);
							assembly.push_back(content);
							//std::cerr<<"Debug: content="<<content<<std::endl;
							assembly.push_back(immediate);
							//std::cerr<<"Debug: immediate="<<immediate<<std::endl;
							/*
							if(!immGood){
								if(isNameValid(arg2)){
									//add address of immediate to pendingLabelMap
									auto iter_pend=pendingLabelMap.find(arg2);
									if(iter_pend==pendingLabelMap.end()){
										auto tmpPair=pendingLabelMap.insert(std::pair<std::string,std::vector<content_type>>(arg2,std::vector<content_type>()));
										iter_pend=tmpPair.first;
									}
									iter_pend->second.push_back(assembly.size()-1);
								}else{
									std::cerr<<"Error: at line "<<lineCount<<": failed to interpret \""<<arg2<<"\" as value"<<std::endl;
									++errorCount;
								}
							}*/
							if(!immGood){
								std::cerr<<"Error: at line "<<lineCount<<": failed to interpret \""<<arg2<<"\" as value"<<std::endl;
								++errorCount;
							}else if(!(label.empty())){
								auto iter_pend=pendingLabelMap.find(label);
								if(iter_pend==pendingLabelMap.end()){
									auto tmpPair=pendingLabelMap.insert(
											std::pair<std::string,std::vector<std::pair<content_type,offset_type>>>(
												label,
												std::vector<std::pair<content_type,offset_type>>()
											)
									);
									iter_pend=tmpPair.first;
								}
								iter_pend->second.push_back(std::pair<content_type,offset_type>(assembly.size()-1,offset));
							}
						}else{
							assembly.push_back(PADD_NOOP);
							assembly.push_back(PADD_NOOP);
							std::cerr<<"Error: at line "<<lineCount<<": failed to interpret \""<<arg1<<"\" as register"<<std::endl;
							++errorCount;
						}
					}break;
					case INSTR_DATA:{
						content_type immediate=0;
						offset_type offset=0;
						std::string label;
						bool immGood=convert2Value_Expression(arg1,immediate,offset,label);
						assembly.push_back(immediate);
						if(!immGood){
							std::cerr<<"Error: at line "<<lineCount<<": failed to interpret \""<<arg1<<"\" as value"<<std::endl;
							++errorCount;
						}else if(!(label.empty())){
							auto iter_pend=pendingLabelMap.find(label);
							if(iter_pend==pendingLabelMap.end()){
								auto tmpPair=pendingLabelMap.insert(
										std::pair<std::string,std::vector<std::pair<content_type,offset_type>>>(
											label,
											std::vector<std::pair<content_type,offset_type>>()
										)
								);
								iter_pend=tmpPair.first;
							}
							iter_pend->second.push_back(std::pair<content_type,offset_type>(assembly.size()-1,offset));
						}
					}break;
					default:{
						assembly.push_back(PADD_NOOP);
						std::cerr<<"Error: at line "<<lineCount<<": opcode handling unimplemented"<<std::endl;
						++errorCount;
					}break;
				}
				isThisAddressLabelled=false;
			}
		}
	}
	if(isThisAddressLabelled){
		std::cerr<<"Warning: EOF reached; the last label is not labeling any instruction"<<std::endl;
	}
	//start to resolve labels
	for(auto iter=pendingLabelMap.begin();iter!=pendingLabelMap.end();++iter){
		auto iter_label=labelMap.find(iter->first);
		if(iter_label==labelMap.end()){
			std::cerr<<"Error: when resolving labels: label \""<<iter->first<<"\" is not found\n\tNote: This label is evaluated at following address:\n"<<std::hex;
			for(auto iter_victim=iter->second.begin();iter_victim!=iter->second.end();++iter_victim){
				std::cerr<<'\t'<<(iter_victim->first);
			}
			std::cerr<<std::endl;
		}else{
			for(auto iter_eval=iter->second.begin();iter_eval!=iter->second.end();++iter_eval){
				assembly[iter_eval->first]=iter_label->second+iter_eval->second;
			}
		}
	}
	
	if(assembly.size()>depth){
		std::cerr<<std::dec<<"Warning: size of assembly ("<<assembly.size()<<") is greater than depth ("<<depth<<") can store!"<<std::endl;
		while(depth<assembly.size()) depth<<=1;
		std::cerr<<"Info: depth changed to "<<depth<<std::endl;
	}
	
	
	//write mif file
	unsigned address_width=1;
	unsigned tmp_depth=depth;
	while(tmp_depth>16){
		++address_width;
		tmp_depth>>=4;
	}
	unsigned data_width=(width-1)/4+1;
	
	unsigned long long assembly_max=(1<<width)-1;
	
	//std::cerr<<"Debug: address_width="<<address_width<<",data_width="<<data_width<<",assembly_max="<<assembly_max<<std::endl;
	
	//output constants and labels
#ifdef OUTPUT_WRITE_SYMBOL_TABLE
	std::cout<<"-- Constants: "<<constantMap.size()<<" in total\n";
	for(auto iter_tmp=constantMap.begin();iter_tmp!=constantMap.end();++iter_tmp){
		std::cout<<"--\t"<<iter_tmp->first<<'\t'<<std::dec<<iter_tmp->second<<"\t0x"<<std::hex<<std::nouppercase<<iter_tmp->second<<'\n';
	}
	std::cout<<"-- Labels: "<<labelMap.size()<<" in total\n";
	std::multimap<content_type,std::string> tmpLabelMap;//sort my increasing address
	for(auto iter_tmp=labelMap.begin();iter_tmp!=labelMap.end();++iter_tmp){
		tmpLabelMap.insert(std::pair<content_type,std::string>(iter_tmp->second,iter_tmp->first));
	}
	for(auto iter_tmp=tmpLabelMap.begin();iter_tmp!=tmpLabelMap.end();++iter_tmp){
		std::cout<<"--\t"<<iter_tmp->second<<"\t0x"<<std::hex<<std::nouppercase<<iter_tmp->first<<'\n';
	}
	std::cout<<'\n';
#endif

	std::cout<<"DEPTH = "<<depth
			<<";\nWIDTH = "<<width
			<<";\nADDRESS_RADIX = HEX;\nDATA_RADIX = HEX;\nCONTENT\nBEGIN\n";
	
	std::cout<<std::hex<<std::setfill('0')<<std::uppercase;
	bool isOversizeNotReported=true;
	auto iter_labelComment=comment_label.begin();
	for(std::size_t i=0;i<assembly.size();++i){
		while((iter_labelComment!=comment_label.end())&&(iter_labelComment->second==i)){
			std::cout<<"-- Label \""<<iter_labelComment->first<<"\":\n";
			++iter_labelComment;
		}
		std::cout<<std::setw(address_width)<<i<<"\t:\t"<<std::setw(data_width)<<assembly[i]<<';';
		if(!(comment_code[i].empty())){
			std::cout<<"\t-- "<<comment_code[i];
		}
		std::cout<<'\n';
	}
#ifdef OUTPUT_ZERO_FILL
	if((depth-assembly.size())==1){
		std::cout<<std::setw(address_width)<<assembly.size()<<"\t:\t"<<std::setw(data_width)<<PADD_NOOP<<";\n";
	}else{
		std::cout<<'['<<std::setw(address_width)<<assembly.size()<<".."<<std::setw(address_width)<<depth-1<<"]\t:\t"<<std::setw(data_width)<<PADD_NOOP<<";\n";
	}
#endif
	std::cout<<"END;"<<std::endl;
#ifdef INFO_SHOW_ERRORCOUNT
	std::cerr<<"Output complete; "<<errorCount<<" error(s) in total"<<std::endl;
#endif
	return 0;
}

int main(int argc, char** argv){
	if(argc>2){
		std::cerr<<"Error: Too many arguments; only an optional DEPTH argument is needed"<<std::endl;
		return 0;
	}
	
	unsigned depth=128;
	unsigned width=16;
	
	if(argc==2){
		std::string arg_depth(argv[1]);
		//std::stoul sometimes do not work with MinGW on Windows
		/*
		try{
			depth=std::stoul(arg_depth,nullptr);
		}catch(...){
			std::cerr<<"Error: invalid DEPTH argument"<<std::endl;
			return 0;
		}*/
		std::stringstream buf(arg_depth);
		buf>>depth;
		if(buf.fail()){
			std::cerr<<"Error: invalid DEPTH argument"<<std::endl;
			return 0;
		}
	}
	
	return process(depth,width);
}