#pragma once
#include "RNA.h"

namespace DockerAgent
{

class DockerAgent : public BioSys::RNA
{
public:
	PUBLIC_API DockerAgent(BioSys::IBiomolecule* owner);
	PUBLIC_API virtual ~DockerAgent();

protected:
	virtual void OnEvent(const DynaArray& name);

private:
	String ssystem(const String& command);
};

}