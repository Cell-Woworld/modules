#pragma once
#include "RNA.h"

namespace ScriptAgent
{

class ScriptAgent : public BioSys::RNA
{
public:
	PUBLIC_API ScriptAgent(BioSys::IBiomolecule* owner);
	PUBLIC_API virtual ~ScriptAgent();

protected:
	virtual void OnEvent(const DynaArray& name);

private:
	String ssystem(const String& command);
};

}