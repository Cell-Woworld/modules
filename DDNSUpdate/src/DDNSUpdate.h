#pragma once
#include "RNA.h"

namespace DDNSUpdate
{

class DDNSUpdate : public BioSys::RNA
{
public:
	PUBLIC_API DDNSUpdate(BioSys::IBiomolecule* owner);
	PUBLIC_API virtual ~DDNSUpdate();

protected:
	virtual void OnEvent(const DynaArray& name);
};

}