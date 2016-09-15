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


Here how its looks like:

[31910.651463] bc750 I/O resource assignment
[31910.651468]   PCI BAR 0: phy: 0x00000000fe300000, base: 0xffffc90001400000, length: 1048576
[31910.651471]   PCI BAR 1: phy: 0x00000000fe200000, base: 0xffffc90001600000, length: 1048576
[31910.651473]   PCI BAR 2: phy: 0x00000000fe100000, base: 0xffffc90001800000, length: 1048576
[31910.651475]   PCI BAR 3: not configured.
[31910.651477]   PCI BAR 4: phy: 0x00000000fe0c0000, base: 0xffffc90001280000, length: 262144
[31910.651478]   PCI BAR 5: not configured.
[31910.651480] bc750 IRQ: 17
[31910.651483] bc750 host DMA address: 0xda52c000
[31910.651542] bcpci0: created.
[31910.651570] symmbc7x: loaded.

06:00.0 Power PC: Freescale Semiconductor Inc MPC8308 (rev 10)
        Subsystem: Datum Inc. Bancomm-Timing Division MPC8308
        Flags: fast devsel, IRQ 17
        Memory at fe300000 (32-bit, non-prefetchable) [size=1M]
        Memory at fe200000 (32-bit, non-prefetchable) [size=1M]
        Memory at fe100000 (64-bit, non-prefetchable) [size=1M]
        Memory at fe0c0000 (64-bit, non-prefetchable) [size=256K]
        Capabilities: [44] Power Management version 2
        Capabilities: [4c] Express Endpoint, MSI 00
        Capabilities: [70] MSI: Enable- Count=1/16 Maskable- 64bit+
        Capabilities: [100] Advanced Error Reporting
        Capabilities: [138] Virtual Channel
        Capabilities: [3f8] Vendor Specific Information: ID=0000 Rev=1 Len=bfc <?>
        Kernel driver in use: bc750
        
                          PTP 
********************************************************************************
PTP Started Successfully!
********************************************************************************


================================================================================
                              Symmetricom, Inc.
                                PCIe-1000 UI
================================================================================
        1. Start PTP                      2. Stop PTP
        3. PTP Management Messages        4. Network Configuration
        5. Software Upgrade               6. Version Info
        7. Read Time From Host Memory     8. Read Time From Target Memory
        9. BC Pass-thru Command          10. System Status
        0. Exit the Program


        Select Option: 10


********************************************************************************
                          BC System Status 
********************************************************************************
PTP: Running

********************************************************************************        


I modified the code to bring it works for Ubuntu 16.04 (Linux kernel 4.0)
