#pragma once
#include "RNA.h"

namespace NodeJS
{

class NodeJS : public BioSys::RNA
{
public:
	PUBLIC_API NodeJS(BioSys::IBiomolecule* owner);
	PUBLIC_API virtual ~NodeJS();

protected:
	virtual void OnEvent(const DynaArray& name);

private:
	String ssystem(const String& command);
};

}