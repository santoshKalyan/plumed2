#include "PlumedConfig.h"
#include "PlumedException.h"
#include "PlumedCommunicator.h"
#include "CLTool.h"
#include "CLToolMain.h"
#include "CLToolRegister.h"
#include "Tools.h"
#include "DLLoader.h"
#include <string>
#include <cstdlib>
#include <cstdio>
#include <iostream>
#include <algorithm>

using namespace std;
using namespace PLMD;

CLToolMain::CLToolMain():
argc(0),
in(stdin),
out(stdout),
comm(*new PlumedCommunicator)
{
}

CLToolMain::~CLToolMain(){
  delete &comm;
}

#define CHECK_NULL(val,word) plumed_massert(val,"NULL pointer received in cmd(\"CLTool " + word + "\")");

void CLToolMain::cmd(const std::string& word,void*val){
  if(false){
  } else if(word=="setArgc"){
       CHECK_NULL(val,word);
       argc=*static_cast<int*>(val);
  } else if(word=="setArgv"){
       CHECK_NULL(val,word);
       char**v=static_cast<char**>(val);
       for(int i=0;i<argc;++i) argv.push_back(string(v[i]));
  } else if(word=="setArgvLine"){
       CHECK_NULL(val,word);
       const char*v=static_cast<char*>(val);
       argv=Tools::getWords(v);
  } else if(word=="setIn"){
       CHECK_NULL(val,word);
       in=static_cast<FILE*>(val);
  } else if(word=="setOut"){
       CHECK_NULL(val,word);
       out=static_cast<FILE*>(val);
  } else if(word=="setMPIComm"){
       comm.Set_comm(val);
  } else if(word=="setMPIFComm"){
       comm.Set_fcomm(val);
  } else if(word=="run"){
       CHECK_NULL(val,word);
       argc=argv.size();
       char**v=new char* [argc];
       for(int i=0;i<argc;++i){
         v[i]=new char [argv[i].length()+1];
         for(unsigned c=0;c<argv[i].length();++c) v[i][c]=argv[i][c];
         v[i][argv[i].length()]=0;
       }
       int ret=run(argc,v,in,out,comm);
       for(int i=0;i<argc;++i) delete [] v[i];
       delete [] v;
       *static_cast<int*>(val)=ret;
  } else {
    plumed_merror("cannot interpret cmd(\"CLTool " + word + "\"). check plumed developers manual to see the available commands.");
  }
}

/**
This is the entry point to the command line tools
included in the plumed library.
*/

int CLToolMain::run(int argc, char **argv,FILE*in,FILE*out,PlumedCommunicator& pc){
  int i;
  bool printhelp=false;

  DLLoader dlloader;

  string root=plumedRoot;
  int err=setenv("PLUMED_ROOT",root.c_str(),1);
  plumed_massert(err==0,"error setting environment variable");

// Check if PLUMED_ROOT/patches/ directory exists (as a further check)
  {
    vector<string> files=Tools::ls(root);
    if(find(files.begin(),files.end(),"patches")==files.end()) {
      string msg=
         "ERROR: I cannot find $PLUMED_ROOT/patches/ directory\n"
         "Check your PLUMED_ROOT variable\n";
      fprintf(stderr,"%s",msg.c_str());
      return 1;
    }
  }

// Start parsing options
  string prefix("");
  string a("");
  for(i=1;i<argc;i++){
    a=prefix+argv[i];
    if(a.length()==0) continue;
    if(a=="help" || a=="-h" || a=="--help"){
      printhelp=true;
      break;
    } else if(a=="--has-mpi"){
      if(PlumedCommunicator::initialized()) return 0;
      else return 1;
    } else if(a=="--no-mpi"){
// this is ignored, as it is parsed in main
      if(i>1){
        fprintf(stderr,"--no-mpi option should can only be used as the first option");
        return 1;
      }
    } else if(a.find("--load=")==0){
      a.erase(0,a.find("=")+1);
      prefix="";
      void *p=dlloader.load(a);
      if(!p){
        fprintf(stderr,"ERROR: cannot load library %s\n",a.c_str());
        fprintf(stderr,"ERROR: %s\n",dlloader.error().c_str());
        return 1;
      }
    } else if(a=="--load"){
      prefix="--load=";
    } else if(a[0]=='-') {
      string msg="ERROR: Unknown option " +a;
      fprintf(stderr,"%s\n",msg.c_str());
      return 1;
    } else break;
  }
// Build list of available C++ tools:
  vector<string> availableCxx=cltoolRegister().list();
// Build list of available shell tools:
  vector<string> availableShell;
  {
    vector<string> tmp;
    tmp=Tools::ls(string(root+"/scripts"));
    for(unsigned j=0;j<tmp.size();++j){
      size_t ff=tmp[j].find(".sh");
      if(ff==string::npos) tmp[j].erase();
      else                 tmp[j].erase(ff);
    }
    for(unsigned j=0;j<tmp.size();++j) if(tmp[j].length()>0) availableShell.push_back(tmp[j]);
  }

 if(printhelp){
    string msg=
        "Usage: plumed [options] [command] [command options]\n"
        "  plumed [command] -h : to print help for a specific command\n"
        "Options:\n"
        "  [help|-h|--help]    : to print this help\n"
        "  [--has-mpi]         : fails if plumed is running with MPI\n"
        "  [--load LIB]        : loads a shared object (typically a plugin library)\n"
        "Commands:\n";
    fprintf(out,"%s",msg.c_str());
    for(unsigned j=0;j<availableCxx.size();++j){
      CLTool *cl=cltoolRegister().create(availableCxx[j]);
      plumed_assert(cl);
      string manual=availableCxx[j]+" : "+cl->description();
      delete cl;
      fprintf(out,"  plumed %s\n", manual.c_str());
    }
    for(unsigned j=0;j<availableShell.size();++j){
      string cmd=root+"/scripts/"+availableShell[j]+".sh --description";
      FILE *fp=popen(cmd.c_str(),"r");
      string line,manual;
      while(Tools::getline(fp,line))manual+=line;
      pclose(fp);
      manual= availableShell[j]+" : "+manual;
      fprintf(out,"  plumed %s\n", manual.c_str());
    }
    return 0;
  }
  if(i==argc) {
    fprintf(out,"%s","Nothing to do. Use 'plumed help' for help\n");
    return 0;
  }

// this is the command to be executed:
  string command(argv[i]);

  if(find(availableCxx.begin(),availableCxx.end(),command)!=availableCxx.end()){
    CLTool *cl=cltoolRegister().create(command);
    plumed_assert(cl);
    int ret=cl->main(argc-i,&argv[i],in,out,pc);
    delete cl;
    return ret;
  }

  if(find(availableShell.begin(),availableShell.end(),command)!=availableShell.end()){
    plumed_massert(in==stdin,"shell tools can only work on stdin");
    plumed_massert(out==stdout,"shell tools can only work on stdin");
    string cmd=root+"/scripts/"+command+".sh";
    for(int j=i+1;j<argc;j++) cmd+=string(" ")+argv[j];
    system(cmd.c_str());
    return 0;
  }

  string msg="ERROR: unknown command " + command;
  fprintf(stderr,"%s\n",msg.c_str());
  return 1;

}