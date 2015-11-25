
//
//  MikrotikAPITypes.h
//  WinboxMobile
//
//  Created by Joey Gentry on 2/11/10.
//  Copyright 2010 __MyCompanyName__. All rights reserved.
//

#include <vector>
#include <map>
#include <string>

#define DEBUG 0

#define NONE 0
#define DONE 1
#define TRAP 2
#define FATAL 3

class Sentence {
	std::vector<std::string> strWords;    // vecor of strings representing individual words
	int returnType;     // return type of sentence
	void Tokenize(const std::string &str, std::vector<std::string> &tokens, 
                            const std::string &delimiters = " ");
	
	
public:
	void SetReturnType(int returnTypeIn) { returnType = returnTypeIn; }
	int GetReturnType() { return returnType; }
	void AddWord(const std::string &strWordToAdd) { strWords.push_back(strWordToAdd); }
	void Clear() { strWords.clear(); returnType = 0; }
	int Length() { return strWords.size(); }
	std::string operator[](int index) { return strWords[index]; }
	std::string GetWord(int index) { return strWords[index]; }
	void GetMap(std::map<std::string, std::string> &sentenceMap);
	bool Print();
};


class Block {
	std::vector<Sentence> sentences;
	
public:
	int Length() { return sentences.size(); }
	void AddSentence(const Sentence &sentence);
	void Clear() { sentences.clear(); }
	Sentence operator[](int index) { return sentences[index]; }
	bool Print();
};
