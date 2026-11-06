// Bounded lab TCP echo probe. External timeout also bounds every invocation.
#include <arpa/inet.h>
#include <sys/socket.h>
#include <poll.h>
#include <unistd.h>
#include <array>
#include <charconv>
#include <chrono>
#include <iostream>
#include <stdexcept>
#include <string>
namespace {
unsigned number(const char* s) {
 unsigned n=0; std::string v(s); auto [p,e]=std::from_chars(v.data(),v.data()+v.size(),n);
 if(e!=std::errc{} || p!=v.data()+v.size() || n==0 || n>65535) throw std::runtime_error("invalid number");
 return n;
}
struct Socket { int fd; explicit Socket(int v):fd(v){if(fd<0)throw std::runtime_error("socket failed");} ~Socket(){close(fd);} Socket(const Socket&)=delete; };
sockaddr_in address(const char* ip,unsigned port){sockaddr_in a{};a.sin_family=AF_INET;a.sin_port=htons(port);if(inet_pton(AF_INET,ip,&a.sin_addr)!=1)throw std::runtime_error("invalid IP");return a;}
void bounded(int fd){timeval t{2,0};if(setsockopt(fd,SOL_SOCKET,SO_RCVTIMEO,&t,sizeof(t)) || setsockopt(fd,SOL_SOCKET,SO_SNDTIMEO,&t,sizeof(t)))throw std::runtime_error("timeout setup failed");}
void send_all(int fd,const std::string& s){size_t done=0;while(done<s.size()){auto n=send(fd,s.data()+done,s.size()-done,MSG_NOSIGNAL);if(n<=0)throw std::runtime_error("send failed");done+=static_cast<size_t>(n);}}
std::string line(int fd){std::string s;while(s.size()<1100){char c;auto n=recv(fd,&c,1,0);if(n!=1)throw std::runtime_error("receive failed");if(c=='\n')return s;s+=c;}throw std::runtime_error("oversize line");}
}
int main(int argc,char** argv){try{
 if(argc==2 && std::string(argv[1])=="--version"){std::cout<<"NetForge TCP lab tool 1\n";return 0;}
 if(argc==5 && std::string(argv[1])=="stream-serve"){
  unsigned port=number(argv[2]),seconds=number(argv[3]),sessions=number(argv[4]);
  if(seconds>30 || sessions>8)throw std::runtime_error("stream server bounds");
  Socket listener(socket(AF_INET,SOCK_STREAM,0));int yes=1;
  setsockopt(listener.fd,SOL_SOCKET,SO_REUSEADDR,&yes,sizeof(yes));
  auto a=address("0.0.0.0",port);
  if(bind(listener.fd,reinterpret_cast<sockaddr*>(&a),sizeof(a)) || listen(listener.fd,8))throw std::runtime_error("listen failed");
  std::cout<<"READY stream port="<<port<<std::endl;
  auto end=std::chrono::steady_clock::now()+std::chrono::seconds(seconds);
  for(unsigned session=0;session<sessions;++session){
   while(true){if(std::chrono::steady_clock::now()>=end)throw std::runtime_error("stream deadline");pollfd p{listener.fd,POLLIN,0};if(poll(&p,1,100)>0)break;}
   Socket client(accept(listener.fd,nullptr,nullptr));bounded(client.fd);
   size_t total=0;std::array<char,4096> data{};
   while(true){if(std::chrono::steady_clock::now()>=end)throw std::runtime_error("stream deadline");auto n=recv(client.fd,data.data(),data.size(),0);if(n<0)throw std::runtime_error("stream receive");if(n==0)break;total+=static_cast<size_t>(n);if(total>1048576)throw std::runtime_error("stream byte bound");send_all(client.fd,std::string(data.data(),static_cast<size_t>(n)));}
   if(shutdown(client.fd,SHUT_WR))throw std::runtime_error("server half close");
   std::cout<<"PASS stream server bytes="<<total<<std::endl;
  }return 0;
 }
 if(argc==6 && std::string(argv[1])=="stream-probe"){
  unsigned rounds=number(argv[4]),size=number(argv[5]);
  if(rounds>128 || size>8192 || static_cast<size_t>(rounds)*size>1048576)throw std::runtime_error("stream client bounds");
  Socket client(socket(AF_INET,SOCK_STREAM,0));bounded(client.fd);auto a=address(argv[2],number(argv[3]));
  if(connect(client.fd,reinterpret_cast<sockaddr*>(&a),sizeof(a)))throw std::runtime_error("connect failed");
  for(unsigned round=0;round<rounds;++round){
   std::string expected(size,' ');for(unsigned i=0;i<size;++i)expected[i]=static_cast<char>((i*31+round*17)%256);
   send_all(client.fd,expected);std::string actual(size,' ');size_t done=0;
   while(done<size){auto n=recv(client.fd,actual.data()+done,size-done,0);if(n<=0)throw std::runtime_error("stream truncated");done+=static_cast<size_t>(n);}
   if(actual!=expected)throw std::runtime_error("stream byte mismatch");
  }
  if(shutdown(client.fd,SHUT_WR))throw std::runtime_error("client half close");
  char extra;if(recv(client.fd,&extra,1,0)!=0)throw std::runtime_error("missing EOF or unexpected data");
  std::cout<<"PASS persistent TCP bytes="<<static_cast<size_t>(rounds)*size<<" rounds="<<rounds<<" half_close=verified peer="<<argv[2]<<std::endl;return 0;
 }
 if(argc==5 && std::string(argv[1])=="serve"){
  unsigned port=number(argv[2]),seconds=number(argv[4]);if(seconds>30)throw std::runtime_error("duration exceeds 30 seconds");
  Socket listener(socket(AF_INET,SOCK_STREAM,0));int yes=1;setsockopt(listener.fd,SOL_SOCKET,SO_REUSEADDR,&yes,sizeof(yes));auto a=address("0.0.0.0",port);
  if(bind(listener.fd,reinterpret_cast<sockaddr*>(&a),sizeof(a)) || listen(listener.fd,8))throw std::runtime_error("listen failed");
  std::cout<<"READY backend="<<argv[3]<<" port="<<port<<std::endl;
  auto end=std::chrono::steady_clock::now()+std::chrono::seconds(seconds);unsigned replies=0;
  while(std::chrono::steady_clock::now()<end){pollfd p{listener.fd,POLLIN,0};if(poll(&p,1,100)<=0)continue;if(!(p.revents&POLLIN))throw std::runtime_error("listener error");Socket client(accept(listener.fd,nullptr,nullptr));bounded(client.fd);auto request=line(client.fd);send_all(client.fd,std::string(argv[3])+"|"+request+"\n");++replies;}
  std::cout<<"STOP backend="<<argv[3]<<" replies="<<replies<<'\n';return 0;
 }
 if(argc==6 && std::string(argv[1])=="probe"){
  Socket client(socket(AF_INET,SOCK_STREAM,0));bounded(client.fd);auto a=address(argv[2],number(argv[3]));
  if(connect(client.fd,reinterpret_cast<sockaddr*>(&a),sizeof(a)))throw std::runtime_error("connect failed");
  std::string token(argv[4]);if(token.empty() || token.size()>1000 || token.find('\n')!=std::string::npos)throw std::runtime_error("invalid token");
  send_all(client.fd,token+"\n");if(line(client.fd)!=std::string(argv[5])+"|"+token)throw std::runtime_error("backend/token mismatch");
  std::cout<<"PASS peer="<<argv[2]<<':'<<argv[3]<<" backend="<<argv[5]<<" token="<<token<<'\n';return 0;
 }
 throw std::runtime_error("Usage: tcp_probe serve PORT LABEL SECONDS | probe IP PORT TOKEN EXPECTED_LABEL");
}catch(const std::exception& e){std::cerr<<"tcp_probe: "<<e.what()<<'\n';return 1;}}
