// Strict launcher arguments. Guest argument forwarding is not implemented.
#pragma once
#include <string>
#include "runtime/run_pe.h"
namespace anyxbox {
struct LauncherOptions {
  RunOptions run;
  bool trust=false,window=false,gpu=false,controller=false;
  std::string path,save,error;
};
inline LauncherOptions parse_launcher_options(int argc,const char* const* argv){
  LauncherOptions o;bool positional=false;
  for(int i=1;i<argc;++i){
    std::string arg=argv[i];
    if(!positional && arg=="--"){positional=true;continue;}
    if(!positional && arg=="--i-built-this")o.trust=true;
    else if(!positional && arg=="--window")o.window=true;
    else if(!positional && arg=="--controller")o.controller=true;
    else if(!positional && arg=="--gpu")o.gpu=true;
    else if(!positional && (arg=="--asset-root" || arg=="--save-frame")){
      if(i+1>=argc || !argv[i+1][0] || std::string(argv[i+1]).rfind("--",0)==0){o.error="missing value for "+arg;return o;}
      std::string& value=arg=="--asset-root"?o.run.asset_root:o.save;
      if(!value.empty()){o.error="duplicate value option: "+arg;return o;}
      value=argv[++i];
    }else{
      if(!positional && arg.size()>1 && arg[0]=='-'){o.error="unknown launcher option: "+arg;return o;}
      if(arg.empty()){o.error="empty executable path";return o;}
      if(!o.path.empty()){o.error="multiple executable paths; guest arguments are not supported";return o;}
      o.path=arg;
    }
  }
  if(o.path.empty())o.error="missing executable path";
  else if(!o.trust)o.error="binary not marked as trusted";
  return o;
}
}
