#include "MikrotikAPI.h"

#include <stdexcept>
#include <sstream>

#include <cstring>
#include <cstdio>

#include <stdlib.h>
#include <unistd.h>

using namespace std;

MikrotikAPI::MikrotikAPI(const string &strIpAddress, const string &strUsername, 
                                    const string &strPassword, int port)
{	
	Connect(strIpAddress, port);
	
	if(fdSock != -1) {
		
		// attempt login
		int loginResult = Login(strUsername, strPassword);
		
		if (!loginResult) {
			throw NOLOGIN;
			Disconnect();
		}
	} else {
		throw NOCONNECT;
	}
}

MikrotikAPI::~MikrotikAPI()
{
	if(fdSock != -1)
		Disconnect();
}

/********************************************************************
 * Connect to API
 * Returns a socket descriptor
 ********************************************************************/
void MikrotikAPI::Connect(const string &strIpAddress, int port)
{
	struct sockaddr_in address;
	int connectResult;
	int addressSize;
	
	fdSock = socket(AF_INET, SOCK_STREAM, 0);
	
	address.sin_family = AF_INET;
	address.sin_addr.s_addr = inet_addr(strIpAddress.c_str());
	address.sin_port = htons(port);
	addressSize = sizeof(address);
	
	connectResult = connect(fdSock, (struct sockaddr *)&address, addressSize);
	
	if(connectResult==-1) {
		Disconnect();
		fdSock = -1;
	}
	
	// determine endianness of this machine
	// iLittleEndian will be set to 1 if we are
	// on a little endian machine...otherwise
	// we are assumed to be on a big endian processor
	littleEndian = IsLittleEndian();
}

/********************************************************************
 * Disconnect from API
 * Close the API socket
 ********************************************************************/
void MikrotikAPI::Disconnect()
{
	if(fdSock != -1) {
	
		close(fdSock);
	}
}

/********************************************************************
 * Login to the API
 * 1 is returned on successful login
 * 0 is returned on unsuccessful login
 ********************************************************************/
int MikrotikAPI::Login(const string &strUsername, const string &strPassword)
{
	Sentence readSentence;
	Sentence writeSentence;
	
	md5_state_t state;
	md5_byte_t digest[16];
	char cNull[1] = {0};
	
	//Send login message
	WriteWord("/login");
	WriteWord(cNull);
	
	ReadSentence(readSentence);
	
	if (readSentence.GetReturnType() != DONE) {
           throw std::runtime_error {"return type is not DONE"};
	} else {
	
		// extract md5 string from the challenge sentence
		char *strWord = new char [readSentence[1].size() + 1];
		strcpy(strWord, readSentence[1].c_str());
		char *md5Challenge = strtok(strWord, "=");	
		md5Challenge = strtok(NULL, "=");
		
		////Place of interest: Check to see if this md5Challenge string works as as string. 
                //   It may not because it needs to be binary.
		// convert szMD5Challenge to binary
		string md5ChallengeBinary = MD5ToBinary(md5Challenge);
		delete[] strWord;
		
		// get md5 of the password + challenge concatenation
		md5_init(&state);
		md5_append(&state, (const md5_byte_t *)cNull, 1);
		md5_append(&state, (const md5_byte_t *)strPassword.c_str(), 
                                 strlen(strPassword.c_str()));
		md5_append(&state, (const md5_byte_t *)md5ChallengeBinary.c_str(), 16);
		md5_finish(&state, digest);
		
		// convert this digest to a string representation of the hex values
		// digest is the binary format of what we want to send
		// szMD5PasswordToSend is the "string" hex format
		string md5PasswordToSend = MD5DigestToHexString(digest);
		
		// put together the login sentence
		writeSentence.AddWord("/login");
		writeSentence.AddWord("=name=" + strUsername);
		writeSentence.AddWord("=response=00" + md5PasswordToSend);
		
		WriteSentence(writeSentence);	
		ReadSentence(readSentence);
		
		if (readSentence.GetReturnType() == DONE) {
			return 1;
		} 
	}
	return 0;
}

/********************************************************************
 * Encode message length and write it out to the socket
 ********************************************************************/
void MikrotikAPI::WriteLength(int messageLength)
{
	char *encodedLengthData;  // encoded length to send to the api socket
	char *lengthData;     // exactly what is in memory at &iLen integer
	
	encodedLengthData = (char *)calloc(sizeof(int), 1);
	
	// set cLength address to be same as messageLength
	lengthData = (char *)&messageLength;
	
	// write 1 byte
	if (messageLength < 0x80) {
		encodedLengthData[0] = (char)messageLength;
		write (fdSock, encodedLengthData, 1);
	} else if (messageLength < 0x4000) { // write 2 bytes
		
		if (littleEndian) {
			encodedLengthData[0] = lengthData[1] | 0x80;
			encodedLengthData[1] = lengthData[0];
		} else {
			encodedLengthData[0] = lengthData[2] | 0x80;
			encodedLengthData[1] = lengthData[3];
		}
		
		write (fdSock, encodedLengthData, 2);
	} else if (messageLength < 0x200000) { // write 3 bytes
		
		if (littleEndian) {
			encodedLengthData[0] = lengthData[2] | 0xc0;
			encodedLengthData[1] = lengthData[1];
			encodedLengthData[2] = lengthData[0];
		} else {
			encodedLengthData[0] = lengthData[1] | 0xc0;
			encodedLengthData[1] = lengthData[2];
			encodedLengthData[2] = lengthData[3];
		}
		
		write (fdSock, encodedLengthData, 3);
	} else if (messageLength < 0x10000000) { // write 4 bytes (untested)
		
		if (littleEndian) {
			encodedLengthData[0] = lengthData[3] | 0xe0;
			encodedLengthData[1] = lengthData[2];
			encodedLengthData[2] = lengthData[1];
			encodedLengthData[3] = lengthData[0];
		} else {
			encodedLengthData[0] = lengthData[0] | 0xe0;
			encodedLengthData[1] = lengthData[1];
			encodedLengthData[2] = lengthData[2];
			encodedLengthData[3] = lengthData[3];
		}
		
		write (fdSock, encodedLengthData, 4);
	} else  { // this should never happen
           std::stringstream out;
           out << "Length of word is " << messageLength << " Word is too long.";
           throw std::runtime_error {out.str()};
	}
	
	delete [] encodedLengthData;	
}

/********************************************************************
 * Write a word to the socket
 ********************************************************************/
void MikrotikAPI::WriteWord(const string &strWord)
{
	WriteLength(strWord.length());
	write(fdSock, strWord.c_str(), strWord.length());
}

/********************************************************************
 * Write a Sentence (multiple words) to the socket
 ********************************************************************/
void MikrotikAPI::WriteSentence(Sentence &writeSentence)
{
	if (writeSentence.Length() == 0) {
		return;
	}
	
	for (int i = 0; i < writeSentence.Length(); ++i) {
		WriteWord(writeSentence[i]);
	}
	
	WriteWord("\0");
}

/********************************************************************
 * Read a message length from the socket
 * 
 * 80 = 10000000 (2 character encoded length)
 * C0 = 11000000 (3 character encoded length)
 * E0 = 11100000 (4 character encoded length)
 *
 * Message length is returned
 ********************************************************************/
int MikrotikAPI::ReadLength()
{
	char firstChar; // first character read from socket
	char *lengthData;   // length of next message to read...will be cast to int at the end
	int *messageLength;       // calculated length of next message (Cast to int)
	
	lengthData = (char *) calloc(sizeof(int), 1);
	read(fdSock, &firstChar, 1);
	

	
	// read 4 bytes
	// this code SHOULD work, but is untested...
	if ((firstChar & 0xE0) == 0xE0) {
		
		if (littleEndian){
			lengthData[3] = firstChar;
			lengthData[3] &= 0x1f;        // mask out the 1st 3 bits
			read(fdSock, &lengthData[2], 1);
			read(fdSock, &lengthData[1], 1);
			read(fdSock, &lengthData[0], 1);
		} else {
			lengthData[0] = firstChar;
			lengthData[0] &= 0x1f;        // mask out the 1st 3 bits
			read(fdSock, &lengthData[1], 1);
			read(fdSock, &lengthData[2], 1);
			read(fdSock, &lengthData[3], 1);
		}
		
		messageLength = (int *)lengthData;
	} else if ((firstChar & 0xC0) == 0xC0) { // read 3 bytes
		
		if (littleEndian) {
			lengthData[2] = firstChar;
			lengthData[2] &= 0x3f;        // mask out the 1st 2 bits
			read(fdSock, &lengthData[1], 1);
			read(fdSock, &lengthData[0], 1);
		} else {
			lengthData[1] = firstChar;
			lengthData[1] &= 0x3f;        // mask out the 1st 2 bits
			read(fdSock, &lengthData[2], 1);
			read(fdSock, &lengthData[3], 1);
		}
		
		messageLength = (int *)lengthData;
	} else if ((firstChar & 0x80) == 0x80) { // read 2 bytes
		
		if (littleEndian) {
			lengthData[1] = firstChar;
			lengthData[1] &= 0x7f;        // mask out the 1st bit
			read(fdSock, &lengthData[0], 1);
		} else {
			lengthData[2] = firstChar;
			lengthData[2] &= 0x7f;        // mask out the 1st bit
			read(fdSock, &lengthData[3], 1);
		}
		
		messageLength = (int *)lengthData;
	} else { // assume 1-byte encoded length...same on both LE and BE systems
		messageLength = (int *) malloc(sizeof(int));
		*messageLength = (int)firstChar;
	}
	
	int retMessageLength = *messageLength;
	delete messageLength;
	delete [] lengthData;
	
	return retMessageLength;
}

/********************************************************************
 * Read a word from the socket
 * The word that was read is returned as a string
 ********************************************************************/
void MikrotikAPI::ReadWord(string &strWordOut)
{
	int messageLength = ReadLength();
	int bytesToRead = 0;
	int bytesRead = 0;
	
	char *tmpWord;
	
	
	strWordOut.clear();
	if (messageLength > 0) {
		// allocate memory for strings
		tmpWord = (char *) calloc(sizeof(char), 1024 + 1);
		
		while (messageLength != 0) {
			// determine number of bytes to read this time around
			// lesser of 1024 or the number of byes left to read
			// in this word
			bytesToRead = messageLength > 1024 ? 1024 : messageLength;
			
			// read iBytesToRead from the socket
			bytesRead = read(fdSock, tmpWord, bytesToRead);
			
			// terminate szTmpWord
			tmpWord[bytesRead] = 0;
			
			// concatenate szTmpWord to szRetWord
			strWordOut += tmpWord;
			
			// subtract the number of bytes we just read from iLen
			messageLength -= bytesRead;
		}		
		
		// deallocate szTmpWord
		delete [] tmpWord;
		
	} 
}

/********************************************************************
 * Read a Sentence from the socket
 * A Sentence struct is returned
 ********************************************************************/
void MikrotikAPI::ReadSentence(Sentence &sentenceOut)
{	
	sentenceOut.Clear();
	
	string strWord;
	ReadWord(strWord);
	while (!strWord.empty()) {
		sentenceOut.AddWord(strWord);
		
		// check to see if we can get a return value from the API
		if (strWord.find("!done") != string::npos) {
			sentenceOut.SetReturnType(DONE);
		} else if (strWord.find("!trap") != string::npos) {
			sentenceOut.SetReturnType(TRAP);
		} else if (strWord.find("!fatal") != string::npos) {
			sentenceOut.SetReturnType(FATAL);
		}
		
		ReadWord(strWord);
	}
	
	// if any errors, get the next sentence
	if (sentenceOut.GetReturnType() == TRAP || sentenceOut.GetReturnType() == FATAL) {
		ReadSentence(sentenceOut);
	}
}

/********************************************************************
 * Read Sentence Block from the socket...keeps reading sentences
 * until it encounters !done, !trap or !fatal from the socket
 ********************************************************************/
void MikrotikAPI::ReadBlock(Block &block)
{
	Sentence sentence;
        block.Clear();
	
	do {
		ReadSentence(sentence);
		block.AddSentence(sentence);
	} while (sentence.GetReturnType() == NONE);
}


/********************************************************************
 * MD5 helper function to convert an md5 hex char representation to
 * binary representation.
 ********************************************************************/
string MikrotikAPI::MD5ToBinary(const string &strHex)
{
	string strReturn;
	
	// 32 bytes in szHex?
	if (strHex.length() != 32) {
		return strReturn;
	}
	
	char binWork[3];
	for (int i = 0; i < 32; i += 2) {
		binWork[0] = strHex[i];
		binWork[1] = strHex[i + 1];
		binWork[2] = 0;
		
		strReturn[i / 2] = HexStringToChar(binWork);
	}
	
	return strReturn;
}

/********************************************************************
 * MD5 helper function to calculate and return hex representation
 * of an MD5 digest stored in binary.
 ********************************************************************/
string MikrotikAPI::MD5DigestToHexString(md5_byte_t *binaryDigest)
{
	char strReturn[32 + 1];
	
	for (int i = 0; i < 16; ++i) {
		sprintf(strReturn + i * 2, "%02x", binaryDigest[i]);
	}
	
	return strReturn;
}

/********************************************************************
 * Quick and dirty function to convert hex string to char...
 * the toConvert string MUST BE 2 characters + null terminated.
 ********************************************************************/
char MikrotikAPI::HexStringToChar(const string &hexToConvert)
{
	unsigned int accumulated = 0;
	char char0[2] = {hexToConvert[0], 0};
	char char1[2] = {hexToConvert[1], 0};
	
	// look @ first char in the 16^1 place
	if (hexToConvert[0] == 'f' || hexToConvert[0] == 'F') {
		accumulated += 16*15;
	} else if (hexToConvert[0] == 'e' || hexToConvert[0] == 'E') {
		accumulated += 16*14;
	} else if (hexToConvert[0] == 'd' || hexToConvert[0] == 'D') {
		accumulated += 16*13;
	} else if (hexToConvert[0] == 'c' || hexToConvert[0] == 'C') {
		accumulated += 16*12;
	} else if (hexToConvert[0] == 'b' || hexToConvert[0] == 'B') {
		accumulated += 16*11;
	} else if (hexToConvert[0] == 'a' || hexToConvert[0] == 'A') {
		accumulated += 16*10;
	} else {
		accumulated += 16 * atoi(char0);
	}
	
	// now look @ the second car in the 16^0 place
	if (hexToConvert[1] == 'f' || hexToConvert[1] == 'F') {
		accumulated += 15;
	} else if (hexToConvert[1] == 'e' || hexToConvert[1] == 'E') {
		accumulated += 14;
	} else if (hexToConvert[1] == 'd' || hexToConvert[1] == 'D') {
		accumulated += 13;
	} else if (hexToConvert[1] == 'c' || hexToConvert[1] == 'C') {
		accumulated += 12;
	} else if (hexToConvert[1] == 'b' || hexToConvert[1] == 'B') {
		accumulated += 11;
	} else if (hexToConvert[1] == 'a' || hexToConvert[1] == 'A') {
		accumulated += 10;
	} else {
		accumulated += atoi(char1);
	}
	
	return (char)accumulated;	
}

/********************************************************************
 * Test whether or not this system is little endian at RUNTIME
 * Courtesy: http://download.osgeo.org/grass/grass6_progman/endian_8c_source.html
 ********************************************************************/
bool MikrotikAPI::IsLittleEndian()
{
	union {
		int testWord;
		char testByte[sizeof(int)];
	} endianTest;
	
	endianTest.testWord = 1;
	
	if (endianTest.testByte[0] == 1)
		return 1;               /* true: little endian */
	
	return 0;                   /* false: big endian */
}
