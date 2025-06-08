#pragma once
#include "RNA.h"

namespace RestfulAgent
{

class RestfulAgent : public BioSys::RNA
{
public:
	PUBLIC_API RestfulAgent(BioSys::IBiomolecule* owner);
	PUBLIC_API virtual ~RestfulAgent();

protected:
	virtual void OnEvent(const DynaArray& name);
};

}