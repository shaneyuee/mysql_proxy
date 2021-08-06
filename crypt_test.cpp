#include <stdio.h>
#include <rpc/des_crypt.h>
#include <string>
#include <stdint.h>
#include <string.h>
#include <stdlib.h>

using namespace std;

static string StrToHex(const string &content)
{
	char buf[content.length()*2+2];
	for(size_t i=0; i<content.length(); i++)
		sprintf(buf+i*2, "%02X", content[i]);
	return string(buf, content.length()*2);
}

static string HexToStr(const string &hex)
{
	size_t newlen = hex.length()/2;
	char buf[newlen];
	for(size_t i=0; i<newlen; i++)
		buf[i] = strtoul(hex.substr(i*2, 2).c_str(), NULL, 16);
	return string(buf, newlen);
}


static string Encrypt(const string &key, const string &content)
{
	uint32_t length = content.length();
	string result = string((char*)&length, sizeof(uint32_t)) + content;
	if(result.length()&0x7)
	{
		static char spaces[] = "        ";
		result += string(spaces, 8-result.length()%8);
	}

	char *data = (char*)result.data();
	int iRet = ecb_crypt((char*)key.c_str(), data, result.length(), DES_ENCRYPT);
	if(!DES_FAILED(iRet))
	{
		return string(data, result.length());
	}
	return string();
}

static string Decrypt(const string &key, const string &content)
{
	string result = content;
	char *data = (char*)result.data();
	int iRet = ecb_crypt((char*)key.c_str(), data, result.length(), DES_DECRYPT);
	if(!DES_FAILED(iRet))
	{
		string str = string(data, result.length());
		if(str.length()>4)
		{
			uint32_t length = *(uint32_t*)str.data();
			if(length+4<=str.length())
			{
				return string(str.data()+sizeof(uint32_t), length);
			}
		}
	}
	return string();
}


int main () {

	//char buffer[] = "operating systems is fun";
	char buffer[] = "crazyshen is crazy";
	int bufSize = strlen(buffer);
	char key[] = "abcd1234";

	printf("Before setparity: %s\n", key);
	des_setparity(key);
	printf("After setparity %s\n", key);

	printf("Before encrypt:\n");
	printf("%s\n", buffer);	

	string str(buffer, bufSize);
	str = Encrypt(key, str);
	memcpy(buffer, str.data(), bufSize);

//	ecb_crypt( key, buffer, bufSize, DES_ENCRYPT); 

	printf("After encrypt:\n");
	printf("%s -- %s\n", buffer, StrToHex(buffer).c_str());	

	str = Decrypt(key, str);
	memcpy(buffer, str.data(), bufSize);
	//ecb_crypt( key, buffer, bufSize, DES_DECRYPT); 

	printf("After decrypt:\n");
	printf("%s\n", buffer);	


	return 0;
}
