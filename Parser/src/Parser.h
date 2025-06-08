#pragma once
#include "RNA.h"

namespace Parser
{

class Parser : public BioSys::RNA
{
public:
	PUBLIC_API Parser(BioSys::IBiomolecule* owner);
	PUBLIC_API virtual ~Parser();

protected:
	virtual void OnEvent(const DynaArray& name);

private:
	void Split(Array<String>& output, const String& input, const String& separator);
};

}