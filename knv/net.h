// net.h
// UC对协议层基本函数的封装
//
// Shaneyu@tencent.com 
//
// 2013-10-31	shaneyu 	Created
//

#ifndef __UC_NET__
#define __UC_NET__

#include <stdint.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <sys/time.h>
#include <string.h>

namespace UcNet
{
	const char *GetLocalIp(const char *family="inet", const char *ifname="eth1");
	static inline const char *GetLocalIpv4Addr(const char *ifname="eth1") { return GetLocalIp("inet", ifname); }
	static inline const char *GetLocalIpv6Addr(const char *ifname="eth1") { return GetLocalIp("inet6", ifname); }

	// mask the difference between IPv4/IPv6
	class UcSockAddr
	{
	public:
		UcSockAddr(bool use_ipv6 = false):a6() { addr_len = use_ipv6? sizeof(sockaddr_in6) : sizeof(sockaddr_in); }
		UcSockAddr(const char *ipstr, int port);
		UcSockAddr(const struct sockaddr_in &a): a4(a), addr_len(sizeof(a4)) {}
		UcSockAddr(const struct sockaddr_in6 &a): a6(a), addr_len(sizeof(a6)) {}
		UcSockAddr(const struct sockaddr *a, socklen_t l): addr_len(l) {memcpy(&addr, a, l);}
		UcSockAddr(const UcSockAddr &a): a6(a.a6), addr_len(a.addr_len) {}

		const char *to_str();
		const char *to_str_with_port();
		bool IsIpv4() const { return addr_len==sizeof(a4); }
		bool IsIpv6() const { return !IsIpv4(); }
		UcSockAddr &operator= (const UcSockAddr &a) { a6 = a.a6; addr_len = a.addr_len; return *this; }

		union
		{
			struct sockaddr     addr;
			struct sockaddr_in  a4;
			struct sockaddr_in6 a6;
		};
		socklen_t addr_len;
	};

	int SetSocketNonblock(int sockfd, bool nonblock);
	int SetSocketRecvTimeout(int sockfd, uint64_t iTimeoutMs);
	int CreateUdpListenSocket(int port, bool reuse = true, bool use_ipv6 = false, bool nblock = true);
};

#endif

