/* +++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++
   Copyright (c) 2011-2014 The plumed team
   (see the PEOPLE file at the root of the distribution for a list of names)

   See http://www.plumed-code.org for more information.

   This file is part of plumed, version 2.

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
#include "Atoms.h"
#include "ActionAtomistic.h"
#include "MDAtoms.h"
#include "PlumedMain.h"
#include "tools/Pbc.h"
#include <algorithm>
#include <iostream>
#include <string>

using namespace std;

namespace PLMD {

/// We assume that charges and masses are constant along the simulation
/// Set this to false if you want to revert to the original (expensive) behavior
static const bool shareMassAndChargeOnlyAtFirstStep=true;

class PlumedMain;

Atoms::Atoms(PlumedMain&plumed):
  natoms(0),
  pbc(*new Pbc),
  energy(0.0),
  dataCanBeSet(false),
  collectEnergy(0.0),
  energyHasBeenSet(false),
  positionsHaveBeenSet(0),
  massesHaveBeenSet(false),
  chargesHaveBeenSet(false),
  boxHasBeenSet(false),
  forcesHaveBeenSet(0),
  virialHasBeenSet(false),
  massAndChargeOK(false),
  plumed(plumed),
  naturalUnits(false),
  timestep(0.0),
  forceOnEnergy(0.0),
  kbT(0.0)
{
  mdatoms=MDAtomsBase::create(sizeof(double));
}

Atoms::~Atoms(){
  if(actions.size()>0){
    std::cerr<<"WARNING: there is some inconsistency in action added to atoms, as some of them were not properly destroyed. This might indicate an internal bug!!\n";
  }
  delete mdatoms;
  delete &pbc;
}

void Atoms::startStep(){
  collectEnergy=false; energyHasBeenSet=false; positionsHaveBeenSet=0;
  massesHaveBeenSet=false; chargesHaveBeenSet=false; boxHasBeenSet=false;
  forcesHaveBeenSet=0; virialHasBeenSet=false; dataCanBeSet=true;
}

void Atoms::setBox(void*p){
  mdatoms->setBox(p);
  Tensor b; mdatoms->getBox(b); boxHasBeenSet=true;
}

void Atoms::setPositions(void*p){
  plumed_massert( dataCanBeSet ,"setPositions must be called after setStep in MD code interface");
  plumed_massert( p || gatindex.size()==0, "NULL position pointer with non-zero local atoms");
  mdatoms->setp(p); positionsHaveBeenSet=3;
}

void Atoms::setMasses(void*p){
  plumed_massert( dataCanBeSet ,"setMasses must be called after setStep in MD code interface");
  plumed_massert( p || gatindex.size()==0, "NULL mass pointer with non-zero local atoms");
  mdatoms->setm(p); massesHaveBeenSet=true;

}

void Atoms::setCharges(void*p){
  plumed_massert( dataCanBeSet, "setCharges must be called after setStep in MD code interface");
  plumed_massert( p || gatindex.size()==0, "NULL charges pointer with non-zero local atoms");
  mdatoms->setc(p); chargesHaveBeenSet=true;
}

void Atoms::setVirial(void*p){
  plumed_massert( dataCanBeSet ,"setVirial must be called after setStep in MD code interface");
  mdatoms->setVirial(p); virialHasBeenSet=true;
  
}

void Atoms::setEnergy(void*p){
  plumed_massert( dataCanBeSet ,"setEnergy must be called after setStep in MD code interface");
  MD2double(p,md_energy);
  md_energy*=MDUnits.getEnergy()/units.getEnergy();
  energyHasBeenSet=true;
}

void Atoms::setForces(void*p){
  plumed_massert( dataCanBeSet ,"setForces must be called after setStep in MD code interface");
  plumed_massert( p || gatindex.size()==0, "NULL force pointer with non-zero local atoms");
  forcesHaveBeenSet=3;
  mdatoms->setf(p);
}

void Atoms::setPositions(void*p,int i){
  plumed_massert( dataCanBeSet ,"setPositions must be called after setStep in MD code interface");
  plumed_massert( p || gatindex.size()==0, "NULL positions pointer with non-zero local atoms");
  mdatoms->setp(p,i); positionsHaveBeenSet++;
}

void Atoms::setForces(void*p,int i){
  plumed_massert( dataCanBeSet ,"setForces must be called after setStep in MD code interface");
  plumed_massert( p || gatindex.size()==0, "NULL force pointer with non-zero local atoms");
  mdatoms->setf(p,i); forcesHaveBeenSet++;
}

void Atoms::share(){
  std::set<AtomNumber> unique;
// At first step I scatter all the atoms so as to store their mass and charge
// Notice that this works with the assumption that charges and masses are
// not changing during the simulation!
  if(!massAndChargeOK && shareMassAndChargeOnlyAtFirstStep){
    shareAll();
    return;
  }
  if(dd && int(gatindex.size())<natoms){
    for(unsigned i=0;i<actions.size();i++) if(actions[i]->isActive()) {
      unique.insert(actions[i]->getUnique().begin(),actions[i]->getUnique().end());
    }
  }
  share(unique);
}

void Atoms::shareAll(){
  std::set<AtomNumber> unique;
  if(dd && int(gatindex.size())<natoms)
    for(int i=0;i<natoms;i++) unique.insert(AtomNumber::index(i));
  share(unique);
}

void Atoms::share(const std::set<AtomNumber>& unique){
  plumed_assert( positionsHaveBeenSet==3 && massesHaveBeenSet );
  mdatoms->getBox(box);
  mdatoms->getPositions(gatindex,positions);
// how many double per atom should be scattered:
  int ndata=3;
  if(!massAndChargeOK){
    ndata=5;
    mdatoms->getCharges(gatindex,charges);
    mdatoms->getMasses(gatindex,masses);
  }
  if(dd && int(gatindex.size())<natoms){
    if(dd.async){
      for(unsigned i=0;i<dd.mpi_request_positions.size();i++) dd.mpi_request_positions[i].wait();
      for(unsigned i=0;i<dd.mpi_request_index.size();i++)     dd.mpi_request_index[i].wait();
    }
    int count=0;
    for(std::set<AtomNumber>::const_iterator p=unique.begin();p!=unique.end();++p){
      if(dd.g2l[p->index()]>=0){
        dd.indexToBeSent[count]=p->index();
        dd.positionsToBeSent[ndata*count+0]=positions[p->index()][0];
        dd.positionsToBeSent[ndata*count+1]=positions[p->index()][1];
        dd.positionsToBeSent[ndata*count+2]=positions[p->index()][2];
        if(!massAndChargeOK){
          dd.positionsToBeSent[ndata*count+3]=masses[p->index()];
          dd.positionsToBeSent[ndata*count+4]=charges[p->index()];
        }
        count++;
      }
    }
    if(dd.async){
      dd.mpi_request_positions.resize(dd.Get_size());
      dd.mpi_request_index.resize(dd.Get_size());
      for(int i=0;i<dd.Get_size();i++){
        dd.mpi_request_index[i]=dd.Isend(&dd.indexToBeSent[0],count,i,666);
        dd.mpi_request_positions[i]=dd.Isend(&dd.positionsToBeSent[0],ndata*count,i,667);
      }
    }else{
      const int n=(dd.Get_size());
      vector<int> counts(n);
      vector<int> displ(n);
      vector<int> counts5(n);
      vector<int> displ5(n);
      dd.Allgather(count,counts);
      displ[0]=0;
      for(int i=1;i<n;++i) displ[i]=displ[i-1]+counts[i-1];
      for(int i=0;i<n;++i) counts5[i]=counts[i]*ndata;
      for(int i=0;i<n;++i) displ5[i]=displ[i]*ndata;
      dd.Allgatherv(&dd.indexToBeSent[0],count,&dd.indexToBeReceived[0],&counts[0],&displ[0]);
      dd.Allgatherv(&dd.positionsToBeSent[0],ndata*count,&dd.positionsToBeReceived[0],&counts5[0],&displ5[0]);
      int tot=displ[n-1]+counts[n-1];
      for(int i=0;i<tot;i++){
        positions[dd.indexToBeReceived[i]][0]=dd.positionsToBeReceived[ndata*i+0];
        positions[dd.indexToBeReceived[i]][1]=dd.positionsToBeReceived[ndata*i+1];
        positions[dd.indexToBeReceived[i]][2]=dd.positionsToBeReceived[ndata*i+2];
        if(!massAndChargeOK){
          masses[dd.indexToBeReceived[i]]      =dd.positionsToBeReceived[ndata*i+3];
          charges[dd.indexToBeReceived[i]]     =dd.positionsToBeReceived[ndata*i+4];
        }
      }
    }
  }
  virial.zero();
  for(unsigned i=0;i<gatindex.size();i++) forces[gatindex[i]].zero();
  for(unsigned i=getNatoms();i<positions.size();i++) forces[i].zero(); // virtual atoms
  forceOnEnergy=0.0;
}

void Atoms::wait(){
  dataCanBeSet=false; // Everything should be set by this stage
// How many double per atom should be scattered
  int ndata=3;
  if(!massAndChargeOK)ndata=5;

  if(dd){
    dd.Bcast(box,0);
  }
  pbc.setBox(box);

  if(collectEnergy) energy=md_energy;

  if(dd && int(gatindex.size())<natoms){
// receive toBeReceived
    Communicator::Status status;
    if(dd.async){
      int count=0;
      for(int i=0;i<dd.Get_size();i++){
        dd.Recv(&dd.indexToBeReceived[count],dd.indexToBeReceived.size()-count,i,666,status);
        int c=status.Get_count<int>();
        dd.Recv(&dd.positionsToBeReceived[ndata*count],dd.positionsToBeReceived.size()-ndata*count,i,667);
        count+=c;
      }
      for(int i=0;i<count;i++){
        positions[dd.indexToBeReceived[i]][0]=dd.positionsToBeReceived[ndata*i+0];
        positions[dd.indexToBeReceived[i]][1]=dd.positionsToBeReceived[ndata*i+1];
        positions[dd.indexToBeReceived[i]][2]=dd.positionsToBeReceived[ndata*i+2];
        if(!massAndChargeOK){
          masses[dd.indexToBeReceived[i]]      =dd.positionsToBeReceived[ndata*i+3];
          charges[dd.indexToBeReceived[i]]     =dd.positionsToBeReceived[ndata*i+4];
        }
      }
    }
    if(collectEnergy) dd.Sum(energy);
  }
// I take note that masses and charges have been set once for all
// at the beginning of the simulation.
  if(shareMassAndChargeOnlyAtFirstStep) massAndChargeOK=true;
}

void Atoms::updateForces(){
  plumed_assert( forcesHaveBeenSet==3 );
  if(forceOnEnergy*forceOnEnergy>epsilon){
     double alpha=1.0-forceOnEnergy;
     mdatoms->rescaleForces(gatindex,alpha);
  }
  mdatoms->updateForces(gatindex,forces);
  if( !plumed.novirial && dd.Get_rank()==0 ){
      plumed_assert( virialHasBeenSet );
      mdatoms->updateVirial(virial);
  }
}

void Atoms::setNatoms(int n){
  natoms=n;
  positions.resize(n);
  forces.resize(n);
  masses.resize(n);
  charges.resize(n);
  gatindex.resize(n);
  for(unsigned i=0;i<gatindex.size();i++) gatindex[i]=i;
}


void Atoms::add(const ActionAtomistic*a){
  actions.push_back(a);
}

void Atoms::remove(const ActionAtomistic*a){
  vector<const ActionAtomistic*>::iterator f=find(actions.begin(),actions.end(),a);
  plumed_massert(f!=actions.end(),"cannot remove an action registered to atoms");
  actions.erase(f);
}


void Atoms::DomainDecomposition::enable(Communicator& c){
  on=true;
  Set_comm(c.Get_comm());
  async=Get_size()<10;
}

void Atoms::setAtomsNlocal(int n){
  gatindex.resize(n);
  if(dd){
    dd.g2l.resize(natoms,-1);
    dd.positionsToBeSent.resize(n*5,0.0);
    dd.positionsToBeReceived.resize(natoms*5,0.0);
    dd.indexToBeSent.resize(n,0);
    dd.indexToBeReceived.resize(natoms,0);
  };
}

void Atoms::setAtomsGatindex(int*g,bool fortran){
  plumed_massert( g || gatindex.size()==0, "NULL gatindex pointer with non-zero local atoms");
  if(fortran){
      for(unsigned i=0;i<gatindex.size();i++) gatindex[i]=g[i]-1;
  } else {
      for(unsigned i=0;i<gatindex.size();i++) gatindex[i]=g[i];
  }
  for(unsigned i=0;i<dd.g2l.size();i++) dd.g2l[i]=-1;
  if(dd) for(unsigned i=0;i<gatindex.size();i++) dd.g2l[gatindex[i]]=i;
}

void Atoms::setAtomsContiguous(int start){
  for(unsigned i=0;i<gatindex.size();i++) gatindex[i]=start+i;
  for(unsigned i=0;i<dd.g2l.size();i++) dd.g2l[i]=-1;
  if(dd) for(unsigned i=0;i<gatindex.size();i++) dd.g2l[gatindex[i]]=i;
}

void Atoms::setRealPrecision(int p){
  delete mdatoms;
  mdatoms=MDAtomsBase::create(p);
}

int Atoms::getRealPrecision()const{
  return mdatoms->getRealPrecision();
}

void Atoms::MD2double(const void*m,double&d)const{
  plumed_assert(mdatoms); mdatoms->MD2double(m,d);
}
void Atoms::double2MD(const double&d,void*m)const{
  plumed_assert(mdatoms); mdatoms->double2MD(d,m);
}

void Atoms::updateUnits(){
  mdatoms->setUnits(units,MDUnits);
}

void Atoms::setTimeStep(void*p){
  MD2double(p,timestep);
}

double Atoms::getTimeStep()const{
  return timestep/units.getTime()*MDUnits.getTime();
}

void Atoms::setKbT(void*p){
  MD2double(p,kbT);
}

double Atoms::getKbT()const{
  return kbT/units.getEnergy()*MDUnits.getEnergy();
}


void Atoms::createFullList(int*n){
  vector<AtomNumber> fullListTmp;
  for(unsigned i=0;i<actions.size();i++) if(actions[i]->isActive())
    fullListTmp.insert(fullListTmp.end(),actions[i]->getUnique().begin(),actions[i]->getUnique().end());
  std::sort(fullListTmp.begin(),fullListTmp.end());
  int nn=std::unique(fullListTmp.begin(),fullListTmp.end())-fullListTmp.begin();
  fullList.resize(nn);
  for(int i=0;i<nn;++i) fullList[i]=fullListTmp[i].index();
  *n=nn;
}

void Atoms::getFullList(int**x){
  if(!fullList.empty()) *x=&fullList[0];
  else *x=NULL;
}

void Atoms::clearFullList(){
  fullList.resize(0);
}

void Atoms::init(){
// Default: set domain decomposition to NO-decomposition, waiting for
// further instruction
  if(dd){
    setAtomsNlocal(natoms);
    setAtomsContiguous(0);
  }
}

void Atoms::setDomainDecomposition(Communicator& comm){
  dd.enable(comm);
}

void Atoms::resizeVectors(unsigned n){
  positions.resize(n);
  forces.resize(n);
  masses.resize(n);
  charges.resize(n);
}

AtomNumber Atoms::addVirtualAtom(ActionWithVirtualAtom*a){
  unsigned n=positions.size();
  resizeVectors(n+1);
  virtualAtomsActions.push_back(a);
  return AtomNumber::index(n);
}

void Atoms::removeVirtualAtom(ActionWithVirtualAtom*a){
  unsigned n=positions.size();
  plumed_massert(a==virtualAtomsActions[virtualAtomsActions.size()-1],"virtual atoms should be destroyed in reverse creation order");
  resizeVectors(n-1);
  virtualAtomsActions.pop_back();
}

void Atoms::insertGroup(const std::string&name,const std::vector<AtomNumber>&a){
  plumed_massert(groups.count(name)==0,"group named "+name+" already exists");
  groups[name]=a;
}

void Atoms::removeGroup(const std::string&name){
  plumed_massert(groups.count(name)==1,"cannot remove group named "+name);
  groups.erase(name);
}

void Atoms::writeBinary(std::ostream&o)const{
  o.write(reinterpret_cast<const char*>(&positions[0][0]),natoms*3*sizeof(double));
  o.write(reinterpret_cast<const char*>(&box(0,0)),9*sizeof(double));
  o.write(reinterpret_cast<const char*>(&energy),sizeof(double));
}

void Atoms::readBinary(std::istream&i){
  i.read(reinterpret_cast<char*>(&positions[0][0]),natoms*3*sizeof(double));
  i.read(reinterpret_cast<char*>(&box(0,0)),9*sizeof(double));
  i.read(reinterpret_cast<char*>(&energy),sizeof(double));
  pbc.setBox(box);
}

double Atoms::getKBoltzmann()const{
  if(naturalUnits) return 1.0;
  else return kBoltzmann/units.getEnergy();
}

double Atoms::getMDKBoltzmann()const{
  if(naturalUnits) return 1.0;
  else return kBoltzmann/MDUnits.getEnergy();
}

}


