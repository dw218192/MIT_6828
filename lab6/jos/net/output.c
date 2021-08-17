#include "ns.h"

extern union Nsipc nsipcbuf;

void
output(envid_t ns_envid)
{
	int r;
	int32_t reqno;
	envid_t from_envid;

	binaryname = "ns_output";

	// LAB 6: Your code here:
	// 	- read a packet from the network server
	//	- send the packet to the device driver
	while(1)
	{
		reqno = ipc_recv(&from_envid, &nsipcbuf, 0);
		if(reqno == NSREQ_OUTPUT && from_envid == ns_envid)
		{
			if((r = sys_net_transmit(nsipcbuf.pkt.jp_data, nsipcbuf.pkt.jp_len)) < 0)
				cprintf("%s: %e\n", binaryname, r);
		}
	}
}
