#include "ns.h"

extern union Nsipc nsipcbuf;

void
input(envid_t ns_envid)
{
	int r;
	int32_t reqno;
	envid_t from_envid;
	char buf[2048];
	
	binaryname = "ns_input";

	// LAB 6: Your code here:
	// 	- read a packet from the device driver
	//	- send it to the network server
	// Hint: When you IPC a page to the network server, it will be
	// reading from it for a while, so don't immediately receive
	// another packet in to the same physical page.
	while(1)
	{
		// recv into a temp buffer first, because nsipcbuf.pkt may be in use
		r = sys_net_recv(buf);
		
		if(r > 0)
		{
			// we have a packet			
			while(!envs[ENVX(ns_envid)].env_ipc_recving) sys_yield();
			
			nsipcbuf.pkt.jp_len = r;
			memcpy(nsipcbuf.pkt.jp_data, buf, r);

			ipc_send(ns_envid, NSREQ_INPUT, &nsipcbuf, PTE_U | PTE_P);
		}
		else
		{
			if(r != -E_RX_EMPTY)
				cprintf("%s:%e\n", binaryname, r);
		}
	}
}
