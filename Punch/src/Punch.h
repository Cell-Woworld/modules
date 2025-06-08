#pragma once
#include "RNA.h"

namespace Punch
{

class Punch : public BioSys::RNA
{
public:
	PUBLIC_API Punch(BioSys::IBiomolecule* owner);
	PUBLIC_API virtual ~Punch();

protected:
	virtual void OnEvent(const DynaArray& name);
};

}