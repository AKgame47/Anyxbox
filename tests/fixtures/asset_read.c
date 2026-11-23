/* Self-written file-backed launch fixture. No third-party data. */
#include <windows.h>
void mainEntry(void) {
  const char expected[]="Anyxbox rooted asset\n";
  char data[64]; DWORD got=0,written=0;
  HANDLE h=CreateFileA("data\\message.txt",GENERIC_READ,FILE_SHARE_READ,0,OPEN_EXISTING,0,0);
  if(h==INVALID_HANDLE_VALUE) ExitProcess(10);
  if(!ReadFile(h,data,sizeof(data),&got,0) || got!=sizeof(expected)-1) ExitProcess(11);
  for(DWORD i=0;i<got;++i) if(data[i]!=expected[i]) ExitProcess(12);
  if(!CloseHandle(h)) ExitProcess(13);
  h=CreateFileA("data\\message.txt",GENERIC_WRITE,0,0,CREATE_ALWAYS,0,0);
  if(h!=INVALID_HANDLE_VALUE) ExitProcess(14);
  h=CreateFileA("..\\outside.txt",GENERIC_READ,0,0,OPEN_EXISTING,0,0);
  if(h!=INVALID_HANDLE_VALUE) ExitProcess(15);
  if(!WriteFile(GetStdHandle(STD_OUTPUT_HANDLE),data,got,&written,0) || written!=got) ExitProcess(16);
  ExitProcess(0);
}
