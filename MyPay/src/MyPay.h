#pragma once
#include "RNA.h"

namespace MyPay
{

class MyPay : public BioSys::RNA
{
public:
	PUBLIC_API MyPay(BioSys::IBiomolecule* owner);
	PUBLIC_API virtual ~MyPay();

protected:
	virtual void OnEvent(const DynaArray& name);

private:
	void Base64Encode(const String& input, String& output);
	void CopyAndPatch(const String& in, String& out);
};

}