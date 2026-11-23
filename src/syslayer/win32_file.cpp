#include "syslayer/win32_file.h"
#include <cerrno>
#include <filesystem>
#include <memory>
#include <fcntl.h>
#include <sys/stat.h>
#include <unistd.h>
namespace anyxbox {
namespace {
// Open each component relative to an owned directory descriptor. Never follow guest-path symlinks.
int rooted_open(const std::string& root, const std::string& path, FileMode mode, bool ro) {
  if (ro && mode != OPEN_EXISTING_RW_READ) return -1;
  if (!ro) { std::error_code ec; std::filesystem::create_directories(root,ec); if(ec) return -1; }
  int dir=::open(root.c_str(),O_RDONLY|O_DIRECTORY|O_CLOEXEC|O_NOFOLLOW);
  if(dir<0) return -1;
  size_t start=0;
  for (;;) {
    size_t end=path.find('/',start);
    std::string part=path.substr(start,end==std::string::npos?end:end-start);
    if(end==std::string::npos) {
      int flags=mode==OPEN_EXISTING_RW_READ?O_RDONLY:mode==CREATE_WRITE_TRUNCATE?O_RDWR|O_CREAT|O_TRUNC:O_WRONLY|O_CREAT|O_APPEND;
      int fd=::openat(dir,part.c_str(),flags|O_NOFOLLOW|O_CLOEXEC|O_NONBLOCK,0600);
      ::close(dir);
      struct stat st{};
      if(fd>=0 && (::fstat(fd,&st)!=0 || !S_ISREG(st.st_mode))) {::close(fd);return -1;}
      return fd;
    }
    if(mode!=OPEN_EXISTING_RW_READ && ::mkdirat(dir,part.c_str(),0700)!=0 && errno!=EEXIST) {::close(dir);return -1;}
    int next=::openat(dir,part.c_str(),O_RDONLY|O_DIRECTORY|O_NOFOLLOW|O_CLOEXEC);
    ::close(dir); if(next<0) return -1; dir=next; start=end+1;
  }
}
}
bool MappedFolder::valid() const {
  int fd=::open(root_.c_str(),O_RDONLY|O_DIRECTORY|O_NOFOLLOW|O_CLOEXEC);
  if(fd<0) return false;
  ::close(fd);return true;
}
bool MappedFolder::resolve(const std::string& vpath, std::string& host) const {
  if(vpath.empty() || vpath.find('\0')!=std::string::npos) return false;
  std::string p=vpath; for(auto& c:p) if(c=='\\') c='/';
  if(p.find(':')!=std::string::npos) return false;
  std::string rel;size_t i=0;
  while(i<=p.size()) {
    size_t j=p.find('/',i);if(j==std::string::npos) j=p.size();
    std::string part=p.substr(i,j-i);if(part=="..") return false;
    if(!part.empty() && part!=".") {if(!rel.empty()) rel+='/';rel+=part;}
    i=j+1;
  }
  if(rel.empty()) return false;
  host=root_+"/"+rel;return true;
}
bool MappedFolder::exists(const std::string& v) const {
  std::string h;if(!resolve(v,h)) return false;
  int fd=rooted_open(root_,h.substr(root_.size()+1),OPEN_EXISTING_RW_READ,true);
  if(fd<0) return false;
  ::close(fd);return true;
}
uint32_t MappedFolder::CreateFile(HandleTable& t,const std::string& v,FileMode mode) const {
  if(mode!=OPEN_EXISTING_RW_READ && mode!=CREATE_WRITE_TRUNCATE && mode!=OPEN_APPEND) return 0xFFFFFFFF;
  std::string h;if(!resolve(v,h)) return 0xFFFFFFFF;
  int fd=rooted_open(root_,h.substr(root_.size()+1),mode,read_only_);if(fd<0) return 0xFFFFFFFF;
  auto f=std::make_shared<FileObject>(fd,mode!=OPEN_APPEND,mode!=OPEN_EXISTING_RW_READ);
  return t.add(f);
}
FileObject::FileObject(int fd,bool rd,bool wr):fd_(fd),readable_(rd),writable_(wr),ok_(fd>=0) {}
FileObject::~FileObject(){if(fd_>=0) ::close(fd_);}
bool FileObject::read(void* b,uint32_t n,uint32_t* got) {
  std::lock_guard<std::mutex> l(m_);if(got) *got=0;if(!readable_) return false;
  ssize_t rc;do{rc=::read(fd_,b,n);}while(rc<0 && errno==EINTR);
  if(rc<0) return false;
  if(got) *got=(uint32_t)rc;
  return true;
}
bool FileObject::write(const void* b,uint32_t n,uint32_t* w) {
  std::lock_guard<std::mutex> l(m_);if(w) *w=0;if(!writable_) return false;
  uint32_t done=0;while(done<n) {ssize_t rc=::write(fd_,(const char*)b+done,n-done);if(rc<0 && errno==EINTR) continue;if(rc<=0) {if(w)*w=done;return false;}done+=(uint32_t)rc;}
  if(w)*w=done;
  return true;
}
bool FileObject::seek(int64_t off,int wh,uint64_t* np) {
  std::lock_guard<std::mutex> l(m_);if(wh<0 || wh>2) return false;
  off_t p=::lseek(fd_,off,wh==0?SEEK_SET:wh==1?SEEK_CUR:SEEK_END);if(p<0)return false;
  if(np)*np=(uint64_t)p;
  return true;
}
bool FileObject::size(uint64_t* sz) {
  std::lock_guard<std::mutex> l(m_);struct stat st{};if(::fstat(fd_,&st)!=0)return false;*sz=(uint64_t)st.st_size;return true;
}
static std::shared_ptr<FileObject> fo(const HandleTable& t,uint32_t h){return std::dynamic_pointer_cast<FileObject>(t.get(h));}
bool ReadFile(const HandleTable& t,uint32_t h,void* b,uint32_t n,uint32_t* g){auto o=fo(t,h);return o&&o->read(b,n,g);}
bool WriteFile(const HandleTable& t,uint32_t h,const void* b,uint32_t n,uint32_t* w){auto o=fo(t,h);return o&&o->write(b,n,w);}
bool SetFilePointer(const HandleTable& t,uint32_t h,int64_t off,int wh,uint64_t* np){auto o=fo(t,h);return o&&o->seek(off,wh,np);}
bool GetFileSize(const HandleTable& t,uint32_t h,uint64_t* sz){auto o=fo(t,h);return o&&o->size(sz);}
}
