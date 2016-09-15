# PCIe-1000
PCIe + PTP Time and Frequency Processor

The Symmetricom® PCIe-1000 PTP Clock Card is referred through this manual as the PCIe-1000
card. It is also referred to in other literature as SyncPoint PCIe-1000.
The PCIe-1000 card provides ultra high availability time with sub-microsecond accurate time stamps
for programs running in Linux. The IEEE 1588 based PCIe-1000 has been optimized to be resilient to
network related time errors and provides nanosecond caliber time as needed to a Linux program in
under a microsecond.
The PTP synchronized PCIe-1000 synchronizes to a PTP grandmaster, such as a SyncServer, over
a network. Networks can introduce arrival time jitter of critical timing packets at the PTP slave, called
packet delay variation (PDV). To overcome PDV the PCIe-1000 deploys state-of-the art filtering and
servo algorithms, accommodates increased packet exchange rates, and includes a high performance
OCXO oscillator as standard. The net result is an extremely accurate clock that is very resilient to network
impairments.
The PCIe-1000 includes a 1 PPS output signal with a convenient BNC connector that is useful to
compare the time on the card to that of the master. This is useful in adjusting the PTP parameters for
optimal time transfer accuracy over the network. The 1 PPS is also useful to synchronize adjacent
network devices or probes that accept a 1 PPS input.
Once synchronized, the PCIe-1000 provides time to the host machine by either writing the time
directly to a host memory location or by responding to requests for time over the PCIe bus.
Applications accessing the hosted memory time location can read the time in excess of one million
times per second and retrieve monotonically advancing time.

The initial Linux driver for PCIe-1000 (SourceRelease-1.02) has driver for older Linux kernel. It was confirmed it works on 2.6 and 3.0 kernel.

I modified the code to bring it works for Ubuntu 16.04 (Linux kernel 4.0)
