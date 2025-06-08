#pragma once
#include "RNA.h"

namespace Converter
{

class Converter : public BioSys::RNA
{
public:
	PUBLIC_API Converter(BioSys::IBiomolecule* owner);
	PUBLIC_API virtual ~Converter();

protected:
	virtual void OnEvent(const DynaArray& name);

private:
	void Base64Encode(const String& input, String& output);
	void Base64Decode(const String& input, String& output);
	void Padding(const String& in, String& out);
	void Unpadding(const String& in, String& out);
	void ASCIIToHex(const String& in, String& out);
	void HexToASCII(const String& in, String& out);
	void EscapeJSON(const String& in, String& out);
	String toBase62(unsigned long long value);
	unsigned long long toBase10(String value);
};

}