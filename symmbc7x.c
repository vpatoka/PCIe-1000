//***************************************************************************
//
// symmbc7x.c
//
// Version 1.0.0
//
// Symmetricom bc7xxPCIe driver
//
// Copyright (c) Symmetricom - 2011
//
// 10/06/2011: Stephen Yu
//
//***************************************************************************

#include <linux/init.h>
#include <linux/module.h>
#include <linux/moduleparam.h>
#include <linux/fs.h>
#include <linux/device.h>
#include <linux/pci_ids.h>
#include <linux/pci.h>
#include <linux/cdev.h>
#include <linux/ioport.h>
#include <linux/dma-mapping.h>
#include <linux/interrupt.h>
#include <linux/mutex.h>
#include <linux/version.h>
#include <linux/mm.h>
#include <linux/time.h>
#if LINUX_VERSION_CODE <= KERNEL_VERSION(2,6,37)
#include <linux/smp_lock.h>
#endif
#include <asm/atomic.h>
#include <asm/uaccess.h>
#include <asm/io.h>
#include "symmbc7x.h"


//-------------------------------------------------------------------------
// Defines
//-------------------------------------------------------------------------
#define CLASS_NAME  "symmbc7x"
#define DRIVER_NAME "symmbc7x"
#define DEV_NAME    "bc750"

//-------------------------------------------------------------------------
// Vendor id - Freescale (MPC8008), and device id
//-------------------------------------------------------------------------
#define SYMMBC_VENDOR_ID PCI_VENDOR_ID_FREESCALE
#define SYMMBC_DEVICE_ID 0xc006

//-------------------------------------------------------------------------
// Default major number to 0 for dynamic allocation
//-------------------------------------------------------------------------
#define SYMMBC_MAJOR    0

//-------------------------------------------------------------------------
// Default number of devices
//-------------------------------------------------------------------------
#define SYMMBC_NUM_DEVS 8

//-------------------------------------------------------------------------
// Define the following in case pci.h does not
//-------------------------------------------------------------------------
#ifndef PCI_STD_RESOURCES
    #define PCI_STD_RESOURCES 0
#endif

#ifndef PCI_STD_RESOURCE_END
    #define PCI_STD_RESOURCE_END 5
#endif

//-------------------------------------------------------------------------
// MPC8308 target registers
//-------------------------------------------------------------------------
#define MPC8308_PCIE_IMMR_OFFSET    0x00009000

#define MPC8308_PEX_OWTARL0         0x00000CA8
#define MPC8308_PEX_OWTARH0         0x00000CAC
#define MPC8308_PEX_OWAR0           0x00000CA0  

#define MPC8308_PEX_OWAR_EN         0x00000001
#define MPC8308_PEX_OWAR_TYPE_MEM   0x00000004
#define MPC8308_PEX_OWAR_SIZE       0xFFFFF000

//-------------------------------------------------------------------------
// Host ready and host system time to target
//-------------------------------------------------------------------------
#define FPGA_HOST_READY_OFFSET      0x018
#define FPGA_HOST_MAJOR_TIME_OFFSET 0x020
#define FPGA_HOST_MINOR_TIME_OFFSET 0x024


//-------------------------------------------------------------------------
// Date type - per device structure
//-------------------------------------------------------------------------
struct symmbc_dev {
    void __iomem   *iomap_base[PCI_STD_RESOURCE_END - PCI_STD_RESOURCES + 1];
    u8              irq;
    int             dev_minor;
    void           *mem_base;
    dma_addr_t      dma_base;
    struct mutex    mtx;
    struct pci_dev *ppci_dev;
    struct cdev     cdev;
};


//-------------------------------------------------------------------------
// Module parameters
//-------------------------------------------------------------------------
static int symmbc_major = SYMMBC_MAJOR;
module_param(symmbc_major, int, 0);
MODULE_PARM_DESC(symmbc_major,
        "bc7xxPCIe Linux driver major device number (default: 0)");

int symmbc_ndevs = SYMMBC_NUM_DEVS;
module_param(symmbc_ndevs, int, 0);
MODULE_PARM_DESC(symmbc_ndevs,
        "Maximum number of bc7xxPCIe cards (default: 8)");

//-------------------------------------------------------------------------
// Module information
//-------------------------------------------------------------------------
MODULE_AUTHOR("Symmetricom, Inc.");
MODULE_DESCRIPTION("Symmetricom bc7xxPCIe TFP Linux device driver");
MODULE_VERSION("1.0");
MODULE_LICENSE("Dual BSD/GPL");

//-------------------------------------------------------------------------
// PCI vendor id and device id supported by the driver
//-------------------------------------------------------------------------
static struct pci_device_id symmbc_ids[] = {
    { PCI_DEVICE(SYMMBC_VENDOR_ID, SYMMBC_DEVICE_ID) },
    { } // NULL entry
};
MODULE_DEVICE_TABLE(pci, symmbc_ids);

//-------------------------------------------------------------------------
// Function prototypes
//-------------------------------------------------------------------------
static int  symmbc_probe(struct pci_dev *pdev, const struct pci_device_id *ent);
static void symmbc_remove(struct pci_dev *pdev);
static int symmbc_open(struct inode *inode, struct file *filp);
static int symmbc_release(struct inode *inode, struct file *filp);
static int symmbc_mmap(struct file *filp, struct vm_area_struct *vma);

#if LINUX_VERSION_CODE >= KERNEL_VERSION(2,6,11)
static long symmbc_unlocked_ioctl(struct file *filp, unsigned int cmd, unsigned long arg);
#else
static int symmbc_ioctl(struct inode *inode, struct file *filp, unsigned int cmd, unsigned long arg);
#endif
static irqreturn_t symmbc_irq(int irq, void *dev_id);


//-------------------------------------------------------------------------
// The symmbc_driver structure initialization
//-------------------------------------------------------------------------
static struct pci_driver symmbc_driver = {
    .name     = DEV_NAME,
    .id_table = symmbc_ids,
    .probe    = symmbc_probe,
    .remove   = symmbc_remove,
};

//-------------------------------------------------------------------------
// The character driver interface
//-------------------------------------------------------------------------
static struct file_operations symmbc_fops = {
    .owner   = THIS_MODULE,
    .open    = symmbc_open,
    .release = symmbc_release,
    .mmap    = symmbc_mmap,
#if LINUX_VERSION_CODE >= KERNEL_VERSION(2,6,11)
    .unlocked_ioctl = symmbc_unlocked_ioctl,
#else
    .ioctl   = symmbc_ioctl,
#endif
};

// Sysfs class
static struct class *symmbc_class;

// Current minor number
static atomic_t curr_minor;


//-------------------------------------------------------------------------
// Probe
//-------------------------------------------------------------------------
static int symmbc_probe(struct pci_dev *pdev, const struct pci_device_id *ent)
{
    int rc, i, found_any;
    struct symmbc_dev *pbc_dev = NULL;
    u8 *pPCIeIMMR;
    u64 l_u64Addr;
    struct device *psys_dev = NULL;
    dev_t dev_num;
    struct timeval tv;
    u8 *pFPGA;

    if (SYMMBC_VENDOR_ID != ent->vendor || SYMMBC_DEVICE_ID != ent->device) {
        pr_err("<-- %s: vendor or device ID mismatch.\n", __func__);
        return -EFAULT;
    }
    if (atomic_read(&curr_minor) > symmbc_ndevs) {
        pr_err("<-- %s: no minor device available.\n", __func__);
        return -ENODEV;
    }

    // Check DMA
#if LINUX_VERSION_CODE >= KERNEL_VERSION(2,6,34)
    // Kernel that supports dma_set_coherent_mask
    if (!dma_set_mask(&pdev->dev, DMA_BIT_MASK(64))) {
        // The following always succeeds (DMA-API_HOWTO.txt).
        dma_set_coherent_mask(&pdev->dev, DMA_BIT_MASK(64));
    }
    else if (dma_set_coherent_mask(&pdev->dev, DMA_BIT_MASK(64))) {
        if (!dma_set_mask(&pdev->dev, DMA_BIT_MASK(32))) {
            // The following always succeeds (DMA-API_HOWTO.txt).
            dma_set_coherent_mask(&pdev->dev, DMA_BIT_MASK(32));
        }
        else if (dma_set_coherent_mask(&pdev->dev, DMA_BIT_MASK(32))) {
            pr_err("<-- %s: no suitable DMA support available.\n", __func__);
            return -ENODEV;
        }
    }
#else
    // Older kernel does not support dma_set_coherent_mask
    if (!dma_set_mask(&pdev->dev, DMA_BIT_MASK(64))) {
        pci_set_consistent_dma_mask(pdev, DMA_BIT_MASK(64));
    }
    else if (pci_set_consistent_dma_mask(pdev, DMA_BIT_MASK(64))) {
        if (!dma_set_mask(&pdev->dev, DMA_BIT_MASK(32))) {
            pci_set_consistent_dma_mask(pdev, DMA_BIT_MASK(32));
        }
        else if (pci_set_consistent_dma_mask(pdev, DMA_BIT_MASK(32))) {
            pr_err("<-- %s: no suitable DMA support available.\n", __func__);
            return -ENODEV;
        }
    }
#endif

    // Allocate memory for the device
    pbc_dev = kzalloc(sizeof(struct symmbc_dev), GFP_KERNEL);
    if (!pbc_dev) {
        pr_err("<-- %s: kmalloc() failed.\n", __func__);
        return -ENOMEM;
    }

    // Set back link to pci_dev
    pbc_dev->ppci_dev = pdev;

    // Initialize PCI resources
    rc = pci_enable_device(pdev);
    if (rc) {
        pr_err("<-- %s: pci_enable_device() failed.\n", __func__);
        goto exit_kfree;
    }

    rc = pci_request_regions(pdev, DEV_NAME);
    if (rc) {
        pr_err("<-- %s: pci_request_regions() failed.\n", __func__);
        goto exit_disable;
    }

    // IO map base if the BAR is configured
    found_any = 0;
    for (i = PCI_STD_RESOURCES; i <= PCI_STD_RESOURCE_END; i++) {
        // Skip not configured BARs
        if (0 == pci_resource_len(pdev, i) ||
            0 == pci_resource_start(pdev, i)) {
            pbc_dev->iomap_base[i] = NULL;
            continue;
        }
        if ((pci_resource_flags(pdev, i) & IORESOURCE_IO) ||
            (pci_resource_flags(pdev, i) & IORESOURCE_MEM)) {
            pbc_dev->iomap_base[i] = pci_iomap(pdev, i, 0);
            if (!pbc_dev->iomap_base[i]) {
                pr_err("<-- %s: pci_iomap(%d) failed.\n", __func__, i);
                rc = -EFAULT;
                goto exit_release;
            }
            found_any = 1;
        }
    }

    if (!found_any) {
        rc = -EFAULT;
        goto exit_release;
    }

    pr_info(DEV_NAME " I/O resource assignment\n");
    for (i = PCI_STD_RESOURCES; i <= PCI_STD_RESOURCE_END; i++) {
        if (0 == pci_resource_len(pdev, i) ||
            0 == pci_resource_start(pdev, i)) {
            pr_info("  PCI BAR %d: not configured.\n", i);
            continue;
        }
        if (pbc_dev->iomap_base[i]) {
            pr_info("  PCI BAR %d: phy: 0x%016llx, base: 0x%p, length: %u\n",
                i,
                (u64)pci_resource_start(pbc_dev->ppci_dev, i),
                pbc_dev->iomap_base[i],
                (u32)pci_resource_len(pbc_dev->ppci_dev, i));
        }
        else {
            pr_info("  PCI BAR %d: configured, but not memory or io resource.\n", i);
        }
    }
    pr_info(DEV_NAME " IRQ: %d\n", pdev->irq);

    // Allocate DMA buffer
    pbc_dev->mem_base = dma_alloc_coherent(&pbc_dev->ppci_dev->dev,
                        DMA_BUFFER_SIZE, &pbc_dev->dma_base, GFP_KERNEL);
    if (!pbc_dev->mem_base) {
        pr_err("<-- %s: dma_alloc_coherent() failed.\n", __func__);
        rc = -ENOMEM;
        goto exit_release;
    }
    pr_info(DEV_NAME " host DMA address: 0x%llx\n", pbc_dev->dma_base);

    // BAR1 points to the MPC8308 IMMR
    pPCIeIMMR = (u8 *)pbc_dev->iomap_base[1];

    *((u32 *)(pPCIeIMMR + MPC8308_PCIE_IMMR_OFFSET + MPC8308_PEX_OWAR0)) = 
            cpu_to_le32( (0x1000 & MPC8308_PEX_OWAR_SIZE) |
                         MPC8308_PEX_OWAR_TYPE_MEM | MPC8308_PEX_OWAR_EN );

    // The host DMA address can be 32 or 64 bit.
    l_u64Addr = (u64)pbc_dev->dma_base;

    *((u32 *)(pPCIeIMMR + MPC8308_PCIE_IMMR_OFFSET + MPC8308_PEX_OWTARL0)) = 
            cpu_to_le32(l_u64Addr & 0xffffffff);
    *((u32 *)(pPCIeIMMR + MPC8308_PCIE_IMMR_OFFSET + MPC8308_PEX_OWTARH0)) = 
            cpu_to_le32((l_u64Addr >> 32) & 0xffffffff);

    mutex_init(&pbc_dev->mtx);
    pci_set_drvdata(pdev, pbc_dev);

    // Register to the device tree
    dev_num = MKDEV(symmbc_major, atomic_read(&curr_minor));
    pbc_dev->dev_minor = atomic_read(&curr_minor);
    pbc_dev->cdev.owner = THIS_MODULE;
    cdev_init(&pbc_dev->cdev, &symmbc_fops);
    rc = cdev_add(&pbc_dev->cdev, dev_num, 1);
    if (rc) {
        pr_err("<-- %s: cdev_add() failed.\n", __func__);
        goto exit_release;
    }

    // Note the prototype of device_create() changed in newer kernel!!!
    psys_dev = device_create(symmbc_class, &pdev->dev, dev_num, NULL,
                   "bcpci%d", atomic_read(&curr_minor));
    if (IS_ERR(psys_dev)) {
        pr_err("<-- %s: device_create() failed.\n", __func__);
        rc = -EFAULT;
        goto exit_del;
    }

    atomic_inc(&curr_minor);

    // Retrieve the host system time - we do this at the last
    do_gettimeofday(&tv);

    // Write the host system time to the target FPGA memory
    pFPGA = (u8 *)pbc_dev->iomap_base[4];

    *((u32 *)(pFPGA + FPGA_HOST_MAJOR_TIME_OFFSET)) = cpu_to_be32(tv.tv_sec);
    *((u32 *)(pFPGA + FPGA_HOST_MINOR_TIME_OFFSET)) = cpu_to_be32(tv.tv_usec);

    // Set the host ready bit
    *((u16 *)(pFPGA + FPGA_HOST_READY_OFFSET)) = cpu_to_be16(1);

    pr_info("bcpci%d: created.\n", pbc_dev->dev_minor);
    return 0;

exit_del:
    cdev_del(&pbc_dev->cdev);

exit_release:
    for (i = PCI_STD_RESOURCES; i <= PCI_STD_RESOURCE_END; i++) {
        if (pbc_dev->iomap_base[i])
            pci_iounmap(pdev, pbc_dev->iomap_base[i]);
    }
    pci_release_regions(pdev);

exit_disable:
    pci_disable_device(pdev);

exit_kfree:
    kfree(pbc_dev);

    pr_err("<-- %s: exit with error.\n", __func__);
    return rc;
}

//-------------------------------------------------------------------------
// Remove
//-------------------------------------------------------------------------
void symmbc_remove(struct pci_dev *pdev)
{
    int i;
    struct symmbc_dev *pbc_dev = pci_get_drvdata(pdev);

    mutex_destroy(&pbc_dev->mtx);
    dma_free_coherent(&pbc_dev->ppci_dev->dev, DMA_BUFFER_SIZE,
        pbc_dev->mem_base, pbc_dev->dma_base);
    pbc_dev->mem_base = NULL;
    device_destroy(symmbc_class, MKDEV(symmbc_major, pbc_dev->dev_minor));
    cdev_del(&pbc_dev->cdev);
    for (i = PCI_STD_RESOURCES; i <= PCI_STD_RESOURCE_END; i++) {
        if (pbc_dev->iomap_base[i])
            pci_iounmap(pdev, pbc_dev->iomap_base[i]);
    }
    pci_release_regions(pdev);
    pci_disable_device(pdev);
    pr_info("bcpci%d: removed.\n", pbc_dev->dev_minor);
    kfree(pbc_dev);
}

//-------------------------------------------------------------------------
// Open
//-------------------------------------------------------------------------
int symmbc_open(struct inode *inode, struct file *filp)
{
    struct symmbc_dev *pdev;
    int rc;

    pdev = container_of(inode->i_cdev, struct symmbc_dev, cdev);

    // Request irq resource
    if ((rc = request_irq(pdev->ppci_dev->irq, symmbc_irq, IRQF_SHARED,
                  DRIVER_NAME, pdev))) {
        pr_err("<-- %s: Error: request_irq() failed with %d.\n", __func__, rc);
        return rc;
    }

    filp->private_data = pdev;
    return 0;
}

//-------------------------------------------------------------------------
// Close
//-------------------------------------------------------------------------
int symmbc_release(struct inode *inode, struct file *filp)
{
    struct symmbc_dev *pdev = (struct symmbc_dev *)filp->private_data;

    free_irq(pdev->ppci_dev->irq, pdev);
    return 0;
}

//-------------------------------------------------------------------------
// I/O control
//-------------------------------------------------------------------------
#if LINUX_VERSION_CODE >= KERNEL_VERSION(2,6,11)

// The version runs without the BKL 
long symmbc_unlocked_ioctl(struct file *filp, unsigned int cmd, unsigned long arg)
{
    long rc = 0, retval = 0, i;
    struct symmbc_dev *pdev = (struct symmbc_dev *)filp->private_data;
    mmap_config mm_cfg;
    resource_size_t start;
    u8 * pPcieImmrRegs;
    u64  l_u64Addr;

    if (SYMMBC_IOC_MAGIC != _IOC_TYPE(cmd)) return -ENOTTY;
    if (_IOC_NR(cmd) > SYMMBC_IOC_MAX) return -ENOTTY;

    if (_IOC_DIR(cmd) & _IOC_READ)
        rc = !access_ok(VERIFY_WRITE, (void __user *)arg, _IOC_SIZE(cmd));
    else if (_IOC_DIR(cmd) & _IOC_WRITE)
        rc =  !access_ok(VERIFY_READ, (void __user *)arg, _IOC_SIZE(cmd));
    if (rc) return -EFAULT;

#if LINUX_VERSION_CODE <= KERNEL_VERSION(2,6,37)
    lock_kernel();
#else
    mutex_lock(&pdev->mtx);
#endif

    switch(cmd) {

        case SYMMBC_IOC_GET_MMAP_CONFIG:
            for (i = PCI_STD_RESOURCES; i <= PCI_STD_RESOURCE_END; i++) {
                mm_cfg.bar[i].length = pci_resource_len(pdev->ppci_dev, i);
                start = pci_resource_start(pdev->ppci_dev, i);
                if (0 == mm_cfg.bar[i].length || 0 == start) {
                    mm_cfg.bar[i].offset = 0;
                }
                else {
                    mm_cfg.bar[i].offset = ((unsigned long)start) & ~PAGE_MASK;
                }
            }
            mm_cfg.dma.length = DMA_BUFFER_SIZE;
            mm_cfg.dma.offset = ((unsigned long)pdev->mem_base) & ~PAGE_MASK;
            rc = copy_to_user((void *)arg, &mm_cfg, sizeof(mmap_config));
            if (rc) {
                pr_err("<-- %s: copy_to_user (GET_MMAP_CONFIG) failed.\n", __func__);

#if LINUX_VERSION_CODE <= KERNEL_VERSION(2,6,37)
                unlock_kernel();
#else
                mutex_unlock(&pdev->mtx);
#endif
                return -EFAULT;
            }
            break;

        case SYMMBC_IOC_SET_DMA_BUS_ADDR:
            // Set the Norfolk card's PCI Express Outbound Window Registers to 
            // address the Host Local DMA memory. This is for target initiated writes.

            // BAR # 1 is the powerpc's registers
            pPcieImmrRegs = (u8 *)pdev->iomap_base[1];

            // The phsical address of the Host Local DMA-able memory

            l_u64Addr = (u64)pdev->dma_base;

            *(u32 *)(pPcieImmrRegs + MPC8308_PCIE_IMMR_OFFSET + MPC8308_PEX_OWTARL0) = 
                            cpu_to_le32(l_u64Addr & 0xffffffff);

            *(u32 *)(pPcieImmrRegs + MPC8308_PCIE_IMMR_OFFSET + MPC8308_PEX_OWTARH0) = 
                            cpu_to_le32((l_u64Addr >> 32) & 0xffffffff);
                        

            *(u32 *)(pPcieImmrRegs + MPC8308_PCIE_IMMR_OFFSET + MPC8308_PEX_OWAR0) = 
                        cpu_to_le32( (0x1000 & MPC8308_PEX_OWAR_SIZE) |  
                                     MPC8308_PEX_OWAR_TYPE_MEM |  
                                     MPC8308_PEX_OWAR_EN);
            break;

        default:
#if LINUX_VERSION_CODE <= KERNEL_VERSION(2,6,37)
            unlock_kernel();
#else
            mutex_unlock(&pdev->mtx);
#endif
            return -ENOTTY;
    }

#if LINUX_VERSION_CODE <= KERNEL_VERSION(2,6,37)
    unlock_kernel();
#else
    mutex_unlock(&pdev->mtx);
#endif
    return retval;
}

#else

// The version runs under the BKL
int symmbc_ioctl(struct inode *inode, struct file *filp, unsigned int cmd, unsigned long arg)
{
    int rc = 0, retval = 0, i;
    struct symmbc_dev *pdev = (struct symmbc_dev *)filp->private_data;
    mmap_config mm_cfg;
    resource_size_t start;
    u8 * pPcieImmrRegs;
    u64  l_u64Addr;

    if (SYMMBC_IOC_MAGIC != _IOC_TYPE(cmd)) return -ENOTTY;
    if (_IOC_NR(cmd) > SYMMBC_IOC_MAX) return -ENOTTY;

    if (_IOC_DIR(cmd) & _IOC_READ)
        rc = !access_ok(VERIFY_WRITE, (void __user *)arg, _IOC_SIZE(cmd));
    else if (_IOC_DIR(cmd) & _IOC_WRITE)
        rc =  !access_ok(VERIFY_READ, (void __user *)arg, _IOC_SIZE(cmd));
    if (rc) return -EFAULT;

    switch(cmd) {

        case SYMMBC_IOC_GET_MMAP_CONFIG:
            for (i = PCI_STD_RESOURCES; i <= PCI_STD_RESOURCE_END; i++) {
                mm_cfg.bar[i].length = pci_resource_len(pdev->ppci_dev, i);
                start = pci_resource_start(pdev->ppci_dev, i);
                if (0 == mm_cfg.bar[i].length || 0 == start) {
                    mm_cfg.bar[i].offset = 0;
                }
                else {
                    mm_cfg.bar[i].offset = ((unsigned long)start) & ~PAGE_MASK;
                }
            }
            mm_cfg.dma.length = DMA_BUFFER_SIZE;
            mm_cfg.dma.offset = ((unsigned long)pdev->mem_base) & ~PAGE_MASK;
            rc = copy_to_user((void *)arg, &mm_cfg, sizeof(mmap_config));
            if (rc) {
                pr_err("<-- %s: copy_to_user (GET_MMAP_CONFIG) failed.\n", __func__);
                return -EFAULT;
            }
            break;

        case SYMMBC_IOC_SET_DMA_BUS_ADDR:
            // Set the Norfolk card's PCI Express Outbound Window Registers to 
            // address the Host Local DMA memory. This is for target initiated writes.

            // BAR # 1 is the powerpc's registers
            pPcieImmrRegs = (u8 *)pdev->iomap_base[1];

            // The phsical address of the Host Local DMA-able memory

            l_u64Addr = (u64)pdev->dma_base;

            *(u32 *)(pPcieImmrRegs + MPC8308_PCIE_IMMR_OFFSET + MPC8308_PEX_OWTARL0) = 
                            cpu_to_le32(l_u64Addr & 0xffffffff);

            *(u32 *)(pPcieImmrRegs + MPC8308_PCIE_IMMR_OFFSET + MPC8308_PEX_OWTARH0) = 
                            cpu_to_le32((l_u64Addr >> 32) & 0xffffffff);
                        

            *(u32 *)(pPcieImmrRegs + MPC8308_PCIE_IMMR_OFFSET + MPC8308_PEX_OWAR0) = 
                        cpu_to_le32( (0x1000 & MPC8308_PEX_OWAR_SIZE) |  
                                     MPC8308_PEX_OWAR_TYPE_MEM |  
                                     MPC8308_PEX_OWAR_EN);
            break;

        default:
            return -ENOTTY;
    }

    return retval;
}

#endif

//-------------------------------------------------------------------------
// Mmap
//-------------------------------------------------------------------------
int symmbc_mmap(struct file *filp, struct vm_area_struct *vma)
{
    struct symmbc_dev *pdev = (struct symmbc_dev *)filp->private_data;
    resource_size_t start;

    if (vma->vm_pgoff < PCI_STD_RESOURCES ||
        (vma->vm_pgoff > PCI_STD_RESOURCE_END &&
         DMA_MMAP_PGOFF != vma->vm_pgoff)) {
        pr_err("<-- %s: invalid input (pgoff=%lu).\n", __func__, vma->vm_pgoff);
        return -EFAULT;
    }
    if (DMA_MMAP_PGOFF == vma->vm_pgoff) {
        if (remap_pfn_range(vma, vma->vm_start,
                virt_to_phys(pdev->mem_base) >> PAGE_SHIFT,
                vma->vm_end - vma->vm_start, vma->vm_page_prot)) {
            pr_err("<-- %s: remap_pfn_range(DMA) failed.\n", __func__);
            return -EAGAIN;
        }
    }
    else {
        start = pci_resource_start(pdev->ppci_dev, vma->vm_pgoff);
        if (0 == pci_resource_len(pdev->ppci_dev, vma->vm_pgoff) ||
            (start = pci_resource_start(pdev->ppci_dev, vma->vm_pgoff)) == 0) {
            pr_err("<-- %s: BAR %lu is not configured.\n", __func__, vma->vm_pgoff);
            return -EFAULT;
        }
        vma->vm_flags |= VM_IO | VM_DONTEXPAND;
        if (remap_pfn_range(vma, vma->vm_start,
                ((unsigned long)start) >> PAGE_SHIFT,
                vma->vm_end - vma->vm_start, vma->vm_page_prot)) {
            pr_err("<-- %s: remap_pfn_range() failed.\n", __func__);
            return -EAGAIN;
        }
    }
    return 0;
}

//-------------------------------------------------------------------------
// symmbc_irq - dummy for the moment
//-------------------------------------------------------------------------
static irqreturn_t symmbc_irq(int irq, void *dev_id)
{
    struct symmbc_dev *pdev = (struct symmbc_dev *)dev_id;
    if (irq != pdev->ppci_dev->irq)
        return IRQ_NONE;

    return IRQ_HANDLED;
}

//-------------------------------------------------------------------------
// Driver initialization
//-------------------------------------------------------------------------
static int __init symmbc_init(void)
{
    dev_t dev_num = MKDEV(symmbc_major, 0);
    int rc;

    // Register the major device
    if (symmbc_major) {
        rc = register_chrdev_region(dev_num, symmbc_ndevs, DEV_NAME);
    }
    else {
        rc = alloc_chrdev_region(&dev_num, 0, symmbc_ndevs, DEV_NAME);
        if (rc == 0)
            symmbc_major = MAJOR(dev_num);
    }

    // Error occurred if rc was not 0
    if (rc) {
        pr_err("<-- %s: allocating major dev number failed.\n", __func__);
        return rc;
    }

    // Device minor number starts from 0
    atomic_set(&curr_minor, 0);

    symmbc_class = class_create(THIS_MODULE, CLASS_NAME);
    if (IS_ERR(symmbc_class)) {
        unregister_chrdev_region(dev_num, symmbc_ndevs);
        pr_err("<-- %s: class_create() failed.\n", __func__);
        return -EFAULT;
    }

    rc = pci_register_driver(&symmbc_driver);
    if (rc) {
        unregister_chrdev_region(dev_num, symmbc_ndevs);
        class_destroy(symmbc_class);
        pr_err("<-- %s: pci_register_driver() failed.\n", __func__);
    }

    pr_info("symmbc7x: loaded.\n");
    return rc;
}

//-------------------------------------------------------------------------
// Unload
//-------------------------------------------------------------------------
static void __exit symmbc_exit(void)
{
    pci_unregister_driver(&symmbc_driver);
    class_destroy(symmbc_class);
    unregister_chrdev_region(MKDEV(symmbc_major, 0), symmbc_ndevs);
    pr_info("symmbc7x: unloaded.\n");
}

module_init(symmbc_init);
module_exit(symmbc_exit);
