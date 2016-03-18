
//
//  untitled.mm
//  WinboxMobile
//
//  Created by Joey Gentry on 2/13/10.
//  Copyright 2010 __MyCompanyName__. All rights reserved.
//

#include "MikrotikAPITypes.h"
#include <cstdio>

using namespace std;

/********************************************************************
 * Print a sentence.
 ********************************************************************/
bool Sentence::Print()
{
	for (unsigned i = 0; i < strWords.size(); ++i) {
		printf("%s\n", strWords[i].c_str());
	}
	
	printf("\n");
	
	return true;
}

void Sentence::GetMap(map<string, string> &sentenceMap)
{
	for (unsigned i = 0; i < strWords.size(); ++i) {
		string tmpDataString = strWords[i];
		vector<string> dataStrings;
		Tokenize(tmpDataString, dataStrings, "=");

		if (returnType == NONE && dataStrings.size() > 1) {
			sentenceMap.insert(make_pair(dataStrings[0], dataStrings[1]));
		}
	}
}

void Sentence::Tokenize(const string &str, vector<string> &tokens, const string &delimiters)
{
	// Skip delimiters at beginning.
    string::size_type lastPos = str.find_first_not_of(delimiters, 0);
    // Find first "non-delimiter".
    string::size_type pos     = str.find_first_of(delimiters, lastPos);
	
    while (string::npos != pos || string::npos != lastPos)
    {
        // Found a token, add it to the vector.
        tokens.push_back(str.substr(lastPos, pos - lastPos));
        // Skip delimiters.  Note the "not_of"
        lastPos = str.find_first_not_of(delimiters, pos);
        // Find next "non-delimiter"
        pos = str.find_first_of(delimiters, lastPos);
    }
}

/********************************************************************
 * Add a Sentence to a block
 ********************************************************************/
void Block::AddSentence(const Sentence &sentence)
{	
	sentences.push_back(sentence);
}

/********************************************************************
 * Print a block.
 ********************************************************************/
bool Block::Print()
{
	for (unsigned i = 0; i < sentences.size(); ++i) {
		sentences[i].Print();
	}
	
	return true;
}
