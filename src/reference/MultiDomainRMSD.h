/* +++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++
   Copyright (c) 2012 The plumed team
   (see the PEOPLE file at the root of the distribution for a list of names)

   See http://www.plumed-code.org for more information.

   This file is part of plumed, version 2.0.

   plumed is free software: you can redistribute it and/or modify
   it under the terms of the GNU Lesser General Public License as published by
   the Free Software Foundation, either version 3 of the License, or
   (at your option) any later version.

   plumed is distributed in the hope that it will be useful,
   but WITHOUT ANY WARRANTY; without even the implied warranty of
   MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
   GNU Lesser General Public License for more details.

   You should have received a copy of the GNU Lesser General Public License
   along with plumed.  If not, see <http://www.gnu.org/licenses/>.
+++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++ */
#ifndef __PLUMED_reference_MultiDomainRMSD_h
#define __PLUMED_reference_MultiDomainRMSD_h

#include "SingleDomainRMSD.h"

namespace PLMD {

class Pbc;

class MultiDomainRMSD : public ReferenceAtoms {
private:
/// The type of RMSD we are using
  std::string ftype;
/// The weight of a block
  std::vector<double> weights;
/// Blocks containing start and end points for all the domains
  std::vector<unsigned> blocks;
/// Each of the domains we are calculating the distance from
  std::vector<SingleDomainRMSD*> domains;
public:
  MultiDomainRMSD( const ReferenceConfigurationOptions& ro );
  ~MultiDomainRMSD();
/// Read in the input from a pdb
  void read( const PDB& );
/// Calculate
  double calc( const std::vector<Vector>& pos, const Pbc& pbc, const std::vector<Value*>& vals, const std::vector<double>& arg, const bool& squared );
  double calculate( const std::vector<Vector>& pos, const Pbc& pbc,  const bool& squared );
};

}

#endif