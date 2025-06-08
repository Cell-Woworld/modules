#pragma once
#include "RNA.h"

namespace Barcode
{

class Barcode : public BioSys::RNA
{
public:
	PUBLIC_API Barcode(BioSys::IBiomolecule* owner);
	PUBLIC_API virtual ~Barcode();

protected:
	virtual void OnEvent(const DynaArray& name);

private:
	static void stbi_handler_func(void* context, void* data, int size);
	static std::stringstream ss_;
private:
	String ssystem(const String& command);
};

}