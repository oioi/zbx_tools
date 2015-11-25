/********************************************************************
 * Some definitions
 * Word = piece of API code
 * Sentence = multiple words
 * Block = multiple sentences (usually in response to a Sentence request)
 * 
 
 try {
	MikrotikAPI mt = MikrotikAPI("64.126.135.214", "test", "joey", 8728);
 
	Sentence sentence;
	Block block;
 
	// Fill and send sentence to the API
	sentence.AddWord("/interface/getall");
	mt.WriteSentence(sentence);
 
	// receive and print block from the API
	mt.ReadBlock(block);
	block.Print();
 } catch (int e) {
	if(e == NOCONNECT)
		printf("Could not connect.\n");
	if(e == NOLOGIN)
		printf("Could not login.\n");
 }
 
 
 Original Author: Joey Gentry (www.Murderdev.com)
 Feel freel to ask me questions but I can't guarantee I will be able to answer them all. 
 No warranties are provided with this
 code. This was written/converted for my iPhone app to allow accessing a Mikrotik router. 
 The app is called Winbox Mobile for those who are interested.
 
 This is written in C++. The code is based highly on the code from 
  http://wiki.mikrotik.com/wiki/API_in_C. I like the way this was done in respect to how easy 
  it is to send a command and get a block of sentences back that are easily parsed.
 
 I have removed all the memory leaks and converted it entirely to C++. There is only a few 
 places using any memory allocation and that is mostly in the encoding as its much easier 
 to do with dynamic char arrays. I have made it so it can be compiled in Xcode for use in 
 Obj C++ and should work fine in any other platform with little or no extra work.
 
 This implementation relies on the MD5 digest calculation functions written by 
 Aladdin Enterprises http://sourceforge.net/projects/libmd5-rfc/files/. An endian test 
 (big/little endian) is also used courtesy GRASS Development Team  
 http://download.osgeo.org/grass/grass6_progman/endian_8c.html. All functions/libraries used 
 from other sources are available under open licenses such as GNU Public License.
 
 Features:
 Written using C++
 Leak Free
 Supports *nix Platforms including Mac
 Sentences will return a map object (so no parsing needed really)
 
 ********************************************************************/

#include<sys/socket.h>
#include<arpa/inet.h>
#include <vector>
#include <string>
#include "md5.h"
#include "MikrotikAPITypes.h"

#define NOCONNECT 1
#define NOLOGIN 2

class MikrotikAPI  {
	private:
		int fdSock;
		bool littleEndian;
	
		bool IsLittleEndian();
	
		void Connect(const std::string &strIpAddress, int port);
		void Disconnect();
	
		// Word
		void WriteLength(int messageLength);
		int ReadLength();
		void WriteWord(const std::string &strWord);
		void ReadWord(std::string &strWordOut);
	
		// MD5 helper functions
		std::string MD5DigestToHexString(md5_byte_t *binaryDigest);
		std::string MD5ToBinary(const std::string &strHex);
		char HexStringToChar(const std::string &hexToConvert);
	
	public:
	MikrotikAPI();
		MikrotikAPI(const std::string &strIpAddress, const std::string &strUsername, 
                                const std::string &strPassword, int port);
		~MikrotikAPI();
	
		// API specific functions
		int Login(const std::string &strUsername, const std::string &strPassword);
		
		// Sentence
		void WriteSentence(Sentence &writeSentence);
		void ReadSentence(Sentence &sentenceOut);
		
		// Block
		void ReadBlock(Block &block);	
};
